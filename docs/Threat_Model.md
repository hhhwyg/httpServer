# 威胁模型

## 范围与资产

生产部署由公网浏览器、Nginx TLS 边界、非 root WebServer、MySQL 和 Redis 组成。需要保护的资产是密码 hash、JWT 签名密钥、数据库和 Redis 凭据、房间成员关系、消息正文及运维端点。攻击者可以构造 HTTP/WebSocket 字节流、重放获得的 JWT、伪造房间 ID、并发建立连接并观察公开错误；攻击者不能直接进入 Compose 内网或读取服务器 secret 文件。

## 信任边界

| 边界 | 控制 | 日志与限制 |
| --- | --- | --- |
| Internet -> Nginx | 80 仅跳转 HTTPS；443 只允许 TLS 1.2/1.3；请求、header、连接和速率限制 | Nginx 生成 request ID；不得记录 Authorization、Cookie 或 URL token |
| Nginx -> WebServer | Compose 内网 `webserver:8088`，不发布宿主机端口；Nginx 代理 Upgrade | `/ws` 只接受配置的 HTTPS Origin，拒绝 query token |
| WebServer -> MySQL/Redis | 仅 Compose 内网，环境 secret，prepared statement 与 Redis argv API | 不记录密码、JWT、消息正文、数据库口令 |
| 运维人员 -> server | SSH 密钥与来源限制；secret 文件权限 600；证书目录不入 Git | 变更、备份、恢复按 Runbook 留记录 |

## 风险与处置

| 风险 | 防护 | 验证 |
| --- | --- | --- |
| 凭据猜测和账号枚举 | scrypt 随机盐、统一登录错误、Redis 用户/IP 窗口限流及 Nginx 限流 | `security.authentication_abuse`、Redis 双实例限流测试 |
| JWT 伪造、算法降级、重放 | HS256 白名单、issuer/iat/exp/max-length 校验、随机 `jti`；logout denylist 以剩余 TTL 写 Redis | `util.crypto`、Redis token-revocation 场景 |
| 房间对象越权 | 每个 HTTP 路由和 WebSocket subscribe/message 都查成员关系 | `security.authorization_matrix` 与聊天 E2E |
| 跨站 WebSocket 劫持 | Nginx 和应用双重 Origin 白名单，WSS，禁止公开入口 query token | `security.websocket_origin` |
| 输入耗尽/协议异常 | URI/header/body/frame/JSON 字段上限、mask/RSV/control-frame 校验、输出背压 | HTTP/WS 协议测试、慢消费者和 sanitizer |
| 注入 | MySQL prepared statement，Redis `redisCommandArgv`，静态路径 canonicalization | SQL/Redis 特殊字符、路径穿越测试 |
| 供应链与 secret 泄露 | secret/镜像不入 Git，gitleaks 与 Trivy 阻断发布 | 安全工作流与 release scan artifact |

## 残余风险

当前令牌 denylist 依赖 Redis；Redis 已配置但不可用时，受保护请求 fail-closed，以保障 logout 语义。直连开发模式允许不设置 Origin 白名单和 query token 兼容，仅限 localhost 测试，不得暴露到公网。应用不自行终止 TLS，证书续期、HSTS 策略和公网防火墙由 M9 部署流程负责。
