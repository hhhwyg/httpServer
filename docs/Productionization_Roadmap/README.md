# 实时通信服务器工程化路线

## 1. 路线目标

本路线用于将当前单机 HTTP/WebSocket 聊天室升级为可公开部署、可水平扩展、可验证可靠性的实时通信服务。完成态不是笼统的“生产级”，而是在一个明确的单机云服务器和双应用实例场景内，具备持久化、跨实例广播、故障恢复、可观测、自动化测试与可复现部署能力。

最终需要用代码、测试和运行记录证明以下事实：

1. 房间、成员和消息不会因为应用进程重启而丢失。
2. 连接到不同应用实例的同房间用户可以实时收发消息。
3. 客户端重试不会产生重复消息，短暂断线后可以补拉遗漏消息。
4. Redis、MySQL、慢客户端和应用重启等故障不会阻塞 EventLoop 或造成无界资源增长。
5. 公网环境通过 HTTPS/WSS 对外提供服务，并具备监控、告警、备份和回滚流程。
6. 性能结论来自固定环境、固定场景、原始结果和错误率，而不是只压测 `/ping` 得出的推测。

## 2. 目标架构

```text
Browser / benchmark clients
            |
       HTTPS / WSS
            |
       Nginx gateway
            |
      +-----+-----+
      |           |
  WebServer-1  WebServer-2
      |           |
      +-----+-----+
            |
    +-------+--------+
    |                |
  MySQL            Redis
durable state   cache/rate-limit/pub-sub/presence
    |                |
    +-------+--------+
            |
 Prometheus / Grafana / Alertmanager
```

MySQL 是用户、房间、成员关系和聊天消息的真相源。Redis 只负责可丢失或可重建的派生状态与实时通知。Nginx 负责 TLS 终止和 WebSocket 反向代理，应用仍专注于 HTTP/WebSocket、业务和依赖治理。

## 3. 模块与依赖

| 顺序 | 模块 | 关键产出 | 前置模块 |
| --- | --- | --- | --- |
| M1 | [业务领域与 API](01_Domain_and_API.md) | 房间成员模型、版本化协议、历史分页 | 当前代码基线 |
| M2 | [消息可靠性](02_Message_Reliability.md) | ACK、幂等、顺序、断线补拉 | M1 |
| M3 | [MySQL 持久化](03_MySQL_Persistence.md) | schema、事务、Repository、迁移 | M1、M2 |
| M4 | [Redis 集成](04_Redis_Integration.md) | 客户端层、限流、缓存、在线状态 | M1、M3 |
| M5 | [多实例与 Outbox](05_Multi_Instance_and_Outbox.md) | Pub/Sub、跨实例广播、可靠发布 | M2、M3、M4 |
| M6 | [可靠性与背压](06_Reliability_and_Backpressure.md) | 超时、限界、降级、故障恢复 | M3、M4、M5 |
| M7 | [可观测性](07_Observability.md) | 指标、日志关联、仪表盘、告警 | M3、M4、M6 |
| M8 | [安全](08_Security.md) | 授权、密钥、输入边界、滥用防护 | M1、M3、M4 |
| M9 | [部署与 CI/CD](09_Deployment_and_CICD.md) | HTTPS/WSS、公网部署、备份、回滚 | M5-M8 |
| M10 | [测试与压测](10_Testing_and_Benchmark.md) | E2E、故障注入、长稳、性能报告 | 贯穿所有模块 |

## 4. 建议执行周期

| 周次 | 工作重点 | 周末必须演示的结果 |
| --- | --- | --- |
| 第 1 周 | M1、M2 | 两个客户端可收到带服务端消息 ID 的 ACK；重复提交只产生一条消息 |
| 第 2 周 | M3 | 重启应用后房间、成员和消息仍存在；历史消息可游标分页 |
| 第 3 周 | M4 | Redis 限流和缓存生效；Redis 故障时服务按设计降级 |
| 第 4 周 | M5 | 两个 WebServer 实例之间完成实时广播；Outbox 失败可重试 |
| 第 5 周 | M6、M7、M8 | 慢客户端和依赖故障不拖死 EventLoop；Grafana 能定位失败原因 |
| 第 6 周 | M9、M10 | 公网 HTTPS/WSS 可访问；完成长稳、故障和多实例性能报告 |

如果时间不足，最低可交付范围是 M1-M5、M9 的单机公网部署以及 M10 的核心验证。不得为了技术栈数量跳过消息持久化、幂等或故障测试。

## 5. 全局完成标准

- 每个模块对应独立 PR，PR 描述包含问题、方案、失败路径、测试命令和回滚方式。
- Debug 与 ASan/UBSan 测试全部通过，新增并发共享状态必须有 TSan 可运行的隔离测试。
- Docker Compose 能启动 Nginx、两个应用实例、MySQL、Redis、Prometheus 和 Grafana。
- 真实浏览器可通过公网域名完成注册、登录、建房、跨实例聊天、刷新页面和历史恢复。
- 连续运行至少 24 小时，无持续 RSS/fd 增长，无 Outbox 永久积压，无未解释的 5xx。
- 发布一个 Git tag，附架构图、演示步骤、故障演练记录和原始压测结果。

## 6. 工作规则

- MySQL 持久化成功之前，不向客户端确认消息已可靠接收。
- Redis 不作为聊天消息的唯一存储；缓存必须可从 MySQL 重建。
- 所有外部依赖调用必须有超时、容量上限、错误指标和关闭流程。
- 所有队列、缓冲区、连接池和重试次数必须有上限。
- 不在 EventLoop 线程执行同步 MySQL、同步 Redis、DNS 或磁盘阻塞操作。
- 每修复一个并发、协议或故障恢复问题，先增加可重复失败的测试，再修改实现。
- 简历只写已经完成并由本路线验收通过的能力和数字。

## 7. 当前实施进度

更新时间：2026-09-06。

- M1 已完成：领域对象与 Repository 接口、内存实现、线程安全应用服务、HTTP API v1、消息 ID 游标历史、WebSocket v1 envelope、前端迁移和业务 E2E 均已落地。
- M2 已完成：原子 `AppendOrGet` 幂等路径、ACK 可靠边界、同键重试不重复广播、`after` 增量恢复、客户端有限指数退避，以及并发/断线 E2E 已落地；语义详见 [消息交付语义](../Message_Delivery_Semantics.md)。
- M1/M2/M3 边界：当前历史和幂等记录仅在进程生命周期内有效；进程重启恢复、MySQL 事务提交和唯一约束裁决等待 M3 持久化验收。
- 兼容策略：旧 `/room/*` 和旧 WebSocket `join/chat` 暂时保留，旧 `join` 会自动加入业务房间；新客户端只使用 v1 接口。
- 当前验证：Debug 与 ASan/UBSan 全量测试均为 26/26，TSan 领域/协议测试 2/2，浏览器加载无脚本错误。
