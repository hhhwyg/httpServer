# 安全模型

## 当前边界

本项目仍是学习型服务器，尚未完成限流、TLS 终止、完整 HTTP 输入上限与数据库异步化。以下措施只降低已识别的认证和数据访问风险，不能替代生产安全评审。

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

JWT 固定使用 HS256，payload 包含 `sub`、`iss`、`iat` 和 `exp`。验证时检查：三段结构、Base64URL 字符集、签名、算法、issuer、签发时间和过期时间。签名比较使用 `CRYPTO_memcmp`。

WebSocket 只接受 `verifyAndExtractUsername()` 成功的 token，避免出现“先解码用户名、后遗漏验证”的调用路径。token 仍通过 WebSocket URL query 传递，可能被访问日志或代理记录；Phase 5 部署时应改为受 TLS 保护且不写入日志的认证传递方式。

## 数据库

注册和登录改为 MySQL prepared statements，用户名与 password hash 不再拼接进 SQL。数据库连接创建失败时连接池初始化失败，空 `MYSQL*` 不会进入队列；连接池没有可用连接时立即返回失败，避免阻塞 EventLoop。

数据库操作仍在 EventLoop 回调中同步执行。因此数据库慢或不可用时仍会影响该 EventLoop 的响应能力。后续应通过独立执行器和 repository 接口把阻塞数据库 I/O 移出网络事件循环。
