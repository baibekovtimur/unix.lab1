#include <pistache/endpoint.h>
#include <pistache/router.h>
#include <pistache/http.h>

#include <nlohmann/json.hpp>
#include <librdkafka/rdkafkacpp.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>

using json = nlohmann::json;
using namespace Pistache;

static std::atomic<bool> g_stop{false};

static void on_signal(int)
{
    g_stop.store(true);
}

static int64_t now_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

static std::string gen_request_id()
{
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist(0, std::numeric_limits<uint64_t>::max());

    auto to_hex16 = [](uint64_t x)
    {
        const char *hex = "0123456789abcdef";
        std::string s(16, '0');
        for (int i = 15; i >= 0; --i)
        {
            s[i] = hex[x & 0xF];
            x >>= 4;
        }
        return s;
    };

    // 32 hex chars
    return to_hex16(dist(rng)) + to_hex16(dist(rng));
}

struct CacheEntry
{
    json value;
    int64_t inserted_ms{0};
};

class GatewayApp
{
public:
    GatewayApp(std::string brokers,
               std::string req_topic,
               std::string res_topic,
               int port,
               int ttl_seconds)
        : brokers_(std::move(brokers)),
          req_topic_(std::move(req_topic)),
          res_topic_(std::move(res_topic)),
          port_(port),
          ttl_ms_(ttl_seconds * 1000LL) {}

    bool init_kafka()
    {
        std::string errstr;

        // Producer
        {
            std::unique_ptr<RdKafka::Conf> conf(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));
            if (!conf)
            {
                std::cerr << "[gateway] Failed to create producer conf\n";
                return false;
            }

            if (conf->set("bootstrap.servers", brokers_, errstr) != RdKafka::Conf::CONF_OK)
            {
                std::cerr << "[gateway] producer conf error: " << errstr << "\n";
                return false;
            }
            conf->set("client.id", "gateway", errstr);
            conf->set("queue.buffering.max.ms", "5", errstr);

            producer_.reset(RdKafka::Producer::create(conf.get(), errstr));
            if (!producer_)
            {
                std::cerr << "[gateway] Failed to create producer: " << errstr << "\n";
                return false;
            }
        }

        // Consumer (results)
        {
            std::unique_ptr<RdKafka::Conf> conf(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));
            if (!conf)
            {
                std::cerr << "[gateway] Failed to create consumer conf\n";
                return false;
            }

            if (conf->set("bootstrap.servers", brokers_, errstr) != RdKafka::Conf::CONF_OK)
            {
                std::cerr << "[gateway] consumer conf error: " << errstr << "\n";
                return false;
            }
            conf->set("group.id", "gateway_results_cache", errstr);
            conf->set("enable.auto.commit", "false", errstr);
            conf->set("auto.offset.reset", "earliest", errstr);

            consumer_.reset(RdKafka::KafkaConsumer::create(conf.get(), errstr));
            if (!consumer_)
            {
                std::cerr << "[gateway] Failed to create consumer: " << errstr << "\n";
                return false;
            }

            auto err = consumer_->subscribe({res_topic_});
            if (err)
            {
                std::cerr << "[gateway] subscribe error: " << RdKafka::err2str(err) << "\n";
                return false;
            }
        }

        std::cout << "[gateway] Kafka initialized. brokers=" << brokers_
                  << " req_topic=" << req_topic_ << " res_topic=" << res_topic_ << "\n";
        return true;
    }

    void start_http()
    {
        Http::Endpoint::Options opts;
        opts.threads(2);

        endpoint_ = std::make_unique<Http::Endpoint>(Address(Ipv4::any(), Port(port_)));
        endpoint_->init(opts);

        using namespace Rest;
        Routes::Post(router_, "/check", Routes::bind(&GatewayApp::handle_check, this));
        Routes::Get(router_, "/result/:id", Routes::bind(&GatewayApp::handle_result, this));
        Routes::Get(router_, "/health", Routes::bind(&GatewayApp::handle_health, this));

        endpoint_->setHandler(router_.handler());
        endpoint_->serveThreaded();

        std::cout << "[gateway] HTTP server started on 0.0.0.0:" << port_ << "\n";
    }

    void start_consumer_thread()
    {
        consumer_thread_ = std::thread([this]
                                       { this->consume_results_loop(); });
    }

    void stop()
    {
        std::cout << "[gateway] Shutting down...\n";

        if (endpoint_)
        {
            endpoint_->shutdown();
        }

        if (consumer_)
        {
            consumer_->close();
        }

        if (consumer_thread_.joinable())
        {
            consumer_thread_.join();
        }

        if (producer_)
        {
            producer_->flush(5000);
        }

        consumer_.reset();
        producer_.reset();

        RdKafka::wait_destroyed(5000);
        std::cout << "[gateway] Stopped.\n";
    }

private:
    void handle_health(const Rest::Request &, Http::ResponseWriter response)
    {
        response.send(Http::Code::Ok, "ok\n");
    }

    void handle_check(const Rest::Request &request, Http::ResponseWriter response)
    {
        try
        {
            auto body = request.body();
            auto in = json::parse(body);

            if (!in.contains("text") || !in["text"].is_string())
            {
                return send_json(response, Http::Code::Bad_Request,
                                 json{{"error", "field 'text' is required and must be string"}});
            }
            std::string text = in["text"].get<std::string>();
            std::string lang = "ru";
            if (in.contains("language") && in["language"].is_string())
            {
                lang = in["language"].get<std::string>();
            }
            if (lang != "ru" && lang != "en")
            {
                return send_json(response, Http::Code::Bad_Request,
                                 json{{"error", "language must be 'ru' or 'en'"}});
            }

            std::string request_id = gen_request_id();
            json msg = {
                {"request_id", request_id},
                {"timestamp", now_ms()},
                {"text", text},
                {"language", lang}};

            std::string payload = msg.dump();
            auto err = producer_->produce(
                req_topic_,
                RdKafka::Topic::PARTITION_UA,
                RdKafka::Producer::RK_MSG_COPY,
                const_cast<char *>(payload.data()),
                payload.size(),
                &request_id,
                nullptr);

            producer_->poll(0);

            if (err != RdKafka::ERR_NO_ERROR)
            {
                std::cerr << "[gateway] produce error: " << RdKafka::err2str(err) << "\n";
                return send_json(response, Http::Code::Service_Unavailable,
                                 json{{"error", "kafka produce failed"}, {"details", RdKafka::err2str(err)}});
            }

            std::cout << "[gateway] accepted request_id=" << request_id
                      << " bytes=" << text.size() << " lang=" << lang << "\n";

            return send_json(response, Http::Code::Ok, json{{"request_id", request_id}});
        }
        catch (const std::exception &e)
        {
            std::cerr << "[gateway] /check error: " << e.what() << "\n";
            return send_json(response, Http::Code::Bad_Request,
                             json{{"error", "invalid json"}, {"details", e.what()}});
        }
    }

    void handle_result(const Rest::Request &request, Http::ResponseWriter response)
    {
        auto id = request.param(":id").as<std::string>();

        {
            std::lock_guard<std::mutex> lk(cache_mtx_);
            auto it = cache_.find(id);
            if (it != cache_.end())
            {
                return send_json(response, Http::Code::Ok, it->second.value);
            }
        }
        return send_json(response, Http::Code::Ok, json{{"request_id", id}, {"status", "processing"}});
    }

    static void send_json(Http::ResponseWriter &response, Http::Code code, const json &j)
    {
        response.headers().add<Http::Header::ContentType>(MIME(Application, Json));
        response.send(code, j.dump());
    }

    void consume_results_loop()
    {
        std::cout << "[gateway] results consumer thread started\n";
        int64_t last_cleanup = now_ms();

        while (!g_stop.load())
        {
            std::unique_ptr<RdKafka::Message> msg(consumer_->consume(200));
            if (!msg)
                continue;

            if (msg->err() == RdKafka::ERR__TIMED_OUT)
            {
                // just poll
            }
            else if (msg->err() == RdKafka::ERR_NO_ERROR)
            {
                try
                {
                    std::string payload(static_cast<const char *>(msg->payload()), msg->len());
                    auto j = json::parse(payload);

                    if (j.contains("request_id") && j["request_id"].is_string())
                    {
                        std::string id = j["request_id"].get<std::string>();
                        {
                            std::lock_guard<std::mutex> lk(cache_mtx_);
                            cache_[id] = CacheEntry{j, now_ms()};
                        }
                        consumer_->commitSync(msg.get());
                        std::cout << "[gateway] cached result request_id=" << id
                                  << " score=" << (j.contains("score") ? j["score"].dump() : "n/a")
                                  << " status=" << (j.contains("status") ? j["status"].dump() : "n/a")
                                  << "\n";
                    }
                    else
                    {
                        std::cerr << "[gateway] invalid result message (no request_id)\n";
                        consumer_->commitSync(msg.get());
                    }
                }
                catch (const std::exception &e)
                {
                    std::cerr << "[gateway] parse result error: " << e.what() << "\n";
                    consumer_->commitSync(msg.get()); // чтобы не застрять на битом сообщении
                }
            }
            else if (msg->err() == RdKafka::ERR__PARTITION_EOF)
            {
                // ignore
            }
            else
            {
                std::cerr << "[gateway] consumer error: " << msg->errstr() << "\n";
            }

            // Cleanup TTL
            int64_t t = now_ms();
            if (t - last_cleanup >= 5000)
            {
                last_cleanup = t;
                std::lock_guard<std::mutex> lk(cache_mtx_);
                for (auto it = cache_.begin(); it != cache_.end();)
                {
                    if (t - it->second.inserted_ms > ttl_ms_)
                        it = cache_.erase(it);
                    else
                        ++it;
                }
            }
        }

        std::cout << "[gateway] results consumer thread exiting\n";
    }

private:
    std::string brokers_;
    std::string req_topic_;
    std::string res_topic_;
    int port_;
    int64_t ttl_ms_;

    std::unique_ptr<RdKafka::Producer> producer_;
    std::unique_ptr<RdKafka::KafkaConsumer> consumer_;

    std::unordered_map<std::string, CacheEntry> cache_;
    std::mutex cache_mtx_;

    Rest::Router router_;
    std::unique_ptr<Http::Endpoint> endpoint_;
    std::thread consumer_thread_;
};

static std::string getenv_or(const char *k, const std::string &defv)
{
    const char *v = std::getenv(k);
    return v ? std::string(v) : defv;
}

static int getenv_int_or(const char *k, int defv)
{
    const char *v = std::getenv(k);
    if (!v)
        return defv;
    try
    {
        return std::stoi(v);
    }
    catch (...)
    {
        return defv;
    }
}

int main()
{
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    std::string brokers = getenv_or("KAFKA_BOOTSTRAP_SERVERS", "kafka:9092");
    std::string req_topic = getenv_or("KAFKA_REQUEST_TOPIC", "text_requests");
    std::string res_topic = getenv_or("KAFKA_RESULT_TOPIC", "text_results");
    int port = getenv_int_or("HTTP_PORT", 8080);
    int ttl = getenv_int_or("RESULT_TTL_SECONDS", 600);

    GatewayApp app(brokers, req_topic, res_topic, port, ttl);

    if (!app.init_kafka())
    {
        std::cerr << "[gateway] init failed\n";
        return 1;
    }

    app.start_http();
    app.start_consumer_thread();

    while (!g_stop.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    app.stop();
    return 0;
}