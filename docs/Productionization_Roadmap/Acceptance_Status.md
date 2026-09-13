# M1-M10 验收状态与证据索引

更新时间：2026-09-12。本轮修复、复现步骤与证据见 [2026-09-12 复验记录](Acceptance_2026-09-12.md)。

本文只登记实际执行过的验收。`通过（本地）` 表示代码、自动化测试或隔离 Compose 环境已达到模块要求；需要域名、受信任证书、空白云主机或长时间运行的项目单独列为外部验收，不用本地 smoke 代替。

## 总览

| 模块 | 当前状态 | 已通过的核心验收 | 尚未完成 |
| --- | --- | --- | --- |
| M1 领域与 API | 通过 | V1 房间、成员、历史分页、WebSocket envelope 与业务 E2E | 无 |
| M2 消息可靠性 | 通过 | 持久化后 ACK、幂等重试、顺序游标、1000 条断线恢复 | 无 |
| M3 MySQL 持久化 | 通过 | migration 001/002、重启恢复、事务回滚、数据库幂等、备份恢复 | 无 |
| M4 Redis 集成 | 通过 | 限流、缓存、故障恢复、认证失败、token denylist | 无 |
| M5 多实例与 Outbox | 通过 | 双实例跨 Redis 广播、Outbox 租约发布与失败重试 | 无 |
| M6 可靠性与背压 | 通过 | 慢消费者、优雅排空、依赖延迟与 Toxiproxy 故障矩阵 | 无 |
| M7 可观测性 | 通过 | Prometheus/Grafana/Alertmanager、六条规则、结构化消息链路日志 | 目标机长期告警观察并入 M9/M10 |
| M8 安全 | 通过 | 10 项安全测试、Gitleaks、Trivy HIGH/CRITICAL=0、最小权限容器 | 无 |
| M9 部署与 CI/CD | 本地门禁通过 | 双实例生产 Compose、HTTPS/WSS、真实 Docker 失败发布回滚、13 项部署回归、迁移幂等 | 真实域名与证书、空白主机、实际 Actions/registry 发布、公网 24h |
| M10 测试与压测 | 短时门禁通过 | Debug/ASan 44 项、TSan、fanout 短测、17 项压测器回归、浏览器桌面/移动交互恢复 | 多发送者并发容量、目标机 30 分钟三轮对照、24h 长稳、release artifact |

## 分模块证据

### M1-M3

- `e2e.chat_business_flow` 验证注册登录、建房、成员加入、订阅、ACK、广播和历史查询。
- `e2e.message_idempotency` 验证 ACK 丢失后的相同 `clientMessageId` 重试只产生一条消息。
- `e2e.reconnect_recovery` 默认覆盖 250 条分页恢复；另以 `HTTPSERVER_RECOVERY_MESSAGE_COUNT=1000` 执行，得到 10 页、1000 条、零遗漏/重复/乱序。
- `integration.persistence_restart`、`integration.transaction_rollback`、`integration.database_message_idempotency` 使用真实 MySQL 8.4 通过。
- `scripts/test_backup_restore.sh` 实际完成导出、导入和六张业务表行数一致性校验。

### M4-M6

- Redis 真实依赖测试覆盖分布式限流、断开恢复、密码错误、cache-aside 和 JWT denylist。
- 两个 WebServer 连接同一 MySQL/Redis 时，`CrossInstanceChatTest.py` 验证 ACK、两个实例广播与 Outbox published 状态。
- `OutboxRecoveryTest.py` 验证发布失败后的持久记录、重试和最终发布。
- `run_fault_matrix.sh` 的生命周期场景全部通过；启用 Toxiproxy 后，MySQL/Redis 延迟和断开返回预期 503，服务可恢复。

### M7-M9

- 生产 Compose 中两个 WebServer、MySQL、Redis 均 healthy；Prometheus 的两个应用目标和 node exporter 均为 UP，Grafana 数据库状态为 `ok`，Alertmanager readiness 为 `OK`。
- 本地 TLS 入口的 `/readyz` 返回 200；`PublicWssSmokeTest.py` 通过注册、登录、建房、加入、WSS 订阅、持久化 ACK、跨实例消息与 ping/pong。
- 同一消息可在日志中通过 `request_id`、`event_id` 和 `message_id` 关联到两个实例的 `websocket.broadcast` 及 `outbox.publish`。
- 浏览器使用 `Sec-WebSocket-Protocol` 完成不含 query token 的认证；服务端不回显 JWT。实际页面验证登录、WSS、ACK 消息状态与 `/logout`，桌面和移动布局无重叠且控制台无错误。
- 停止一个应用实例后，新 HTTPS/WSS 连接可由另一实例处理；恢复后两个实例重新 healthy。
- Gitleaks 8.24.2 扫描 21 个提交无泄露；Trivy 0.74.0 使用 GHCR 漏洞库扫描当前候选镜像，Ubuntu 24.04 的 HIGH/CRITICAL 为 0。

### M10

最终本地回归：

```text
Debug + real MySQL/Redis: 44/44 passed
ASan/UBSan + real MySQL/Redis: 44/44 passed
TSan isolated: chat_service, database_executor, authentication_rate_limiter passed
```

Release、epoll、6 个服务线程、本机客户端的短时业务结果：

| 场景 | 已确认投递 | 遗漏/重复/乱序 | p99 |
| --- | ---: | ---: | ---: |
| 1 房间，100 fanout，256B | 18,600 | 0 / 0 / 0 | 7.708 ms |
| 1 房间，500 fanout，256B | 30,500 | 0 / 0 / 0 | 25.930 ms |
| 1 房间，1000 fanout，256B | 35,000 | 0 / 0 / 0 | 43.165 ms |
| 4 房间，各 25 fanout，1024B | 30,575 | 0 / 0 / 0 | 2.341 ms |
| 双实例，2 房间，各 10 fanout，256B | 90 | 0 / 0 / 0 | 170.787 ms |

这些窗口只有 1-2 秒，客户端与服务端同机，作用是验证工具、协议正确性和 fanout 边界，不代表目标云主机的持续容量。简历中的稳定吞吐与运行时长必须等待下面的外部验收。

## 外部验收清单

1. 在空白 Ubuntu 云主机按 `docs/Deployment.md` 部署，并保存完整命令、镜像 digest 和耗时。
2. 绑定真实域名和 ACME 证书，在不使用 `--insecure` 的情况下通过 `PublicWssSmokeTest.py`。
3. 在目标云主机重复失败发布和数据库恢复演练；本地独立 Docker readiness 失败后的两实例原镜像恢复已于 2026-09-12 通过。
4. 单实例和双实例场景分别运行至少三轮 30 分钟，压测端与服务端分机，保存原始结果。
5. 运行 24 小时长稳，证明 RSS、fd、线程数无持续增长，Outbox 无永久积压且无未解释 5xx。
6. 将结果作为与 Git tag 对应的 release artifact，再从其中提取简历数字。
