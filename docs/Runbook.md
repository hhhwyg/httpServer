# 故障处置

## `/readyz` 返回 503

检查 `HTTPSERVER_JWT_SECRET` 是否至少 32 字节，并确认数据库变量完整配置。再检查 MySQL 连通性和启动日志中的连接失败信息。`/healthz` 为 200 而 `/readyz` 为 503 表示进程存活，但认证流量不应送入该实例。

## 5xx 或 I/O 错误增长

查询 `/metrics` 中的 `httpserver_responses_total{class="5xx"}`、`httpserver_io_errors_total` 与 MySQL 连接池计数。空闲连接为 0 时，先排查慢查询、数据库容量与连接池大小；不要通过无限增大线程数或连接池来掩盖问题。

## 连接洪泛或慢客户端

检查 `httpserver_connections_active`、`httpserver_output_buffer_high_water_total`、`httpserver_output_messages_dropped_total` 与 `httpserver_slow_consumer_closed_total`。高水位增长表示消费端跟不上广播；先检查客户端网络、订阅数量和单条消息大小。硬上限关闭增长时不要调大限制掩盖问题，应先摘除异常客户端或降低广播频率。

## 依赖超时或队列拒绝

检查 `httpserver_database_executor_queue_depth`、`httpserver_database_executor_rejected_total`、`httpserver_database_executor_expired_total`、`httpserver_redis_queue_depth` 和 `httpserver_redis_errors_total`。数据库业务会返回稳定的 `503`；缓存可回源，但认证与建房的限流保持保守回退。先恢复 MySQL/Redis 延迟和连接数，再逐步恢复流量，不要通过增大队列容量制造更长的排队时间。

## 日志写入失败

确认 `/var/log/httpserver` 的磁盘空间、挂载权限和容器日志卷状态。当前日志线程没有独立磁盘失败指标或轮转策略，应由平台日志采集和日志轮转工具限制文件增长。

## 受控重启

使用 `SIGTERM`、`systemctl stop httpserver` 或容器编排的正常停止动作。确认日志依次出现主循环、Outbox、数据库执行器、Redis 的 shutdown phase，并为 `HTTPSERVER_GRACEFUL_SHUTDOWN_SECONDS` 预留额外平台宽限期；不要使用 `SIGKILL`，除非进程超过平台宽限期仍未退出。

## 发布或回滚

只从受保护的 `deploy-production` 工作流发布 40 位 commit SHA 镜像。服务器的 `/opt/httpserver/.env` 必须是 0600，且 `HTTPSERVER_IMAGE_REPOSITORY` 指向 GHCR 仓库。工作流调用 `scripts/deploy_release.sh`，该脚本在每次迁移前保存带 UTC 时间戳的 MySQL dump，并在任意 readiness/Nginx/smoke 失败时恢复之前的两个应用实例镜像。

二进制回滚不回退数据库 schema。迁移必须保持向前兼容；如遇数据错误，从发布前 dump 恢复到隔离 MySQL 后验证，再按事故流程恢复，不要对线上库执行未经演练的降级 SQL。
