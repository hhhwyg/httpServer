# M6：可靠性、超时与背压

> 完成状态：已完成（2026-09-06）。容量、deadline、慢消费者处理、信号排空和 Toxiproxy 故障验收均已实现。

## 目标

确保慢客户端、突发流量、MySQL/Redis 变慢和关闭过程不会导致 EventLoop 阻塞、内存无界增长或请求永久悬挂。每项资源都有容量，每个外部调用都有截止时间，每种过载都有明确拒绝或断开策略。

## 容量边界

需要配置并公开以下限制：

| 资源 | 第一版策略 |
| --- | --- |
| 单连接输入缓冲 | HTTP 与 WebSocket 使用既有协议上限，超限返回错误或 close 1009 |
| 单连接输出缓冲 | 设置高水位；超过后暂停业务推送，达到硬上限主动关闭慢客户端 |
| 数据库任务队列 | 有界；满时返回 `503 DEPENDENCY_OVERLOADED` |
| Redis 任务队列 | 有界；缓存请求可回源，强依赖请求快速失败 |
| Outbox 批量与积压 | 每批固定上限；监控最老事件年龄和总量 |
| 重试 | 仅重试可恢复错误，有限次数，指数退避加随机抖动 |
| 请求体与消息 | 维持显式上限，解析前后都校验 |

发送缓冲区必须跟踪待发送字节数。软高水位触发指标和暂停非关键推送，硬上限关闭连接并记录原因。广播时先构造目标快照，再逐连接排队，不能在房间锁内执行网络写入。

## 超时和降级

- MySQL 查询、获取连接、Redis 命令、Outbox 发布分别设置独立超时。
- `/healthz` 只代表进程存活；`/readyz` 在关键依赖不可用或 schema 不匹配时返回 503。
- Redis 缓存不可用时回源 MySQL；分布式限流不可用时，认证与建房采用 fail-closed，普通历史缓存采用 fail-open 回源。
- MySQL 不可用时禁止确认新消息，但已有 WebSocket 连接仍可收到服务状态错误和完成关闭握手。
- 关闭时停止 accept，拒绝新业务任务，等待有限宽限期，发送 WebSocket close 1001，排空日志和依赖执行器。

## 实施计划

1. 列出所有队列、连接池、buffer 和重试循环，逐项增加配置上限和指标。
2. 为输出缓冲增加字节计数、高水位回调和慢客户端关闭原因。
3. 为 DatabaseExecutor、RedisExecutor、OutboxPublisher 增加 deadline 与取消标记。
4. 将依赖错误映射为稳定 HTTP/WS 业务错误，不泄露底层连接字符串。
5. 实现 Redis 与 MySQL 断线重连状态机，加入指数退避和随机抖动。
6. 完善信号驱动关闭顺序，记录各阶段耗时和未排空数量。
7. 引入 Toxiproxy，仅在测试 Compose 中模拟延迟、超时、断连和 reset。
8. 编写 Runbook，说明每个降级状态的用户表现、指标和恢复动作。

## 参考

- [Toxiproxy](https://github.com/Shopify/toxiproxy)：在测试和 CI 中确定性注入延迟、断连和超时。
- [userver](https://github.com/userver-framework/userver)：参考 deadline、取消、拥塞控制和依赖客户端治理思想。
- 当前项目 [Operations](../Operations.md)、[Runbook](../Runbook.md) 和连接生命周期测试。

## 验收方法

```bash
docker compose -f docker-compose.chaos.yml up -d --build
python3 tests/resilience/DependencyLatencyTest.py
python3 tests/resilience/SlowConsumerTest.py
python3 tests/resilience/GracefulDrainTest.py
ctest --preset asan -L "lifecycle|resilience" --output-on-failure
```

注入 Redis/MySQL 2 秒延迟时，EventLoop 仍能在既有 `/ping` 延迟阈值内响应；依赖请求在配置 deadline 后失败。慢客户端测试持续推送，输出缓冲不得超过硬上限，连接按预期关闭且其他客户端延迟不显著恶化。队列满载时 RSS 不持续增长，拒绝计数与客户端错误数量一致。

优雅退出必须在配置宽限期内结束，退出码为 0，监听端口立即停止接收，已有连接收到关闭事件，日志、数据库和 Redis 线程均完成 join。ASan/UBSan 无泄漏或非法访问。

## 交付物

- 全部资源上限与配置校验
- 输出缓冲高水位和慢客户端策略
- 依赖 deadline、重连、退避与降级策略
- Toxiproxy 测试环境和三类故障测试
- 更新后的 Operations 与 Runbook

## 实施结果

| 能力 | 实现与观测 |
| --- | --- |
| 输出背压 | `HTTPSERVER_OUTPUT_BUFFER_HIGH_WATER_BYTES`（256 KiB）和 `HTTPSERVER_OUTPUT_BUFFER_HARD_LIMIT_BYTES`（1 MiB）限制每连接待写字节。高水位丢弃非关键房间广播；硬上限关闭连接。`/metrics` 提供 high-water、dropped 和 slow-consumer 计数。 |
| 依赖截止与拒绝 | 数据库执行器保持有界队列，任务在 3 秒排队 deadline 后回调稳定 `503`；MySQL 连接/读/写超时和 Redis 连接/命令超时分别配置。指标包含队列深度、拒绝和过期数。 |
| 重连与重试 | Redis 重连采用指数退避加随机抖动；Outbox 重试仅针对未发布记录，并采用有限指数退避和 0-1 秒抖动。 |
| 优雅退出 | `signalfd` 先停止监听，给 WebSocket 发 `1001`，在可配宽限期后依次 join Outbox、数据库执行器、Redis，并记录阶段日志。 |

## 验收记录

```bash
cmake --build --preset debug --parallel
ctest --test-dir build/debug -L 'resilience|lifecycle' --output-on-failure
# 结果：resilience.slow_consumer、resilience.graceful_drain 通过

docker compose -f docker-compose.chaos.yml up -d --build
python3 tests/resilience/DependencyLatencyTest.py 18082 18474
# 结果：dependency_latency_acceptance ping_max_ms=2 dependency_status=503
```

`SlowConsumerTest.py` 使用 64 KiB 硬上限和 96 KiB 静态响应，验证慢连接被关闭且其他 `/ping` 请求正常；`GracefulDrainTest.py` 验证 `SIGTERM` 后监听立即停止、WebSocket 收到 `1001`、进程在两秒测试宽限期内以零状态退出。Toxiproxy 配置仅用于测试，代理 MySQL 与 Redis，并允许通过 `18474` 管理端口注入 toxic。
