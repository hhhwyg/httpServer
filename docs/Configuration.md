# 配置

服务可由 `HTTPSERVER_CONFIG_FILE` 指向的最小 TOML 文件读取配置，再由环境变量逐项覆盖。仓库中的 `.env.example` 和 `config.example.toml` 都只是字段清单，程序不会自动加载 `.env`；启动前请由 shell、systemd、容器编排工具或密钥管理系统注入敏感值。

```bash
export HTTPSERVER_JWT_SECRET="$(openssl rand -base64 48)"
export HTTPSERVER_JWT_ISSUER="httpServer"
export HTTPSERVER_JWT_TTL_SECONDS=3600

export HTTPSERVER_DB_HOST=127.0.0.1
export HTTPSERVER_DB_PORT=3306
export HTTPSERVER_DB_USER=httpserver
export HTTPSERVER_DB_PASSWORD='replace-this-value'
export HTTPSERVER_DB_NAME=webserver
export HTTPSERVER_DB_POOL_SIZE=4
export HTTPSERVER_AUTH_MAX_ATTEMPTS=10
export HTTPSERVER_AUTH_WINDOW_SECONDS=60
```

例如，开发环境可使用：

```bash
export HTTPSERVER_CONFIG_FILE="$PWD/config.example.toml"
export HTTPSERVER_JWT_SECRET="$(openssl rand -base64 48)"
export HTTPSERVER_DB_PASSWORD='replace-this-value'
```

支持的 TOML section 是 `jwt`、`database`、`authentication_rate_limit` 和 `limits`，字段名与 `config.example.toml` 一致。环境变量优先级更高；建议将真实 secret 和 password 只放在环境或密钥管理服务中。

| 变量 | 默认值 | 规则 |
| --- | --- | --- |
| `HTTPSERVER_JWT_SECRET` | 未设置，JWT 功能关闭 | 至少 32 字节；必须随机生成，不能提交到 Git |
| `HTTPSERVER_CONFIG_FILE` | 未设置 | 可选的 TOML 配置路径；无法读取或格式错误时拒绝启动 |
| `HTTPSERVER_JWT_ISSUER` | `httpServer` | 非空字符串 |
| `HTTPSERVER_JWT_TTL_SECONDS` | `3600` | 60 到 86400 秒 |
| `HTTPSERVER_DB_HOST` | `127.0.0.1` | MySQL 主机名或地址 |
| `HTTPSERVER_DB_PORT` | `3306` | 1 到 65535 |
| `HTTPSERVER_DB_USER` | 未设置 | 与 password、database 必须同时设置 |
| `HTTPSERVER_DB_PASSWORD` | 未设置 | 仅通过部署环境提供 |
| `HTTPSERVER_DB_NAME` | 未设置 | 与 user、password 必须同时设置 |
| `HTTPSERVER_DB_POOL_SIZE` | `4` | 1 到 64 |
| `HTTPSERVER_AUTH_MAX_ATTEMPTS` | `10` | 单个用户名/已认证主体在窗口内允许的认证请求数，1 到 1000 |
| `HTTPSERVER_AUTH_WINDOW_SECONDS` | `60` | 认证限流滑动窗口，1 到 3600 秒 |
| `HTTPSERVER_MAX_FDS` | `100000` | 1024 到 1000000；接收连接的 fd 上限 |
| `HTTPSERVER_IO_URING_QUEUE_SIZE` | `4096` | 64 到 65536；io_uring SQ/CQ 队列容量 |

未设置全部 DB 变量时，服务器仍可启动并提供静态文件和 `/ping`，但 `/register`、`/login` 返回 `503`。未设置 JWT secret 时，登录和 WebSocket 鉴权同样返回 `503` 或 `401`，不会回退到固定 secret。

任意非法数值、短 JWT secret，或只配置部分数据库凭据都会让进程在启动前退出。配置错误信息不会回显 secret 或密码。
