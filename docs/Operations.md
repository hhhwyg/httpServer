# 运行操作

## 优雅退出

进程通过 Linux `signalfd` 在主 EventLoop 中处理 `SIGTERM` 与 `SIGINT`。收到信号后会立即停止监听新连接，向现有 WebSocket 连接发送 `1001`（going away）关闭帧，保留连接至 `HTTPSERVER_GRACEFUL_SHUTDOWN_SECONDS`（默认 5 秒），再依次停止 Outbox、数据库执行器和 Redis 线程。

```bash
kill -TERM "$(pgrep -f WebServer)"
```

不要使用 `SIGKILL` 作为常规停止方式；它会跳过宽限期和资源回收。容器编排的停止宽限期应至少比该配置多 2 秒。

`integration.graceful_shutdown` 会验证服务收到 `SIGTERM` 后在宽限期内以状态码 0 退出。

## 健康检查

`/healthz` 适合存活探针；`/readyz` 适合就绪探针。数据库或 JWT 未配置时，进程仍可服务静态文件，但 `/readyz` 返回 `503`，不应接收需要认证的业务流量。

## 容量与超时

每个连接的待发送数据由 `HTTPSERVER_OUTPUT_BUFFER_HIGH_WATER_BYTES`（默认 256 KiB）和 `HTTPSERVER_OUTPUT_BUFFER_HARD_LIMIT_BYTES`（默认 1 MiB）约束，且硬上限必须大于高水位。高水位后房间广播等非关键推送会丢弃并增加指标；到硬上限时连接立即关闭，避免慢客户端占用无界内存。

数据库执行器队列固定容量，任务最多等待 3 秒；满载或超时会返回 `503`。MySQL 连接、读、写均使用 `HTTPSERVER_DB_OPERATION_TIMEOUT_SECONDS`，Redis 使用独立的连接/命令超时。Redis 的缓存读允许回源，认证和建房限流使用本地保守回退。
