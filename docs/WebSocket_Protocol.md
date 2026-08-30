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

`roomId` 固定为字符串，聊天内容字段固定为 `content`。客户端发送：

```json
{"type":"join","roomId":"10000001"}
```

```json
{"type":"chat","roomId":"10000001","content":"hello"}
```

服务端向房间内其他客户端广播：

```json
{"type":"chat","roomId":"10000001","content":"hello"}
```

单条 `content` 最长 4096 字节。不是该契约的 JSON 消息会被忽略；畸形 WebSocket 帧会关闭连接。

## 房间权限与断线

客户端必须先发送 `join`，服务端才接受该客户端对房间的 `chat`。连接关闭时服务端会从所有房间移除该连接；不存在的房间或未加入房间会收到 `type=error` 的 JSON 消息。

## 验证

`integration.websocket_smoke` 使用临时 JWT 启动真实服务，覆盖认证建房、房间列表、WebSocket 握手、ping/pong、无效房间错误，以及两个客户端加入同一房间后的双向文本广播。
