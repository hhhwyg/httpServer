# 安全模型

## 当前边界

本项目的公开部署使用 Nginx 终止 TLS，应用、MySQL 和 Redis 不对宿主机发布端口。完整边界、资产和残余风险见 [Threat Model](Threat_Model.md)。应用仍不信任客户端伪造的转发地址；公网 IP 限流由 Nginx 负责，应用仅使用 socket 对端地址作为补充。

## 凭据与配置

- JWT secret 和数据库密码仅来自环境变量，`.env`、私钥和本地配置已被 `.gitignore` 排除。
- 服务日志只记录功能状态和数据库错误，不输出 password、JWT 或 secret。
- 未配置数据库或 JWT 时，相应认证功能显式不可用，不使用演示默认值。

## 密码

注册时密码使用 OpenSSL `EVP_PBE_scrypt` 派生 32 字节 hash，并使用每个账户独立的 16 字节随机 salt。数据库保存的格式为：

```text
scrypt$v1$32768$8$1$<base64url-salt>$<base64url-derived-key>
```

登录时按固定参数重新派生 hash，并用 `CRYPTO_memcmp` 做恒定时间比较。旧数据库中已有的明文 `passwd` 无法兼容此格式，应在部署前删除测试账号或实施一次受控的强制重置密码迁移；禁止把明文自动当作有效 hash。

密码当前接受 8 到 128 字节，用户名接受 3 到 64 个 ASCII 字母、数字、`_` 或 `-`。这是边界校验，不是完整的账号策略；速率限制、密码泄露库检查和账号恢复流程仍待实现。

## JWT

JWT 固定使用 HS256，payload 包含 `sub`、`jti`、`iss`、`iat` 和 `exp`。验证时检查：三段结构、Base64URL 字符集、签名、算法、issuer、签发时间、过期时间、最大长度和 `jti` 格式。签名比较使用 `CRYPTO_memcmp`。`POST /logout` 把 `jti` 按剩余有效期写入 Redis denylist；Redis 已配置但 denylist 无法读取时，受保护请求 fail-closed。

WebSocket 只接受完整 token 校验成功的主体，避免出现“先解码用户名、后遗漏验证”的调用路径。公网 Nginx 仅接受 `Authorization: Bearer`，拒绝 URL query token，并在 Nginx 和应用中校验 `HTTPSERVER_WS_ALLOWED_ORIGINS` 的 HTTPS Origin 白名单。直接本地开发为了兼容测试客户端可不配置白名单，不得作为公网配置。

## 认证限流

登录、注册、建房、发消息按主体或 IP 执行 Redis Lua 窗口限流；WebSocket 握手保留本地主体/IP 限流并叠加 Nginx 入口限流。默认认证维度 60 秒内最多 10 次；`HTTPSERVER_AUTH_MAX_ATTEMPTS` 和 `HTTPSERVER_AUTH_WINDOW_SECONDS` 可调整。该地址来自 socket，不信任客户端可伪造的转发头；部署在反向代理之后由 Nginx 对真实公网连接限流。

## 数据库

注册和登录改为 MySQL prepared statements，用户名与 password hash 不再拼接进 SQL。数据库连接创建失败时连接池初始化失败，空 `MYSQL*` 不会进入队列；连接池没有可用连接时立即返回失败，避免阻塞 EventLoop。

注册和登录的密码派生、密码校验及 MySQL repository 调用在两个专用工作线程中执行；响应通过所属连接的 EventLoop 回送。单连接在认证请求完成前暂停后续请求解析，确保 Keep-Alive 响应顺序。执行器队列上限为 256，饱和时认证路由返回 `503`，避免无限排队耗尽内存。
