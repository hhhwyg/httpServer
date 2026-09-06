# M3：MySQL 持久化

## 目标

让用户、房间、成员、消息和待发布事件在进程重启后仍可恢复，并将 SQL、事务和连接管理限制在 infrastructure 层。所有数据库工作继续通过有界执行器运行，不得阻塞 EventLoop。

## 数据模型

第一版 schema 使用 InnoDB 和 `utf8mb4`：

| 表 | 关键字段与约束 |
| --- | --- |
| `users` | `id` 主键，`username` 唯一，`password_hash` |
| `rooms` | `id` 主键，`name`，`owner_id`，`created_at` |
| `room_members` | `(room_id,user_id)` 联合主键，`joined_at`，外键 |
| `messages` | `id` 主键，`room_id`，`sender_id`，`client_message_id`，`content`，`created_at`，唯一键 `(sender_id,client_message_id)` |
| `message_outbox` | `event_id` 主键，`aggregate_id`，`event_type`，`payload`，`attempts`，`next_attempt_at`，`published_at` |
| `schema_migrations` | `version` 主键，`applied_at` |

历史查询索引至少包含 `(room_id,id)`；Outbox 扫描索引包含 `(published_at,next_attempt_at,id)`。所有字符串长度与应用层限制一致，外键删除策略显式定义，不依赖默认行为。

## 事务边界

- 创建房间：插入 `rooms` 与 owner 的 `room_members` 必须同一事务提交。
- 发送消息：校验成员资格、插入 `messages`、插入 `message_outbox` 必须形成明确的一致性边界。
- 幂等冲突：捕获唯一键冲突后读取已有消息并返回原 ACK。
- 退出房间：成员删除与必要的 owner 规则校验在事务内完成。

第一版允许后台轮询 Outbox，不引入 Debezium/Kafka。是否使用带 `FOR UPDATE SKIP LOCKED` 的领取查询必须通过当前 MySQL 版本验证；若仅有一个发布线程，可先使用短事务批量领取并更新租约字段。

## 实施计划

1. 将测试初始化 SQL 拆为按版本排序的 `db/migrations/001_initial.sql` 和后续迁移。
2. 启动时只检查 schema 版本；生产环境由部署命令执行迁移，不由每个应用实例并发建表。
3. 新增 `RoomRepository`、`MembershipRepository`、`MessageRepository` 的 MySQL 实现。
4. 为事务增加 RAII wrapper，析构时对未提交事务执行 rollback。
5. 所有 prepared statement 检查 prepare、bind、execute、store/fetch 的返回值并映射错误。
6. 数据库执行器增加任务超时、队列长度、拒绝计数和 shutdown drain。
7. readiness 同时检查 schema 版本、连接池和一次有超时的轻量查询。
8. 编写备份恢复脚本，在临时数据库实际恢复验证，而不是只生成 dump。

## 参考

- [MySQL 8.4 事务语句](https://dev.mysql.com/doc/refman/8.4/en/sql-transactional-statements.html)。
- [MySQL PRIMARY KEY 与 UNIQUE 约束](https://dev.mysql.com/doc/refman/8.4/en/constraint-primary-key.html)。
- [Debezium Outbox Event Router](https://debezium.io/documentation/reference/stable/transformations/outbox-event-router.html)：只参考 outbox 表、唯一事件 ID 和可靠交换思想，本项目第一版不引入 CDC。
- 当前项目 `src/sql/UserRepository.cpp` 与 `src/application/DatabaseExecutor.cpp`。

## 验收方法

使用独立 Compose MySQL 执行：

```bash
docker compose -f docker-compose.test.yml up -d mysql
cmake --build --preset debug --parallel
ctest --preset debug -L database --output-on-failure
python3 tests/integration/PersistenceRestartTest.py ./build/debug/WebServer 18313
scripts/test_backup_restore.sh
```

必须验证：应用重启后房间、成员和消息不丢；并发重复消息只有一行；事务中第二条 SQL 故意失败时第一条也回滚；连接池耗尽时请求在配置超时内返回；迁移可从空库执行且重复执行不会破坏数据；备份恢复后的行数和关键校验和一致。

## 交付物

- `db/migrations/` 与迁移执行脚本
- 三类 MySQL Repository
- RAII 事务封装
- 数据库超时、队列和指标
- 重启持久化、事务回滚、迁移和备份恢复测试
