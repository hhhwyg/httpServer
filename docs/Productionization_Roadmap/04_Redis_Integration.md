# M4：Redis 集成

## 完成状态

已完成。实现位于 `src/redis/RedisClient.cpp`：生产构建通过 hiredis 连接 Redis；缺少 hiredis 的开发构建显式禁用 Redis 并保留本地限流回退，不会悄然链接错误依赖。

| 能力 | 实现与边界 | 验收结果 |
| --- | --- | --- |
| 客户端与关闭 | 命令连接和订阅连接各自归专用线程；有界队列、连接/命令超时、可中断指数退避和最大重连次数 | `docker stop -t 4` 正常退出，退出码为 0 |
| 分布式限流 | Lua 原子 `INCR`/`PEXPIRE`；登录、建房和发消息均通过语义化接口调用；Redis 不可用时退回本地限制 | 双实例共享 10 次阈值，100 并发中恰好 10 次放行 |
| Cache Aside | 房间和最近消息以异步 best-effort 回填，读未命中回源；发送消息后失效相关键；指标暴露 hit/miss/error/队列深度 | 命中、删除键、回源并再次回填均已验证 |
| Presence 与 Pub/Sub | 每实例 10 秒续期、30 秒 TTL；WebSocket 建连登记、断连删除；订阅连接为 M5 暴露线程边界明确的回调 | Redis 停止/恢复测试验证 presence 清理与重新连接 |
| 可测试性 | `RedisService` 依赖 `RedisClient` 抽象，`SetClientForTesting` 可安装 fake；fake 覆盖缓存、限流和订阅回调 | 含 hiredis 的 Docker 构建编译 `RedisClientTest` |

缓存写入和失效不等待 Redis 返回，队列满时计入 rejected 指标并继续由 MySQL 提供权威数据；Lua 限流和缓存读取才等待受命令超时约束的结果。任何已发送命令发生 I/O 错误时都不重放，以免产生未知副作用。

## 目标

引入 Redis 解决多实例下本地状态不共享的问题，而不是把它作为技术栈装饰。第一版职责限定为分布式限流、可重建缓存、在线状态和 Pub/Sub 通道；聊天消息的可靠存储仍由 MySQL 负责。

## 客户端架构

使用 hiredis，并封装 `RedisClient` 接口，使业务层不出现 `redisReply` 或命令字符串。hiredis 异步上下文不是线程安全的，因此禁止多个 EventLoop 无锁共享同一个 context。

建议分两步实施：

1. 第一版建立专用 Redis I/O 线程，线程拥有命令连接池和独立订阅连接；EventLoop 通过有界任务队列提交命令，完成回调使用 `queueInLoop` 返回原线程。
2. 第一版稳定后，再实现 hiredis 与项目 Poller 的异步 adapter，并用基准数据决定是否替换专用线程方案。

订阅连接必须专用，不能与 GET/SET/限流命令复用。客户端包含连接状态机、连接/命令超时、指数退避重连、最大重试次数、关闭排空和错误分类。

## Redis 数据设计

| key | 类型 | TTL | 用途 |
| --- | --- | --- | --- |
| `chat:ratelimit:{scope}:{identity}:{window}` | String | 窗口后自动过期 | 登录、建房、发消息限流 |
| `chat:room:{roomId}` | Hash/String | 5 分钟 | 房间元数据缓存 |
| `chat:room:{roomId}:recent` | List | 10 分钟 | 最近 50 条消息缓存 |
| `chat:presence:{userId}:{instanceId}` | String | 30 秒 | 用户在某实例在线 |
| `chat:instance:{instanceId}` | String | 30 秒 | 实例存活心跳 |
| `chat:events` | Pub/Sub channel | 无 | 跨实例实时事件 |

限流使用 Lua 将计数与过期设置作为原子操作。缓存采用 Cache Aside：读未命中访问 MySQL并回填；写事务成功后删除相关缓存。缓存失败时回源 MySQL，不能把 Redis 故障转换为全部业务不可用。

在线状态使用带 TTL 的实例级 key，每 10 秒续期；WebSocket 断开时主动删除，异常退出则依赖 TTL 清理。在线状态仅用于展示，不作为授权依据。

## 实施计划

1. 增加 hiredis CMake 依赖和 `RedisConfig`，校验地址、凭据、超时、队列容量。
2. 定义 `RedisClient`、`RedisSubscriber` 和可注入的 fake 实现。
3. 完成 Redis I/O 线程、有界请求队列、回调线程归属和关闭流程。
4. 使用 Lua 替换当前进程内认证限流，并增加发消息限流。
5. 为房间信息和近期消息实现 Cache Aside，记录 hit/miss/error。
6. 实现 presence 心跳、断开删除和过期清理。
7. 增加独立 Pub/Sub 订阅连接，为 M5 提供事件回调接口。
8. 验证 Redis 重启、认证失败、超时和网络断开后的重连行为。

## 参考

- [hiredis](https://github.com/redis/hiredis)：异步 API、线程安全边界、连接回调和 adapter 接口。
- [Redis 限流模式](https://redis.io/docs/latest/commands/incr/)：`INCR` 与过期设置的竞态及 Lua 修复方式。
- [Redis Pub/Sub](https://redis.io/docs/latest/develop/pubsub/)：订阅模式与至多一次投递语义。
- [Drogon](https://github.com/drogonframework/drogon) 和 [userver](https://github.com/userver-framework/userver)：参考异步依赖客户端对业务暴露的接口，不直接引入其实现。

## 验收方法

```bash
docker compose -f docker-compose.test.yml up -d redis
docker build --target build -f Dockerfile.deploy -t httpserver-m4-buildcheck .

# 以两个使用同一 Redis 的实例启动服务后执行：
HTTPSERVER_REDIS_HOST=127.0.0.1 HTTPSERVER_REDIS_PORT=36379 \
  python3 tests/integration/RedisRateLimitTest.py external 18314 18315
python3 tests/integration/RedisCacheTest.py 18320
HTTPSERVER_REDIS_HOST=127.0.0.1 HTTPSERVER_REDIS_PORT=36379 \
  python3 tests/integration/RedisFailureRecoveryTest.py external 18319
python3 tests/integration/RedisAuthenticationFailureTest.py external 18321
```

验收包括：两个应用实例对同一用户共享限流计数；100 个并发请求不会突破配置上限；缓存命中降低 MySQL 查询数；删除缓存后可正确回源；Redis 停止时 EventLoop 延迟不出现依赖超时之外的长时间停顿；Redis 恢复后自动重连；关闭服务时 Redis 线程可在规定时间退出。

使用 TSan 单测验证任务队列和关闭竞态，使用指标确认队列长度有界。不得以“Redis 命令返回成功”代替业务和故障验收。

本次完成验收记录：`redis_rate_limit_acceptance shared_limit=10 concurrent=100`、`redis_cache_acceptance cache_hit=1 cache_miss=1 invalidation_refill=1`、`redis_failure_recovery_acceptance fallback_lt=2s reconnected=1 presence=clean`、`redis_auth_failure_acceptance fallback_lt=2s`。

## 交付物

- Redis 配置、客户端抽象和 fake
- Redis I/O 线程及订阅连接
- Lua 分布式限流
- 房间/近期消息缓存与 presence
- Redis 正常、超时、重启和关闭测试
