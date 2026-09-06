# M5：多实例广播与 Transactional Outbox

## 目标

让连接到不同 WebServer 实例的同房间用户能够实时通信，并处理“MySQL 已提交但 Redis 未发布”和“事件重复发布”两类一致性问题。完成态为两实例可互发、发布失败可恢复、重复事件不重复展示。

## 事件流

```text
client A -> instance-1
  -> validate membership and payload
  -> MySQL transaction: insert message + insert outbox event
  -> ACK persisted message to A
  -> outbox publisher reads pending event
  -> Redis PUBLISH chat:events
  -> instance-1 and instance-2 receive event
  -> each instance broadcasts only to matching local sessions
  -> client B on instance-2 receives the message
```

事件 envelope 至少包含 `eventId`、`eventType`、`messageId`、`roomId`、`originInstanceId`、`occurredAt` 和完整 `MessageDto`。所有实例订阅一个全局 channel 并按本地房间连接过滤，适合校招项目的双实例规模；只有测量证明全局广播成为瓶颈后，才改为动态房间订阅或分片通道。

## Outbox 设计

消息和 Outbox 事件在同一 MySQL 事务中插入。发布线程每次领取固定批量的未发布记录，发布成功后设置 `published_at`；失败时增加 `attempts` 并按照有限指数退避更新 `next_attempt_at`。

发布成功但标记完成前进程崩溃会产生重复发布，因此每个实例维护有界的近期 `eventId` 去重集合，客户端也按 `messageId` 去重。去重集合必须有容量和过期时间，不能无限增长。

Pub/Sub 至多一次，因此客户端断线或订阅重连期间的遗漏消息通过 M2/M3 的历史补拉恢复。Outbox 保证事件最终有机会发布，不把 Pub/Sub 变成可靠消息存储。

## 实施计划

1. 在发送消息事务中插入 `message_outbox`，使用随机唯一 `eventId`。
2. 实现单进程 `OutboxPublisher`，配置批量大小、轮询间隔、租约、最大退避和并发数。
3. 发布线程不得持有数据库事务等待 Redis 网络结果；领取与标记使用短事务。
4. Pub/Sub 回调切回每个 EventLoop，由实例本地 `SessionRegistry` 按 roomId 广播。
5. `SessionRegistry` 使用稳定 connection/session ID，不使用可复用 fd 作为业务身份。
6. 实现事件去重、来源实例识别和 ACK/广播的统一 MessageDto。
7. Compose 启动两个应用实例，通过 Nginx 或不同端口连接两个客户端。
8. 为 Outbox 积压、最老事件年龄、发布失败和重复事件增加指标。

## 参考

- [Redis Pub/Sub delivery semantics](https://redis.io/docs/latest/develop/pubsub/)：明确实时通道的丢失边界。
- [Debezium Outbox Event Router](https://debezium.io/documentation/reference/stable/transformations/outbox-event-router.html)：参考事件 ID、聚合 ID、payload 和 outbox 一致性目标。
- [Redis Streams](https://redis.io/docs/latest/develop/use-cases/streaming/)：用于理解持久化流与消费组，不在第一版替换广播 Pub/Sub。
- [Mattermost](https://github.com/mattermost/mattermost) 和 [Tinode](https://github.com/tinode/chat)：参考多连接会话与实时事件模型。

## 验收方法

```bash
docker compose -f docker-compose.scale.yml up -d --build
python3 tests/e2e/CrossInstanceChatTest.py https://localhost
python3 tests/integration/OutboxRecoveryTest.py
```

跨实例测试必须确认两个客户端被路由到不同 `instanceId`，随后完成双向发送、ACK 和广播。Outbox 测试在 MySQL 提交后、Redis 发布前停止实例，重启后消息最终到达；在发布后、标记前停止实例，重启后客户端最终只显示一条逻辑消息。

停止 Redis 60 秒期间发送的消息必须留在 MySQL 和 Outbox；Redis 恢复后积压归零。接收方若当时离线，则通过历史接口恢复，而不是要求 Pub/Sub 重放。保存数据库行、Outbox 状态、事件 ID、客户端消息 ID 和实例日志作为验收证据。

## 交付物

- Outbox schema、Repository 和 Publisher
- 实例级 SessionRegistry 与稳定 session ID
- Redis 事件 envelope 与去重实现
- 双实例 Compose 配置
- 跨实例、崩溃窗口和重复发布测试
