# 故障处置

## `/readyz` 返回 503

检查 `HTTPSERVER_JWT_SECRET` 是否至少 32 字节，并确认数据库变量完整配置。再检查 MySQL 连通性和启动日志中的连接失败信息。`/healthz` 为 200 而 `/readyz` 为 503 表示进程存活，但认证流量不应送入该实例。

## 5xx 或 I/O 错误增长

查询 `/metrics` 中的 `httpserver_responses_total{class="5xx"}`、`httpserver_io_errors_total` 与 MySQL 连接池计数。空闲连接为 0 时，先排查慢查询、数据库容量与连接池大小；不要通过无限增大线程数或连接池来掩盖问题。

## 连接洪泛或慢客户端

检查 `httpserver_connections_active` 与系统 fd 使用量。通过反向代理、负载均衡器或防火墙实施 IP 连接与请求速率限制；服务自身的认证限流不替代边界防护。出现资源耗尽时先摘除实例，再保留日志和指标快照。

## 日志写入失败

确认 `/var/log/httpserver` 的磁盘空间、挂载权限和容器日志卷状态。当前日志线程没有独立磁盘失败指标或轮转策略，应由平台日志采集和日志轮转工具限制文件增长。

## 受控重启

使用 `SIGTERM`、`systemctl stop httpserver` 或容器编排的正常停止动作。等待进程退出后，再检查 `/metrics` 的最终快照和运行日志；不要使用 `SIGKILL`，除非进程超过平台宽限期仍未退出。
