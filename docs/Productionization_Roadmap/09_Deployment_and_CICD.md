# M9：公网部署与 CI/CD

> 完成状态：本地交付物与隔离演练已完成；真实域名 HTTPS/WSS、空白云主机部署、失败发布回滚和公网 24 小时运行尚待外部环境验收，因此 M9 尚未最终关闭。

## 目标

在一台 Linux 云服务器上通过真实域名提供 HTTPS/WSS 服务，同时运行两个应用实例验证水平扩展路径。部署必须可重复、可备份、可回滚，不以手工复制二进制作为最终流程。

## 部署拓扑

云服务器建议至少 2 vCPU、4 GiB 内存和独立数据盘或可靠云盘。Docker Compose 运行 Nginx、两个 WebServer、MySQL、Redis、Prometheus、Grafana、Alertmanager 和 node_exporter。只有 Nginx 的 80/443 对公网开放；SSH 限制来源并使用密钥登录。

Nginx 负责：

- HTTP 跳转 HTTPS。
- TLS 证书和安全协议配置。
- `/ws` 的 Upgrade/Connection 转发和长连接超时。
- 两个应用实例的轮询负载均衡。
- 请求体上限、基础连接限流和访问日志。
- `/metrics`、Grafana 和管理接口的访问控制。

WebSocket 连接建立后固定在被选中的实例，不要求逐消息切换。JWT 和房间成员状态必须允许任意实例处理新连接，实时消息通过 Redis 跨实例传播。

## 实施计划

### CI/CD

Pull Request 流水线：格式检查、clang-tidy、Debug 构建、CTest、ASan/UBSan、数据库/Redis集成测试、Compose E2E、镜像构建和安全扫描。主分支只生成不可变 commit SHA 镜像；Git tag 生成版本镜像和发布说明。

部署采用受保护的手动触发或 tag 触发：拉取新镜像、执行数据库迁移检查、备份数据库、启动新版本、等待 readiness、执行 smoke test，再切换或更新服务。失败时回滚上一镜像；包含不兼容 schema 的版本必须提供向前修复方案，不能假设简单降级二进制即可回滚数据。

### 运维

1. 购买域名并配置 DNS，使用 ACME 自动签发和续期证书。
2. 配置 UFW/云安全组，仅开放 22、80、443；数据库和 Redis 仅在 Compose 网络监听。
3. secret 保存在服务器权限为 600 的环境文件或云 secret 服务，不进入镜像、日志和 Actions 输出。
4. MySQL 每日备份，保留 7 天；每周在临时容器执行一次恢复校验。
5. 为日志、Prometheus 数据和 Docker 数据设置容量、保留期和磁盘告警。
6. 容器设置 CPU、内存、PID 限制、healthcheck、restart policy、只读根文件系统和非 root 用户。
7. 编写发布、回滚、证书续期、数据库恢复、Redis 故障和磁盘告警 Runbook。
8. 公网运行至少 24 小时后再记录可用性和资源数据，不预写运行时长。

## 参考

- [Nginx WebSocket proxying](https://nginx.org/en/docs/http/websocket.html)：Upgrade 与 Connection 头转发要求。
- [Docker Compose production](https://docs.docker.com/compose/how-tos/production/)：生产 Compose 覆盖与部署建议。
- [GitHub Actions C++](https://docs.github.com/en/actions/use-cases-and-examples/building-and-testing/building-and-testing-cpp)：CMake 构建测试流程。
- 当前项目 [Deployment](../Deployment.md)、`Dockerfile.deploy` 和 `docker-compose.production.yml`。

## 验收方法

```bash
docker compose -f docker-compose.production.yml config
docker compose -f docker-compose.production.yml up -d --build
test -n "${CHAT_PUBLIC_HOST}"
curl --fail "https://${CHAT_PUBLIC_HOST}/healthz"
curl --fail "https://${CHAT_PUBLIC_HOST}/readyz"
openssl s_client -connect "${CHAT_PUBLIC_HOST}:443" -servername "${CHAT_PUBLIC_HOST}" </dev/null
python3 tests/e2e/PublicWssSmokeTest.py "wss://${CHAT_PUBLIC_HOST}/ws"
scripts/test_backup_restore.sh
```

实际验收时将 `CHAT_PUBLIC_HOST` 设置为已备案或可用的真实域名，并将命令与输出保存在 release artifact。必须证明两个客户端分别落到不同 instanceId 后仍能聊天；停止一个应用实例后新连接转移到另一实例，已断开的客户端能够重连补拉。

在空白 Ubuntu 主机仅按照文档执行即可完成部署。执行一次故意失败的 readiness 发布，流水线必须停止切换并保留上一版本；执行一次上一镜像回滚和数据库恢复演练，并记录恢复耗时。

## 交付物

- Nginx HTTPS/WSS 与双实例配置
- 完整 production Compose 和资源限制
- CI、镜像发布、部署与回滚工作流
- 数据库备份恢复脚本
- 公网 smoke test 与部署 Runbook

## 实施结果与验收

`docker-compose.production.yml` 提供 `webserver-a`、`webserver-b`、MySQL、Redis、Nginx、Prometheus、Grafana、Alertmanager 与 node exporter。两个应用分别使用独立 `instanceId` 和 Outbox publisher ID，只 `expose` 内网 8088；主机仅映射 Nginx 80/443。Nginx upstream 使用两个实例，TLS、WSS Upgrade、入口 Origin/速率限制、临时文件系统和 worker 降权均在容器内实际启动验证。

`.github/workflows/ci.yml` 将 Debug/ASan、secret scan、Trivy release image scan 和 Compose 渲染拆成阻断 job；`release.yml` 对 tag/手动发布推送 GHCR commit SHA 与版本 tag；`deploy.yml` 仅能在受保护的 production environment 下使用服务器 secret。`scripts/deploy_release.sh` 强制 SHA tag，并执行 MySQL 备份、排序迁移、双实例 readiness、Nginx 校验、可选公网 smoke 和旧应用镜像恢复。

本地隔离演练以 `httpserver:m8-security`、自签 TLS 证书和独立 Compose 项目运行：两个应用实例与 MySQL/Redis 均进入 healthy，`curl -k https://127.0.0.1/healthz` 返回 `ok`、`/readyz` 返回 `ready`；停止 `webserver-a` 后新 HTTPS 请求仍返回 `ok`，恢复后两个实例再次 healthy。演练产生的容器、网络、卷和临时证书均已清理。

`tests/e2e/PublicWssSmokeTest.py` 使用系统信任链校验证书，经公网入口完成两个临时账号的注册登录、建房和加入，并验证 WSS 订阅、持久化 ACK、跨连接 `message.created` 以及 ping/pong。`--insecure` 仅允许本地自签证书演练，不能作为公网验收证据。
