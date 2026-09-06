# M2：消息可靠性

## 目标

定义服务端何时确认消息、如何去重、如何维持房间内可解释顺序，以及客户端断线后如何恢复。目标语义为：消息持久化至少一次尝试，客户端可重复提交，服务端最终只保留一个逻辑消息；实时推送允许短暂丢失，但可通过历史接口恢复。

## 核心语义

客户端为每次逻辑发送生成不可复用的 `clientMessageId`。服务端以 `(senderId, clientMessageId)` 作为幂等键，MySQL 唯一约束是最终裁决。服务端只有在消息事务提交后才返回 `message.ack`。

ACK 包含：

```json
{
  "version": 1,
  "type": "message.ack",
  "requestId": "01991f47-1b22-7ad0-8c23-59a221adbd21",
  "payload": {
    "clientMessageId": "01991f47-4600-79d5-a7df-f35ce75ff899",
    "messageId": 8451,
    "createdAt": "2026-09-06T12:30:00.123Z"
  }
}
```

客户端超时后可以使用相同 `clientMessageId` 重试。若消息已存在，服务端查询原记录并返回相同 `messageId`，不重复广播。房间内历史以 `(created_at, id)` 或单调递增 `id` 作为稳定游标；第一版不宣称严格的全局时钟顺序，只保证数据库提交后的确定性排序。

## 断线恢复计划

客户端为每个房间记录最后确认的 `messageId`。重连成功后执行：

1. 重新进行 JWT 鉴权并加入房间。
2. 请求 `after=lastMessageId&limit=100` 的增量历史。
3. 按 `messageId` 去重并合并本地实时消息与补拉结果。
4. 如果缺口超过单页，持续分页直到返回为空或到达当前上界。
5. 完成补拉后切换为正常实时展示。

服务端推送与历史返回必须复用同一个 `MessageDto`，避免同一消息出现不同字段或时间格式。

## 实施计划

1. 在协议层加入 `requestId`、`clientMessageId`、ACK 和稳定错误响应。
2. 在应用层增加 `SendMessageCommand` 与 `SendMessageResult`。
3. 内存 Repository 先实现幂等冲突路径，随后由 M3 的 MySQL 唯一索引替换最终裁决。
4. 客户端维护 `pendingMessages`，ACK 后从 pending 移除；超时采用有限次数指数退避。
5. 增加历史增量接口和前端按 `messageId` 去重逻辑。
6. 明确失败响应：鉴权失败不重试，依赖暂时不可用可重试，非法内容不重试。
7. 为发送过程记录状态：received、persisted、published、delivered，其中只有 persisted 是可靠接收边界。

## 参考

- [Tinode](https://github.com/tinode/chat)：参考服务端消息 ID、历史分页和断线判断。
- [MySQL UNIQUE 约束](https://dev.mysql.com/doc/refman/8.4/en/constraint-primary-key.html)：用数据库约束而不是进程内判断保证并发幂等。
- [Redis Pub/Sub delivery semantics](https://redis.io/docs/latest/develop/pubsub/)：实时推送是至多一次，遗漏消息必须从持久化存储恢复。

## 验收方法

组件测试同时建立两条线程，以相同 `(senderId, clientMessageId)` 并发提交 100 次，最终只有一条消息记录，全部成功响应返回同一个 `messageId`。

端到端测试目标命令：

```bash
python3 tests/e2e/MessageIdempotencyTest.py ./build/debug/WebServer 18311
python3 tests/e2e/ReconnectRecoveryTest.py ./build/debug/WebServer 18312
```

第一项测试在 ACK 前主动断开并重复提交，验证数据库只有一条记录且接收方只显示一次。第二项测试让接收方断线，发送 250 条消息后重连，分三页补拉，最终消息 ID 连续且无重复。

验收记录必须保存重复提交次数、数据库实际行数、客户端最终消息数和所有 message ID。仅在“发送正常路径可用”时不得标记完成。

## 交付物

- v1 ACK 和错误协议
- 幂等发送用例及唯一键设计
- 增量历史查询与客户端恢复逻辑
- 并发幂等和断线恢复测试
- `docs/Message_Delivery_Semantics.md`
