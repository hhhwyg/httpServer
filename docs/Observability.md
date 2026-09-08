# 可观测性

## 端点

- `GET /healthz`：进程存活检查，始终返回 `200 ok`，不依赖数据库或 JWT。
- `GET /readyz`：服务就绪检查。JWT 已启用且 MySQL 连接池已初始化时返回 `200 ready`；否则返回 `503 not ready`。
- `GET /metrics`：Prometheus 文本格式指标。该端点不要求认证，因此应仅在内网、管理网或反向代理 ACL 后暴露。

## 指标

`/metrics` 提供基础连接与响应指标，以及低基数的 `httpserver_http_requests_total{method,route,status_class}`、HTTP/WS 延迟直方图、WebSocket 连接和消息结果、数据库/Redis 队列、Outbox 积压及慢消费者指标。路由会归一为固定模板，指标只记录聚合数值，不记录用户名、密码、JWT、请求体、原始 URI 或任意业务 ID。

每个 HTTP 响应还会产生 JSON 结构化日志，其中包含 `timestamp_ms`、`instance_id`、`request_id`、`connection_id`、`operation`、`duration_ms` 与稳定 `error_code`。客户端可提供 64 字节以内的字母数字、`-`、`_` request ID，其余情况由服务生成；WebSocket 与 Outbox 事件额外携带连接 ID 或 event ID。日志不包含令牌、密码或消息正文。

一次 5xx 排查应先检查 `httpserver_responses_total{class="5xx"}` 是否增长，再核对 `httpserver_database_connections_free`、`httpserver_io_errors_total` 和应用日志的时间窗口。认证服务未就绪时，优先查看 `/readyz` 与数据库连接配置。

## 验证

```bash
curl -i http://127.0.0.1:8088/healthz
curl -i http://127.0.0.1:8088/readyz
curl http://127.0.0.1:8088/metrics
```

`integration.http_protocol` 会覆盖三个端点在未配置数据库/JWT 时的契约。

本地观测栈和告警规则验证：

```bash
docker compose -f docker-compose.observability.yml up -d --build
curl --fail http://127.0.0.1:19090/-/ready
curl --fail http://127.0.0.1:13000/api/health
docker run --rm --entrypoint promtool \
  -v "$PWD/deploy/prometheus:/etc/prometheus:ro" prom/prometheus:v3.5.0 \
  check config /etc/prometheus/prometheus.yml
```

进程停止语义与信号验证见 `docs/Operations.md`。
