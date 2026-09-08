# M8：安全与滥用防护

> 完成状态：已完成（2026-09-06）。威胁模型、JWT `jti` denylist、Origin 双重校验、分布式滥用限流、最小权限入口和安全回归已落地；扫描在 CI/release 阶段执行。

## 目标

让服务可以在 Nginx 和最小权限网络边界后公开访问，重点防止认证绕过、越权访问、暴力尝试、输入耗尽、注入、敏感信息泄露和不受信任 WebSocket 来源。安全结论以威胁模型和测试为依据。

## 威胁模型

需要保护的资产包括用户密码哈希、JWT 密钥、数据库凭据、房间成员关系和聊天内容。攻击者假设可以构造任意 HTTP/WebSocket 字节流、重复请求、建立大量连接、伪造 roomId、重放 token 和观察公开错误响应，但不能直接进入云服务器内网。

主要边界：公网到 Nginx、Nginx 到应用、应用到 MySQL/Redis、部署人员到 secret。每个边界记录认证方式、允许端口、超时和日志策略。

## 实施计划

1. 保留 scrypt 密码哈希和随机盐，校验参数版本，为未来升级预留 rehash 路径。
2. JWT 校验算法白名单、issuer、签发时间、过期时间和最大长度；增加 `jti`，退出登录时可选择以剩余 TTL 写入 Redis denylist。
3. HTTP 房间接口和 WebSocket 每个命令都进行成员授权，不把“成功握手”当成房间权限。
4. Nginx 仅允许 HTTPS/WSS；校验 WebSocket `Origin` 白名单；设置请求体、header、连接和速率限制。
5. Redis Lua 实现用户/IP 维度的登录、注册、建房、发消息和握手限流。
6. MySQL 始终使用 prepared statement；Redis 命令使用 argv API，不拼接用户输入。
7. 维持路径穿越、请求行/header/body、WebSocket frame 和 JSON 字段上限。
8. 容器以非 root 运行，只读根文件系统，MySQL/Redis 不映射公网端口，secret 不写入镜像和 Git。
9. 日志屏蔽 Authorization、Cookie、密码、JWT、数据库口令和消息正文。
10. 增加依赖版本扫描、镜像扫描和 secret 扫描，发现高危问题时阻止发布。

## 错误与审计

认证错误对外返回统一信息，避免区分用户名不存在和密码错误。安全日志记录稳定事件类型、主体哈希或内部 ID、来源 IP、结果和限流原因，不记录凭据。管理员审计功能不是第一版目标，但注册、登录失败、权限拒绝和限流应有聚合指标。

## 参考

- [OWASP API Security](https://owasp.org/www-project-api-security/)：认证、对象级授权、资源消耗和安全配置风险。
- [RFC 6455](https://www.rfc-editor.org/rfc/rfc6455.html)：Origin、mask、控制帧和关闭要求。
- [Redis 限流模式](https://redis.io/docs/latest/commands/incr/)。
- 当前项目 [Security Model](../Security_Model.md) 与 [Static File Security](../Static_File_Security.md)。

## 验收方法

```bash
ctest --preset asan -L security --output-on-failure
python3 tests/security/AuthorizationMatrixTest.py
python3 tests/security/AuthenticationAbuseTest.py
python3 tests/security/WebSocketOriginTest.py
gitleaks detect --source . --no-banner
trivy image httpserver:release
```

授权矩阵覆盖 owner、成员、非成员和过期 token 对每个 API/WS 命令的结果。滥用测试从两个实例并发登录，确认共享限流不会被绕过且窗口后恢复。协议测试发送未掩码帧、非法 RSV、超大 payload、错误 Origin、路径穿越、SQL 特殊字符和 Redis 特殊字符，服务应拒绝且不崩溃。

使用 `docker compose ps` 和主机端口扫描确认公网只暴露 80/443，应用、MySQL、Redis、Prometheus 和 Grafana 管理端口受防火墙或内网限制。扫描报告中的高危项必须修复或在发布记录中说明不可利用证据。

## 交付物

- `docs/Threat_Model.md` 与授权矩阵
- JWT jti/退出策略和 Origin 校验
- 分布式滥用限流
- 安全测试、secret 扫描和镜像扫描
- 网络与容器最小权限配置

## 实施结果与验收

`docs/Threat_Model.md` 记录资产、信任边界、攻击路径、控制和残余风险。JWT 新签发 token 包含随机 `jti`，`POST /logout` 以剩余 TTL 写 Redis denylist；配置 Redis 后 denylist 查询失败会拒绝受保护请求。密码继续使用版本化 scrypt hash，MySQL 使用 prepared statement，Redis 通过 argv API 传入所有用户数据。真实 Redis 验收确认 token 在 logout 前为 200、logout 返回 204、之后同 token 为 401。

`HTTPSERVER_WS_ALLOWED_ORIGINS` 配置 HTTPS Origin 白名单；应用与 `deploy/nginx/httpserver.conf.template` 都执行该校验，公网代理拒绝 query token。`docker-compose.production.yml` 只发布 Nginx 的 80/443，WebServer 非 root、只读根文件系统、限制 PID/内存并移除 capability；MySQL/Redis 仅在 Compose 网络可达。

```bash
ctest --test-dir build/debug -L security --output-on-failure
ctest --test-dir build/asan -L security --output-on-failure
# 结果：两组均 6/6 通过（scrypt/JWT、配置校验、限流、账号枚举、Origin、授权矩阵）。
```

其中 `security.authentication_abuse` 验证未知账号与错误密码同样返回 401、窗口限流及恢复；`security.websocket_origin` 验证缺失/恶意 Origin 为 403、白名单 Origin 为 101；`security.authorization_matrix` 验证 owner/member/non-member/过期 token 的 200/200/403/401 结果，并覆盖 member/non-member 的 WebSocket subscribe。Gitleaks 8.24.2 扫描 20 个提交无泄露；Trivy 0.74.0 使用 `public.ecr.aws/aquasecurity/trivy-db:2` 扫描 `httpserver:m8-security`，HIGH/CRITICAL 为 0。M9 的 CI 将两项设为发布阻断并保存报告 artifact。
