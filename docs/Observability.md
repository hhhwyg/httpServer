# 可观测性

## 端点

- `GET /healthz`：进程存活检查，始终返回 `200 ok`，不依赖数据库或 JWT。
- `GET /readyz`：服务就绪检查。JWT 已启用且 MySQL 连接池已初始化时返回 `200 ready`；否则返回 `503 not ready`。
- `GET /metrics`：Prometheus 文本格式指标。该端点不要求认证，因此应仅在内网、管理网或反向代理 ACL 后暴露。

## 指标

`/metrics` 提供当前连接数、累计连接数、累计请求数、2xx/4xx/5xx 响应数、I/O 错误数、活跃房间数及 MySQL 连接池空闲/使用数。指标只记录聚合数值，不记录用户名、密码、JWT 或请求体。

一次 5xx 排查应先检查 `httpserver_responses_total{class="5xx"}` 是否增长，再核对 `httpserver_database_connections_free`、`httpserver_io_errors_total` 和应用日志的时间窗口。认证服务未就绪时，优先查看 `/readyz` 与数据库连接配置。

## 验证

```bash
curl -i http://127.0.0.1:8088/healthz
curl -i http://127.0.0.1:8088/readyz
curl http://127.0.0.1:8088/metrics
```

`integration.http_protocol` 会覆盖三个端点在未配置数据库/JWT 时的契约。

进程停止语义与信号验证见 `docs/Operations.md`。
