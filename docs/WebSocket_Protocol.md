# WebSocket 协议

## 握手

连接地址为：

```text
ws://127.0.0.1:8088/ws?token=<JWT>
```

服务端要求 `Upgrade: websocket`、`Connection: Upgrade`、`Sec-WebSocket-Key` 和 `Sec-WebSocket-Version: 13`。JWT 在升级前校验，失败返回 `401`，不会切换到 WebSocket 状态。

## 帧边界

- 仅接受客户端掩码帧；未掩码帧会关闭连接。
- 仅接受未设置 RSV 位的帧；文本消息支持使用 continuation 帧分片，单条重组消息上限为 1 MiB。
- 文本和二进制长度上限为 1 MiB；控制帧上限为 125 字节。
- 支持文本帧、`ping`/`pong` 和 `close`。收到 `ping` 会返回内容相同的 `pong`。
- 二进制消息当前不进入聊天室 JSON 协议，会以 `1003` close code 拒绝；非法 opcode、嵌套分片和超限消息会返回协议错误 close frame。

服务端所有聊天室消息都会编码为 RFC 6455 的无掩码文本帧后再写出，绝不直接把 JSON 裸字节写入 TCP 流。

## JSON 契约

新客户端统一使用 `version=1` envelope，命令包括 `room.subscribe` 和 `message.send`。服务端响应包括 `room.subscribed`、`message.ack`、`message.created` 和 `error`。完整字段与示例见 [API_v1.md](API_v1.md)。

`requestId`、`roomId`、`clientMessageId` 都是 1 至 128 字节的非空字符串，单条 `content` 为 1 至 4096 字节。缺失字段、错误类型、未知版本和未知消息类型都会返回带稳定错误码的 v1 `error` envelope，不再静默忽略。

旧 `join/chat` JSON 暂时只作为兼容层保留，新前端和新测试不再依赖它。

## 房间权限与断线

客户端必须先通过 HTTP 成为房间成员，再发送 `room.subscribe` 挂载当前连接。只有同时满足成员关系和连接订阅，服务端才接受 `message.send`。退出房间会立即移除该用户在房间中的在线连接；连接关闭时也会从所有房间移除该 session。

## 验证

`application.chat_protocol` 覆盖协议字段、类型、版本和大小限制；`e2e.chat_business_flow` 覆盖注册登录、成员权限、ACK、实时推送、重复消息、历史游标和退出后的权限撤销。`integration.websocket_smoke` 继续覆盖旧协议兼容性和 WebSocket 帧行为。
