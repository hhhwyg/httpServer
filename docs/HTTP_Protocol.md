# HTTP 协议边界

## 范围

`HttpData` 目前仍同时承担连接协调和 HTTP 状态机职责。Phase 2 的目标不是在这一轮迁移全部目录，而是先把来自 socket 的不可信字节流变成受限制、可预测的请求，再由现有路由分发。

## 请求状态机

```text
PARSE_URI -> PARSE_HEADERS -> RECV_BODY -> ANALYSIS -> FINISH
     ^                                                   |
     +------------------- keep-alive --------------------+
```

每个状态都可以因为半包返回“需要更多数据”，不会把尚未收齐的请求交给控制器。解析失败会终止本次连接，避免错误状态与后续请求混合。

## 输入限制

| 项目 | 上限 | 超限行为 |
| --- | ---: | --- |
| 请求行 | 8 KiB | `400 Bad Request` |
| URI（含 query） | 2048 字节 | `400 Bad Request` |
| 请求头区 | 16 KiB | `400 Bad Request` |
| 请求头数量 | 100 | `400 Bad Request` |
| 单个 header 名/值 | 128 / 8192 字节 | `400 Bad Request` |
| `Content-Length` 与请求体 | 1 MiB | `400 Bad Request` 或 `413 Payload Too Large` |
| 缓冲中的 HTTP 请求总量 | 1040 KiB | `413 Payload Too Large` |

Header 名在解析时转为小写，因此 `Content-Length`、`content-length` 等写法等价。数值长度不用 `stoi`，而是逐字符解析并在计算前检查上限，异常输入不会抛出异常或造成整数溢出。

## 路由与响应

- 支持 `GET`、`POST`、`HEAD`；其他方法返回 `405`。
- URI 会拆为 `path_` 与 `query_`，路由只能匹配 `path_`。
- HTTP/1.1 默认 Keep-Alive；`Connection: keep-alive` 控制 HTTP/1.0 的长连接。
- `HEAD` 使用与 `GET` 相同的状态码和 `Content-Length`，但不发送响应体。

当前公开的轻量级健康检查是 `GET /ping`，返回 `200` 和 `OK`。它不依赖数据库与 JWT，适合启动探测。

## 验证

```bash
cmake --build --preset debug --parallel
ctest --preset debug -L integration --output-on-failure
```

`integration.http_smoke` 会以 `epoll` 后端启动真实服务，覆盖 `/ping`、静态首页、`HEAD` 响应头和路径穿越拒绝。`integration.http_protocol` 额外覆盖同连接 Keep-Alive、POST body 后的下一请求、未支持方法的 `405`、畸形 header 和超长 header。

## 后续工作

下一步将把 parser、response writer 和 router 从 `HttpData` 分离，并补充分块传输编码、百分号解码规则和更多畸形报文测试。
