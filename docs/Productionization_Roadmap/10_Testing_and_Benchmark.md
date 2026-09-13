# M10：测试、故障演练与性能验证

> 完成状态：测试框架、故障矩阵和业务 WebSocket 基准已实现；目标服务器上的 30 分钟对照、24 小时长稳与 release artifact 尚未执行，因此 M10 尚未最终关闭。

## 目标

建立覆盖正确性、可靠性、资源安全和容量边界的验证体系。最终报告要回答服务在指定机器和场景下能维持多少连接、多少消息吞吐、怎样的尾延迟和错误率，以及 Redis/MySQL 或实例故障时用户看到什么。

## 测试分层

| 层级 | 运行频率 | 重点 |
| --- | --- | --- |
| 单元测试 | 每次提交 | 协议、领域规则、游标、幂等、Lua 结果解析、退避计算 |
| 组件测试 | 每次 PR | Repository、Redis client、Outbox、SessionRegistry、buffer 高水位 |
| 集成测试 | 每次 PR | 真 MySQL、真 Redis、真 socket、服务重启 |
| E2E | 主分支和发布 | 注册到跨实例聊天、断线恢复、浏览器主流程 |
| Sanitizer | 每次 PR/定时 | ASan/UBSan；可隔离模块运行 TSan |
| 故障测试 | 主分支和发布 | 延迟、断连、重启、队列满、慢客户端 |
| 性能与长稳 | 发布前 | HTTP、长连接、广播、持久化、24 小时资源曲线 |

## 核心场景

正确性场景包括并发重复发送、ACK 丢失、历史跨页、非法成员、WebSocket 分片、服务端关闭和 schema 升级。可靠性场景包括 MySQL/Redis 延迟与断开、Outbox 发布窗口崩溃、单实例退出、Nginx 重载、慢读客户端和磁盘空间压力。

性能场景不能只测 `/ping`：

1. HTTP `/ping` 作为 transport 回归基线。
2. 登录和历史查询分别测 MySQL 冷路径与 Redis 缓存命中路径。
3. 1 个发送者向同房间 100、500、1000 个接收者广播。
4. 多房间并发广播，避免单热点房间代表全部情况。
5. 两实例跨 Redis 广播，与单实例本地广播对照。
6. 客户端断线重连并补拉 1000 条消息。
7. 长连接只保活场景测最大稳定连接数和每连接内存。

## 指标与实验规则

每轮记录 Git commit、构建类型、后端、机器 CPU/内存、内核、fd 上限、应用线程数、连接数、房间数、消息大小、广播 fanout、持续时间、客户端位置、QPS/消息每秒、成功率、p50/p95/p99、CPU、RSS、fd、上下文切换、Redis/MySQL 延迟和 Outbox 积压。

客户端必须验证响应和消息 ID，只有完整正确结果计入成功。服务端和压测端尽量分离；若同机运行，报告明确资源竞争。至少运行三次，报告中位数和波动，不选择单次峰值。优化必须保留前后对照和原始结果。

## 实施计划

1. 扩展现有 C++ benchmark，使其支持 WebSocket 握手、房间加入、发送、接收校验和延迟直方图。
2. 为广播创建可配置发送者、接收者、房间数、消息大小和持续时间的客户端。
3. 在测试结果中记录每条遗漏、重复、乱序和协议错误。
4. 将 Toxiproxy 场景纳入单独 resilience job，避免普通 PR 流水线不稳定。
5. 使用 `/proc`、Prometheus 和客户端结果交叉验证 CPU、RSS、fd 和延迟。
6. 运行 30 分钟预检和最终 24 小时长稳，按固定间隔保存指标快照。
7. 使用 `perf record/report` 或火焰图定位热点；没有 profile 证据不做复杂底层优化。
8. 更新 `docs/Performance_Analysis.md`，按场景陈述结论与局限。

## 参考

- 当前项目 [Benchmark](../Benchmark.md)、[Performance Analysis](../Performance_Analysis.md) 与 `tests/benchmark/http_epoll_benchmark.cpp`。
- [Drogon benchmark methodology](https://github.com/drogonframework/drogon/wiki/ENG-14-Benchmarks)：参考环境和场景记录方式，不直接比较不同机器数字。
- [Toxiproxy](https://github.com/Shopify/toxiproxy)：确定性网络故障注入。
- [Prometheus instrumentation practices](https://prometheus.io/docs/practices/instrumentation/)：在线服务请求、错误和延迟指标。

## 验收方法

发布前执行统一脚本：

```bash
scripts/validate_linux.sh debug
scripts/validate_linux.sh asan
tests/benchmark/run_http_benchmark.sh https://127.0.0.1/ping
tests/benchmark/run_websocket_benchmark.sh deploy/benchmark/scenario-single-instance.env
tests/benchmark/run_websocket_benchmark.sh deploy/benchmark/scenario-cross-instance.env
tests/benchmark/run_soak_test.sh deploy/benchmark/scenario-soak-24h.env
tests/resilience/run_fault_matrix.sh
```

双实例压测前先以 `set -a; source deploy/benchmark/scenario-cross-instance.env; set +a` 加载场景，再启动 `docker-compose.scale.yml`；这样压测专用消息阈值会透传到两个应用实例。场景文件不包含 JWT、MySQL 等密钥，密钥仍须通过部署环境单独提供。

完成标准不是预设一个虚构 QPS，而是：所有成功率和延迟均有原始数据；零遗漏/重复要求经过客户端消息 ID 校验；24 小时内 RSS、fd 和线程数没有持续单调增长；故障矩阵每一项都有预期用户结果、实际结果、恢复时间和对应指标；同版本单实例与双实例结果可复现。

简历中的连接数、消息吞吐、p99、错误率和运行时长必须逐项能定位到某个结果文件和 Git commit。

## 交付物

- WebSocket 业务压测客户端与场景文件
- 故障矩阵自动化脚本
- 24 小时长稳结果和资源曲线
- 单实例/双实例对照报告
- 与 Git tag 对应的原始结果 release artifact

## 实施结果与验收

2026-09-12 补验：压测器修复远端 URL/TLS、失败退出、长时客户端内存与资源采样；17 项回归通过。浏览器回归覆盖桌面/移动布局、中文输入、草稿、断线恢复、ACK 丢失、订阅重试与跨账号隔离，已接入 CI。当前业务压测仍是单消息在途的闭环多房间轮询，不代表独立发送者并发容量；还需补充该负载及目标机长时证据。详见 [本轮复验记录](Acceptance_2026-09-12.md)。

`scripts/validate_linux.sh` 继续提供 Debug/ASan/TSan 构建入口；`tests/resilience/run_fault_matrix.sh` 聚合慢客户端、优雅排空和 shutdown，Toxiproxy 通过 `HTTPSERVER_RUN_TOXIPROXY=1` 显式启用。`tests/benchmark/run_websocket_benchmark.py` 验证真实 WebSocket upgrade 和 masked ping/pong；业务模式由 `run_chat_benchmark.py` 创建 V1 房间、加入可配置数量的接收者，并逐条校验 ACK 与所有 `clientMessageId`。`run_websocket_benchmark.sh` 按 `deploy/benchmark/*.env` 保存原始结果。`run_soak_test.sh` 需要 `HTTPSERVER_ALLOW_LONG_SOAK=1`，避免误执行 24 小时任务。

业务压测器现支持房间数、每房间 fanout、消息大小、真实 API/fixture 认证、服务 PID 资源采样，并输出 ACK、预期/实际投递、遗漏、重复、乱序、协议错误及 p50/p95/p99。跨实例场景使用独立的发送端口和接收端口，将发送者固定在实例 A、接收者固定在实例 B，避免仅凭场景名称误判 Redis 跨节点路径已被覆盖。场景包装脚本同时记录 Git SHA、主机、内核、CPU、内存、fd 上限、后端、服务线程和客户端位置。`ReconnectRecoveryTest.py` 可通过 `HTTPSERVER_RECOVERY_MESSAGE_COUNT=1000` 验证 10 页完整补拉。

本机 Release/epoll 的短时边界结果为：100、500、1000 fanout 分别完成 18,600、30,500、35,000 次已校验投递，全部零遗漏/重复/乱序，p99 分别为 7.708ms、25.930ms、43.165ms；4 房间各 25 fanout、1024B 消息完成 30,575 次投递，p99 为 2.341ms。另以 Compose 双实例执行强制跨节点短测，发送者固定实例 A、20 个接收连接固定实例 B，90/90 次投递且遗漏、重复、乱序和协议错误均为 0。窗口仅 1-2 秒且客户端同机，只用于验收工具和 fanout 正确性。发布前仍必须在目标机执行三轮 30 分钟对照与 24 小时长稳，并生成 Git tag 对应 artifact。
