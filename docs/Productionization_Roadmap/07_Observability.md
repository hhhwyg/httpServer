# M7：可观测性

> 完成状态：已完成（2026-09-06）。指标、结构化关联日志、Prometheus/Grafana、告警规则及场景验收已完成。

## 目标

从一次用户可见故障出发，能够通过指标判断影响范围，通过关联日志定位实例、连接、请求、消息和依赖错误，并通过告警与 Runbook 给出处理动作。观测代码不得在高频路径引入明显锁竞争或无界标签。

## 指标设计

在线服务采用请求量、错误率和延迟为主的 RED 指标，并补充依赖与队列状态：

```text
httpserver_http_requests_total{method,route,status_class}
httpserver_http_request_duration_seconds{route}
httpserver_websocket_connections
httpserver_websocket_messages_total{type,result}
httpserver_websocket_broadcast_duration_seconds
httpserver_output_buffer_bytes
httpserver_slow_consumer_disconnects_total
httpserver_database_operations_total{operation,result}
httpserver_database_operation_duration_seconds{operation}
httpserver_database_executor_queue_size
httpserver_redis_commands_total{command,result}
httpserver_redis_command_duration_seconds{command}
httpserver_redis_cache_total{cache,result}
httpserver_outbox_pending
httpserver_outbox_oldest_age_seconds
httpserver_outbox_publish_total{result}
```

禁止将 userId、roomId、messageId、requestId、原始 URI 或错误文本作为 label。路由使用规范化模板，状态码按有限类别组织。延迟使用 histogram，并根据本项目实测分布设置 bucket。

## 日志与关联

结构化日志至少包含 `timestamp`、`level`、`instance_id`、`request_id`、`connection_id`、`event_id`、`operation`、`duration_ms` 和稳定 `error_code`。敏感字段、JWT、密码、消息正文默认不记录。

HTTP 请求入口生成或接收合法 request ID；WebSocket 升级后生成稳定 connection ID；发送消息沿用 request ID 并增加 clientMessageId/messageId。Outbox 发布沿用 event ID，使数据库记录、发布日志和接收日志可以关联。

## 仪表盘与告警

Grafana 至少提供四个 dashboard row：流量与延迟、WebSocket、MySQL/Redis、Outbox 与资源。第一版告警包括：实例不可用、5xx 比例升高、p99 延迟升高、Outbox 最老事件超阈值、数据库执行队列接近容量、Redis 连续错误、慢客户端断开激增。

每条告警设置持续时间避免瞬时抖动，并链接到项目 Runbook。告警优先针对用户症状，再用依赖指标解释原因。

## 实施计划

1. 重构现有 Metrics，增加 counter、gauge 和固定桶 histogram。
2. 为 HTTP、WebSocket、数据库、Redis、Outbox 和输出缓冲埋点。
3. 扩展异步日志字段并实现 request/connection/event 上下文传递。
4. 增加 Prometheus scrape 配置、Grafana datasource 和版本化 dashboard JSON。
5. 增加 recording rules 与 alerting rules，并使用 promtool 校验。
6. 在故障注入测试中验证指标和日志确实反映故障，而不是只检查文件存在。
7. 测量开启观测前后的 `/ping` 与消息广播性能，记录开销。

## 参考

- [Prometheus instrumentation practices](https://prometheus.io/docs/practices/instrumentation/)：在线服务的请求、错误、延迟与标签基数建议。
- [Prometheus naming](https://prometheus.io/docs/practices/naming/)：指标命名、单位和 label 约束。
- [Prometheus alerting rules](https://prometheus.io/docs/prometheus/latest/configuration/alerting_rules/)：告警持续时间、标签和注释。
- 当前项目 [Observability](../Observability.md)。

## 验收方法

```bash
docker compose -f docker-compose.observability.yml up -d --build
curl --fail http://127.0.0.1:9090/-/ready
curl --fail http://127.0.0.1:3000/api/health
promtool check config deploy/prometheus/prometheus.yml
promtool check rules deploy/prometheus/rules.yml
python3 tests/observability/MetricsScenarioTest.py
```

测试依次制造正常请求、401、500、Redis 超时、数据库队列满、慢客户端和 Outbox 积压，并查询 `/metrics` 验证对应计数与 gauge。随机抽取一个 messageId，必须能从应用接收日志追踪到 MySQL 持久化、Outbox 发布和目标实例广播。

观测开销验收使用同一版本、同一机器、同一参数对照；吞吐和 p99 的差异必须记录。如果性能下降超过预设 5%，用 profile 定位后再决定采样或减少热路径指标。

## 交付物

- 完整指标定义与埋点
- 结构化关联日志
- Prometheus、Grafana 和规则配置
- 指标场景测试与观测开销报告
- 告警到 Runbook 的链接

## 实施结果与验收

HTTP 指标采用固定方法、归一化路由和状态类别；请求与 WebSocket 广播延迟提供固定 bucket、`sum`、`count`。数据库执行器、Redis、Outbox、输出背压与慢消费者均由 `/metrics` 暴露，避免使用任意业务 ID 作为 label。结构化日志包含实例、请求/连接/事件关联 ID、消息 ID、操作、耗时和错误码，且不记录敏感字段。实际公网入口 smoke 中，同一条消息通过 `request_id`、`event_id`、`message_id` 关联到两个实例的 `websocket.broadcast` 和发布实例的 `outbox.publish`。

`deploy/prometheus/` 包含 scrape 与六条持续告警规则，`deploy/grafana/` 预置流量与延迟、WebSocket、依赖、Outbox/资源四个 dashboard row。真实 production Compose 验收结果：Prometheus 中 `webserver-a`、`webserver-b` 与 node exporter 三个目标均为 `up`，Grafana `/api/health` 返回 `database: ok`，Alertmanager readiness 返回 `OK`；`promtool check config` 与 `promtool check rules` 均通过。

```bash
ctest --test-dir build/debug -R 'application.metrics|observability.metrics_scenario' --output-on-failure
# 结果：通过；覆盖 2xx/4xx/5xx、归一化路由和依赖队列指标。
```
