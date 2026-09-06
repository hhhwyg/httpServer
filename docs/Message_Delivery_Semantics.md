# 消息交付语义

## 范围

本文定义聊天室 v1 的可靠接收、实时广播和断线恢复边界。当前 M2 使用进程内 Repository 验证协议与并发行为；进程重启后的消息恢复、MySQL 唯一约束和事务提交将在 M3 替换该实现后验收。因此，当前版本不能将内存消息描述为跨重启持久化。

## 逻辑消息与确认

客户端为每次用户发送生成不可复用的 `clientMessageId`。服务端以 `(senderId, clientMessageId)` 作为幂等键：无论客户端使用新的 `requestId` 重试多少次，服务端都只保留一个逻辑消息，并返回该消息原有的 `messageId` 和 `createdAt`。

```text
received -> persisted -> published -> delivered
                    ^
              message.ack
```

- `received`：服务端已完成协议、鉴权、成员关系和订阅校验；还不能向客户端确认。
- `persisted`：消息已由 Repository 原子写入或命中既有幂等记录；这是 `message.ack` 的唯一可靠边界。
- `published`：新插入消息已尝试投递给本进程的在线订阅者。幂等重试不会再次发布。
- `delivered`：对端是否真正显示消息无法由一次 WebSocket 写入证明，实时链路不承诺此状态。

M3 会将 `persisted` 的实现替换为同一 MySQL 事务中的消息写入和 `(sender_id, client_message_id)` 唯一约束。发生唯一键冲突时，Repository 必须读取既有记录并形成同样的 ACK，不能抛出重复消息业务错误。

## WebSocket 协议

发送命令中的 `requestId` 标识一次传输尝试；`clientMessageId` 标识逻辑消息。服务端在 `persisted` 后返回：

```json
{
  "version": 1,
  "type": "message.ack",
  "requestId": "retry-request-2",
  "payload": {
    "clientMessageId": "client-message-1",
    "messageId": "20000000",
    "createdAt": "2026-09-06T12:30:00.123Z"
  }
}
```

只有首次插入会向其他在线订阅者发送 `message.created`。该事件和 HTTP 历史均使用相同的 `MessageDto`：`messageId`、`roomId`、`senderId`、`clientMessageId`、`content`、`createdAt`。

客户端在 ACK 超时、连接中断或 `dependency_unavailable` 短暂依赖错误后保留原来的 `clientMessageId`，以指数退避最多重发 4 次；鉴权、成员关系或非法内容错误不重试。M3 的 MySQL 依赖故障会使用该稳定错误码。重试次数耗尽后，客户端保留“发送失败”的显式手动重试入口。

## 断线恢复

每个房间以最新已合并的 `messageId` 为游标。重连后客户端重新鉴权、订阅房间，并使用：

```text
GET /api/v1/rooms/{roomId}/messages?after={lastMessageId}&limit=100
```

结果按 `messageId` 升序返回。`nextAfter` 非空时，将其作为下一页的 `after` 继续请求；为空表示已经追到当前上界。客户端按服务端 `messageId` 去重，并将同一 `clientMessageId` 的本地 pending 消息替换为已确认消息。历史 `before`/`nextBefore` 仍保留给向前翻阅旧消息，不能和 `after` 同时使用。

## 当前验收记录

- `application.chat_service`：两个线程共 100 次同键提交，所有结果成功且返回同一 `messageId`，仅一次 `inserted`。
- `e2e.message_idempotency`：发送连接未消费 ACK 后断开，以相同 `clientMessageId` 重连重发；历史记录数和接收端事件数均为 1。
- `e2e.reconnect_recovery`：接收方断线期间发送 250 条消息，重连后使用 `after` 游标分 3 页恢复，消息 ID 连续且无重复。

M3 验收时，上述“消息记录数”必须额外以 MySQL `chat_messages` 实际行数验证，并覆盖进程重启后的恢复。
