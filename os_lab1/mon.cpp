#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <chrono>

using namespace std::chrono_literals;

struct Event
{
    int id;
    explicit Event(int id) : id(id) {}
};

class EventMonitor
{
    std::mutex mtx;
    std::condition_variable cv;
    std::unique_ptr<Event> event;
    bool closed = false;

public:
    void send(std::unique_ptr<Event> ev)
    {
        std::unique_lock lock(mtx);
        cv.wait(lock, [&]
                { return !event && !closed; });

        event = std::move(ev);
        cv.notify_one();
    }

    std::unique_ptr<Event> receive()
    {
        std::unique_lock lock(mtx);
        cv.wait(lock, [&]
                { return event || closed; });

        if (!event)
            return nullptr;

        auto result = std::move(event);
        cv.notify_one();
        return result;
    }

    void close()
    {
        std::lock_guard lock(mtx);
        closed = true;
        cv.notify_all();
    }
};

int main()
{
    EventMonitor monitor;
    std::mutex coutMutex;
    const int EVENT_COUNT = 5;

    std::setlocale(LC_ALL, "");

    std::thread producer([&]
                         {
        for (int i = 1; i <= EVENT_COUNT; ++i) {
            std::this_thread::sleep_for(1s);

            {
                std::lock_guard lock(coutMutex);
                std::cout << "Producer: send event " << i << '\n';
            }

            monitor.send(std::make_unique<Event>(i));
        }
        monitor.close(); });

    std::thread consumer([&]
                         {
        while (auto ev = monitor.receive()) {
            {
                std::lock_guard lock(coutMutex);
                std::cout << "Consumer: received event " << ev->id << '\n';
            }
        } });

    producer.join();
    consumer.join();

    return 0;
}
