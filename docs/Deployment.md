# 部署

## 容器

`Dockerfile.deploy` 是生产运行镜像：构建阶段编译 release 二进制，运行阶段只包含必要动态库并以非 root `httpserver` 用户运行。默认使用 `epoll`，避免 Docker 宿主的 `io_uring` 安全策略造成不可预测的启动失败。生产 Compose 运行 Nginx、两个 WebServer 实例、MySQL、Redis、Prometheus、Grafana、Alertmanager 与 node exporter。WebServer 只在 Compose 网络暴露 8088，由 Nginx 在 80/443 提供 HTTP->HTTPS、TLS/WSS、Origin、连接和速率限制；依赖与观测端口不映射到主机。

```bash
export HTTPSERVER_JWT_SECRET="$(openssl rand -base64 48)"
export HTTPSERVER_DB_PASSWORD='replace-this-value'
export MYSQL_ROOT_PASSWORD='replace-root-password'
export HTTPSERVER_PUBLIC_ORIGIN='https://chat.example.com'
export HTTPSERVER_GRAFANA_ADMIN_PASSWORD='replace-grafana-password'
install -d -m 700 deploy/nginx/tls
# Place ACME-managed tls.crt/tls.key in deploy/nginx/tls; never commit them.
docker compose -f docker-compose.production.yml up -d --build
```

示例 Compose 设置了只读根文件系统、受限 tmpfs、PID/内存/CPU 上限和 `no-new-privileges`。应用容器移除全部 Linux capabilities；Nginx 仅保留绑定 HTTPS、读取 0600 私钥、设置临时缓存目录和降权 worker 所需 capabilities。MySQL、Redis 和 `/metrics` 不发布公网端口。

服务器把 `.env` 保存为 `/opt/httpserver/.env` 且权限为 0600，至少包含 `HTTPSERVER_JWT_SECRET`、`HTTPSERVER_DB_PASSWORD`、`MYSQL_ROOT_PASSWORD`、`HTTPSERVER_GRAFANA_ADMIN_PASSWORD`、`HTTPSERVER_PUBLIC_ORIGIN` 与 `HTTPSERVER_IMAGE_REPOSITORY`。防火墙/安全组只开放 22（限制来源）、80、443；Prometheus/Grafana 不发布主机端口，只能经 SSH 隧道或额外受控管理入口访问。发布不可使用 `latest`：tag 工作流会生成 commit SHA 镜像，受保护的 `deploy-production` 工作流在服务器执行 `scripts/deploy_release.sh`，依次完成依赖健康检查、备份、幂等迁移、双实例 readiness、Nginx 语法和可选公网 smoke；失败时恢复此前应用镜像。

## systemd

`deploy/httpserver.service` 是示例单元。安装时创建 `httpserver` 系统用户、`/opt/httpserver` 程序目录、`/var/log/httpserver` 可写日志目录，并把 JWT 与数据库变量写入权限为 `0600` 的 `/etc/httpserver/httpserver.env`。

```bash
sudo install -m 0644 deploy/httpserver.service /etc/systemd/system/httpserver.service
sudo systemctl daemon-reload
sudo systemctl enable --now httpserver
```

停止使用 `systemctl stop httpserver`，它发送 `SIGTERM` 并保留至少 5 秒给服务完成固定 2 秒的退出宽限期。

常见故障的判定与操作步骤见 `docs/Runbook.md`。
