# 部署

## 容器

`Dockerfile.deploy` 是生产运行镜像：构建阶段编译 release 二进制，运行阶段只包含必要动态库并以非 root `httpserver` 用户运行。默认使用 `epoll`，避免 Docker 宿主的 `io_uring` 安全策略造成不可预测的启动失败。

```bash
export HTTPSERVER_JWT_SECRET="$(openssl rand -base64 48)"
export HTTPSERVER_DB_PASSWORD='replace-this-value'
export MYSQL_ROOT_PASSWORD='replace-root-password'
docker compose -f docker-compose.production.yml up --build
```

示例 Compose 设置了只读根文件系统、`/tmp` tmpfs、PID/内存/CPU 上限和 `no-new-privileges`。它是单机部署示例，MySQL 数据卷、日志卷和密码管理需要由实际平台接管。生产入口应由 Nginx、Caddy 或云负载均衡器执行 TLS 终止，并限制 `/metrics` 的访问来源。

## systemd

`deploy/httpserver.service` 是示例单元。安装时创建 `httpserver` 系统用户、`/opt/httpserver` 程序目录、`/var/log/httpserver` 可写日志目录，并把 JWT 与数据库变量写入权限为 `0600` 的 `/etc/httpserver/httpserver.env`。

```bash
sudo install -m 0644 deploy/httpserver.service /etc/systemd/system/httpserver.service
sudo systemctl daemon-reload
sudo systemctl enable --now httpserver
```

停止使用 `systemctl stop httpserver`，它发送 `SIGTERM` 并保留至少 5 秒给服务完成固定 2 秒的退出宽限期。

常见故障的判定与操作步骤见 `docs/Runbook.md`。
