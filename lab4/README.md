# Практическая работа: «Горизонтально масштабируемый сервис» (C++ + Apache Kafka)

## 1) Архитектура

### Компоненты и роли
- Gateway/API (controller):
  - Принимает текст по HTTP.
  - Публикует задания в Kafka topic text_requests (producer).
  - Параллельно читает результаты из text_results (consumer) и хранит их в памяти (unordered_map) с TTL.
  - Отдаёт результат по GET /result/{request_id}.

- Worker (реплицируемый):
  - Читает задания из text_requests в одном consumer group.
  - Считает метрики качества текста (без ML).
  - Публикует результат в text_results.

- Kafka broker (KRaft):
  - Один брокер Kafka в режиме KRaft (без Zookeeper).
  - Auto-create топиков выключен, топики создаёт init-контейнер.

### Топики
- text_requests — входные задания  
- text_results — результаты

Рекомендуемые параметры (в проекте так и сделано):
- text_requests: partitions=6 (важно для масштабирования workers), replication-factor=1  
- text_results: partitions=3, replication-factor=1

### Формат сообщений (JSON)

Запрос (text_requests):
```json
{
  "request_id": "32hex...",
  "timestamp": 1730000000000,
  "text": "....",
  "language": "ru|en"
}