# WebSocket 协议

## 握手

连接地址为：

```text
ws://127.0.0.1:8088/ws?token=<JWT>
```

服务端要求 `Upgrade: websocket`、`Connection: Upgrade`、`Sec-WebSocket-Key` 和 `Sec-WebSocket-Version: 13`。JWT 在升级前校验，失败返回 `401`，不会切换到 WebSocket 状态。

## 帧边界

- 仅接受客户端掩码帧；未掩码帧会关闭连接。
- 仅接受未设置 RSV 位、未分片（`FIN=1`）的帧。
- 文本和二进制长度上限为 1 MiB；控制帧上限为 125 字节。
- 支持文本帧、`ping`/`pong` 和 `close`。收到 `ping` 会返回内容相同的 `pong`。
- 当前不支持二进制消息和分片消息，它们会关闭连接。

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

## 后续工作

聊天室断线清理和房间授权仍需要提炼为独立 `ChatService`；分片重组、显式 close code 和 WebSocket 端到端测试也应在该模块拆分后补齐。
