# httpServer 重构路线图

## 1. 目标与边界

本项目的定位是一个用于展示 Linux C++ 网络编程能力的 HTTP/WebSocket 服务器，而不是在没有运维、容量评估和长期线上验证的前提下宣称“生产可直接部署”的通用 Web 服务。重构目标是让它具备**工业项目应有的工程方法与关键质量属性**：可构建、可测试、可观测、可配置、可维护，并在一个定义清楚的部署场景内可靠运行。

完成后，应能向面试官清楚说明：

- 为什么选择 Reactor + `io_uring`，线程和连接分别由谁拥有。
- 一个连接从 `accept`、事件注册、读写、超时到关闭的完整生命周期。
- HTTP、WebSocket、鉴权和聊天室业务如何分层，为什么不会彼此污染。
- 如何通过单元测试、集成测试、Sanitizer、压测和 CI 防止回归。
- 具体解决过哪些资源、并发和安全问题，以及量化验证方式。

本轮不追求实现 HTTP/2、TLS 终止、分布式聊天室、ORM 或复杂微服务。它们会显著扩大范围，也会掩盖网络服务核心设计是否扎实。

## 2. 当前基线

仓库已经具备 C++17、主从 `EventLoop`、`io_uring`、静态文件、HTTP 状态机、WebSocket、MySQL、JWT 和异步日志等学习性实现。它的优点是组件覆盖面广；主要问题是模块边界、可靠性验证和安全边界尚未收紧。

以下事实是后续计划的输入，而不是假设：

| 类别 | 当前情况 | 风险/影响 |
| --- | --- | --- |
| 构建 | 根目录 CMake 使用全局编译选项和 `GLOB_RECURSE`，测试未接入 CTest | 构建不透明，测试无法在 CI 自动执行 |
| I/O 生命周期 | `IoUring` 用 `fd` 编码完成事件，按 `fd` 查表回调 | fd 被关闭并复用时，旧 CQE 可能误投递给新连接 |
| 连接资源 | `HttpData` 已开始修复对象池复用和 fd 关闭 | 仍需用 ASan/LSan 和高频断连验证完整生命周期 |
| HTTP/聊天协议 | `path_` 没有在 URI 解析中赋值；前后端 WebSocket 字段不一致 | 房间 API 和聊天流程不能作为可信演示功能 |
| 安全 | JWT 密钥硬编码；密码明文存储；SQL 字符串拼接 | 不能暴露到公网，不能作为完成态功能宣传 |
| 数据库 | 连接池初始化在 `Main.cpp` 中被注释 | 注册和登录默认不可用且配置不可交付 |
| 工程交付 | 没有 CTest/CI、运行配置、容器化、基准报告和统一文档 | 别人无法稳定复现项目能力 |

## 3. 完成标准

“重构完成”不是代码看起来更整齐，而是同时满足下列条件：

1. 在干净的 Ubuntu 环境中，按文档命令可构建、运行、测试和停止服务。
2. CTest 覆盖核心组件和 HTTP/WebSocket 关键路径；CI 对每个合并请求执行格式、构建和测试。
3. 连接关闭、超时、I/O 取消和 fd 复用有明确所有权模型，ASan/LSan/UBSan 及压力断连测试无已知泄漏或未定义行为。
4. 所有外部输入有大小、格式和状态限制；密码使用成熟的带盐哈希方案；SQL 使用预处理/转义接口；密钥和数据库凭据不进入源码。
5. 日志、配置、健康检查和基础指标足以定位一次失败请求或连接异常。
6. 发布一个可复现的压测报告。简历只使用报告中的环境、命令、指标和结论，不估算或虚构 QPS、延迟、连接数。

## 4. 目标架构

重构后遵循依赖只能由外向内的方向：

```text
app (启动、信号、依赖装配)
  -> transport (EventLoop / Channel / Poller / socket)
  -> protocol (HTTP parser / response / WebSocket codec)
  -> application (Router / AuthService / ChatService)
  -> domain (User / Room / Message)
  -> infrastructure (MySQL repository / Logger / Config / Metrics)
```

`transport` 不知道 URI、JWT 或 SQL；`protocol` 不直接调用 MySQL；`application` 只依赖抽象接口，例如 `UserRepository`。这样既能用假实现测试业务，也能独立替换 `io_uring` 或数据库实现。

建议的最终目录如下。不要一开始机械移动全部文件，必须在对应模块已有测试保护后再迁移。

```text
include/httpserver/       # 对外可见头文件，按模块分目录
src/
  app/                    # main、ServerApp、配置装配
  transport/              # socket、Channel、EventLoop、Poller、Timer
  protocol/http/          # Request、Response、Parser、静态文件
  protocol/websocket/     # 握手、帧编解码、会话
  application/            # Router、认证和聊天室用例
  domain/                 # 用户、房间、消息等纯业务对象
  infrastructure/         # MySQL、日志、配置、指标实现
tests/
  unit/                   # 无网络、无数据库依赖的快速测试
  integration/            # 真 socket / 真 HTTP / 容器化数据库测试
  e2e/                    # 启动服务后的黑盒场景
  benchmark/              # 可复现的 wrk/hey 脚本与结果
docs/                     # 架构、设计决策、运行和压测报告
deploy/                   # Dockerfile、compose、示例 systemd 单元
cmake/                    # CMake 辅助模块
```

## 5. 执行原则

- 先建立测试和可复现基线，再改变行为；每个阶段均能独立构建和运行。
- 一次 Pull Request 只解决一个问题。禁止“格式化全仓库 + 移动文件 + 改业务”混在同一次提交。
- 资源使用 RAII，生命周期由对象关系表达，不依赖约定或注释。
- 外部数据默认不可信。长度、编码、状态和权限均在边界校验。
- 先测量，再优化。`io_uring`、对象池和零拷贝都必须通过 profile 或基准数据证明其收益。
- 新接口先写文档和测试，再替换旧调用点；删除旧实现前保持至少一条端到端回归测试。

## 6. 分阶段计划

### Phase 0：冻结基线与工程脚手架

**状态：已完成。** Debug 与 ASan/UBSan 构建均已在 WSL Ubuntu 中通过，`base.object_pool` CTest 在两套构建下均通过。存量编译告警已记录，暂不启用 `-Werror`，留待 Phase 4 以独立改动清理。

**目标：** 让任何人都能在同一环境复现当前行为和后续变化。

- 建立 `.gitignore`，忽略 `build/`、日志、覆盖率、编译数据库和本地密钥文件；保留必要的示例配置。
- 改用 target-based Modern CMake：移除全局 `CMAKE_CXX_FLAGS` 和 `GLOB_RECURSE`，显式列出源文件，使用 `target_include_directories`、`target_compile_options`、`target_link_libraries`。
- 引入 `CMakePresets.json`：至少提供 `debug`、`release`、`asan`、`tsan` 四套预设；C++ 标准固定为 C++17。
- 接入 CTest 和 GoogleTest/Catch2 之一；先把现有 `ObjectPoolTest`、日志测试改为自动测试目标。
- 添加 clang-format、clang-tidy 的最小配置及 GitHub Actions。CI 最小流水线为：格式检查、Debug 构建、CTest、ASan/UBSan 构建与测试。
- 建立 `docker compose` 的开发环境，至少包含编译镜像和 MySQL；容器只用于复现环境，不替代 Linux 主机压测。

**验收：** 新机器执行 `cmake --preset debug`、`cmake --build --preset debug`、`ctest --preset debug` 可通过；CI 对每次提交运行同样命令。

**产出文档：** `docs/Build_and_Test.md`、`docs/Development_Guide.md`、`docs/CI.md`。

**学习重点：** CMake target 的依赖传播、Debug/Release/Sanitizer 差异、测试金字塔、可复现构建。

### Phase 1：先修正连接生命周期和 I/O 正确性

**目标：** 消除最危险的 fd、对象和异步操作生命周期问题。这是整个项目的最高优先级。

- 为 `HttpData`、`Channel`、`TimerNode`、`EventLoop` 标注所有权：谁创建、谁持有、谁销毁、在哪个线程销毁。
- 将“关闭连接”收敛成幂等的单一状态转换：停止接收新事件，取消/排空未完成 I/O，解除计时器和 Channel 关联，最后关闭 fd。重复关闭应安全。
- 重做 `io_uring` 的 `user_data`。不能只编码 fd；应携带稳定操作上下文（操作类型、连接 generation/token、持有连接生命周期的对象），并在处理 CQE 时校验 generation，忽略过期完成事件。
- 明确每个连接同时允许的读、写、poll 请求数量，并在状态机中禁止重复提交。处理 `-ECANCELED`、`-EAGAIN`、`-EINTR`、对端半关闭和短读短写。
- 将 fd 上限和 `io_uring` 队列容量做成配置；替换固定的 `MAXFDS` 大数组，或在容量上限、索引检查和内存占用间做出明确设计选择。
- 保留已修复的 `ObjectPool` 生命周期测试，并补充多线程 acquire/release、缓存上限、构造异常和 `HttpData` 释放 fd 的测试。若压测没有证明收益，允许删除 `HttpData` 对象池，优先选择正确且更容易推理的普通 `shared_ptr` 生命周期。
- 在 ASan/LSan/UBSan 下执行“快速连接/断开 + fd 重用 + 超时”的集成测试；TSan 仅用于无 `io_uring` 不兼容项的并发单元测试。

**验收：** 反复连接、发送不完整请求、立即断开、超时和 fd 重用不会崩溃、串线或泄漏；Sanitizer 流水线通过；关闭流程有状态图和测试覆盖。

**产出文档：** `docs/Connection_Lifecycle.md`、`docs/IoUring_Design.md`、`docs/ADR/0001-io-uring-operation-identity.md`。

**学习重点：** RAII、`shared_ptr`/`weak_ptr` 环、异步取消语义、ABA/fd reuse 问题、内核完成队列。

### Phase 2：协议实现与功能闭环

**目标：** 将 HTTP、WebSocket 和聊天室变成经过黑盒测试的完整功能，而不是分散在 `HttpData` 中的条件分支。

- 拆分 `HttpData`：`HttpRequestParser` 只解析字节流，`HttpResponseWriter` 只负责输出，`Router` 只匹配路由，连接对象只协调 I/O 状态。
- 使用明确的 request/response 数据结构，定义请求行、Header 大小、Body 大小、Header 数量、URI 长度、空闲超时等上限，并在超限时返回正确的 4xx/5xx。
- 修复 URI 解析后 `path_`、`query_` 的赋值和规范化；禁止 `..` 路径穿越，静态文件用受限根目录解析并处理 `open`/`sendfile` 错误与部分发送。
- 定义版本化的 WebSocket JSON 协议。前端和后端统一 `type`、`roomId`、`content`、错误码和服务端事件字段；`roomId` 的类型固定，不能一端数值一端字符串。
- 完整处理 WebSocket opcode、mask、分片、ping/pong、close、帧长上限和非法帧，不将未校验数据直接广播。
- 将聊天室从 `HttpData` 移到 `ChatService`/`ChatRoom`，定义加入、离开、广播、断线清理和房间权限的并发策略。
- 建立黑盒测试：`curl`/自写测试客户端覆盖静态文件、Keep-Alive、坏请求、注册登录、JWT、握手、join/chat/leave 和断线清理。

**验收：** README 中列出的每个 API 都有对应测试；浏览器前端可完成登录、创建/加入房间、聊天和断线重连；错误输入不会让事件循环失效。

**产出文档：** `docs/HTTP_Protocol.md`、`docs/WebSocket_Protocol.md`、`docs/API.md`、`docs/Static_File_Security.md`。

**学习重点：** 流式协议解析、状态机、RFC 的边界条件、契约测试、输入校验。

### Phase 3：安全、配置与数据访问

**目标：** 将演示代码改为不会以明文凭据、SQL 拼接或硬编码密钥运行的服务。

- 增加 `Config` 模块，支持配置文件和环境变量覆盖；提供 `.env.example`/`config.example.toml`，绝不提交真实密码、JWT 密钥或私钥。
- 以成熟密码哈希库实现 Argon2id 或 bcrypt。数据库只保存哈希；登录使用常量时间校验接口；定义注册限制和错误信息，避免用户枚举。
- 以 MySQL prepared statement 或可靠转义 API 替换拼接 SQL；将 SQL 放入 repository 层；每个数据库异常有稳定的应用层错误码和日志。
- 将 JWT 的算法、密钥、过期时间、issuer/audience 设为配置；校验算法白名单、签发时间、过期时间和 token 长度。后续密钥轮换可通过 `kid` 设计预留。
- MySQL 连接池的启动、健康检查、超时和关闭纳入应用生命周期；数据库未配置时，服务要清楚地降级或拒绝相关路由，而非在请求时崩溃。
- 对登录和 WebSocket 握手增加 IP/用户维度限流的最小实现；日志中不记录密码、token 和完整敏感请求体。

**验收：** 安全扫描和测试确认无明文密码、硬编码密钥、可复现 SQL 注入；使用环境变量可启动真实数据库；认证失败不会泄露敏感信息。

**产出文档：** `docs/Configuration.md`、`docs/Security_Model.md`、`docs/Database.md`、`docs/Threat_Model.md`。

**学习重点：** 密码哈希和盐、认证与授权边界、依赖注入、最小权限、OWASP 输入处理。

### Phase 4：模块边界与代码质量

**目标：** 在 Phase 1-3 的测试保护下，完成可维护的模块拆分和风格统一。

- 按“目标架构”逐模块迁移，先迁移最稳定的 `base`/日志和 `transport`，再迁移协议、业务和基础设施；每次迁移只改路径、命名空间和 include，不混入逻辑修改。
- 所有公开接口放入 `include/httpserver`；实现细节留在 `src`。添加 `httpserver::` 命名空间，清除头文件中的 `using namespace std`。
- 统一命名、错误处理、头文件顺序、常量和注释规范。注释只解释约束和原因，不复述代码。
- 用 `std::expected` 需要 C++23，因此本项目 C++17 阶段选择统一的 `Status`/`Result<T>` 或异常策略，并在模块边界一致使用。
- 删除未使用的旧 `epoll`/线程池/脚本，或明确它们作为独立实验目标保存；删除前检查编译依赖和 README 引用。
- 在保持行为不变的 PR 中减少 `HttpData` 的职责，使其最终成为连接会话，而不是 HTTP、文件、鉴权、聊天室和数据库的总控制器。

**验收：** 依赖方向符合第 4 节；clang-tidy 的约定检查无新增告警；核心模块可脱离 `main` 被单测链接。

**产出文档：** `docs/Architecture.md`、`docs/Coding_Style.md`、`docs/Module_Guide.md`、`docs/ADR/0002-module-boundaries.md`。

**学习重点：** 单一职责、依赖倒置、接口隔离、重构时的行为保持、ADR（Architecture Decision Record）。

### Phase 5：可观测性、可靠性与部署

**目标：** 出问题时能定位，启动和停止时行为可预测。

- 日志改为结构化字段或至少统一键值格式，包含时间、级别、线程、连接标识、请求标识、路由、状态码、延迟和错误码；实现日志轮转、保留和背压策略。
- 添加 `/healthz`、`/readyz` 和 `/metrics`；最小指标包括当前连接数、请求数、各状态码、活跃房间数、事件循环延迟、I/O 错误和数据库池使用量。
- 实现 SIGTERM/SIGINT 优雅退出：停止接收新连接，等待有限时间内的在途请求，关闭线程、I/O ring、数据库和日志。
- 编写 Dockerfile、docker-compose、非 root 运行用户、资源限制和示例 systemd 单元；为生产环境提供反向代理/TLS 终止说明，不在本项目中手写 TLS 协议栈。
- 加入故障演练：数据库不可用、慢客户端、磁盘写日志失败、连接洪泛、进程收到终止信号。

**验收：** 用日志和指标可回答“一次 5xx 的路由、错误原因和延迟”；终止测试无连接泄漏；容器和 systemd 示例均能启动。

**产出文档：** `docs/Observability.md`、`docs/Operations.md`、`docs/Deployment.md`、`docs/Runbook.md`。

**学习重点：** SLI/SLO、优雅关闭、日志背压、健康检查与可操作性。

### Phase 6：性能验证与发布

**目标：** 用可复现数据证明设计，而不是以“高性能”作为没有依据的标签。

- 固定机器、内核、CPU、内存、编译器、构建类型、文件描述符上限和压测工具版本；记录到报告。
- 分别测静态文件、短连接 HTTP、Keep-Alive HTTP、WebSocket 广播和连接风暴；同时记录 QPS、p50/p95/p99 延迟、错误率、CPU、内存、上下文切换和打开 fd 数。
- 用 `perf`、火焰图或 `strace` 定位一个实际热点，再做一个有对照组的优化。例如比较普通读写与 `sendfile`，或比较对象池启用/禁用，而不是预先假定优化有效。
- 执行故障和长稳测试，记录持续时间、最大连接数、错误数、内存曲线和结果。
- 打 tag、生成 release notes，更新 README 的功能表和限制；只声明已经验证的能力和已知限制。

**验收：** `tests/benchmark` 的脚本可复跑，报告包含原始命令和环境；优化前后有对照；没有未经数据证实的性能宣传。

**产出文档：** `docs/Benchmark.md`、`docs/Performance_Analysis.md`、`docs/Release_Checklist.md`、`CHANGELOG.md`。

**学习重点：** 基准设计、尾延迟、性能剖析、容量边界、因果验证。

## 7. 推荐执行顺序与里程碑

| 里程碑 | 前置条件 | 可演示成果 | 完成判断 |
| --- | --- | --- | --- |
| M1：工程可复现 | Phase 0 | 一条命令构建、测试、启动 | CI 绿灯，文档可在新环境复现 |
| M2：连接可靠 | M1 | 高频连断和超时仍稳定 | Sanitizer 和生命周期测试通过 |
| M3：功能闭环 | M2 | 浏览器聊天室端到端可用 | HTTP/WS 黑盒测试通过 |
| M4：安全可配置 | M3 | 容器化数据库下完成注册登录 | 没有明文密码、硬编码密钥、拼接 SQL |
| M5：工程可运维 | M4 | 指标、日志、优雅退出、部署示例 | 故障演练和退出测试通过 |
| M6：可量化发布 | M5 | 可复现性能报告和 release | 基准报告、tag、README 一致 |

建议每周只完成一个小主题，例如“先完成 CTest，再做 Sanitizer”，不要按目录一次性大迁移。每个主题单独建分支、开 PR、做一次自我 Code Review。Phase 0 和 Phase 1 必须先于其它所有重构；Phase 2 与 Phase 3 可在稳定的连接层之上交替推进；Phase 4 的目录迁移放在行为稳定之后。

## 8. 文档交付清单

每份文档使用相同模板：背景、目标、非目标、设计、关键约束、失败场景、测试/验证命令、权衡和后续工作。避免只写“使用了某技术”，必须说明为什么、替代方案和验证结果。

| 文档 | 写作时机 | 应回答的问题 |
| --- | --- | --- |
| `Architecture.md` | M4 前 | 请求经过哪些层？每层能依赖什么？ |
| `Connection_Lifecycle.md` | M2 | 谁拥有 fd？为什么不会有 stale CQE/fd reuse？ |
| `IoUring_Design.md` | M2 | SQE/CQE、取消和并发请求如何工作？ |
| `HTTP_Protocol.md` | M3 | parser 如何处理半包、超限和错误请求？ |
| `WebSocket_Protocol.md` | M3 | JSON 契约、帧限制、断线语义是什么？ |
| `Security_Model.md` | M4 | 密钥、密码、SQL、限流和敏感日志如何处理？ |
| `Observability.md` | M5 | 看到 5xx 或延迟升高时如何定位？ |
| `Benchmark.md` | M6 | 环境、命令、原始结果、结论和局限是什么？ |
| `ADR/*.md` | 关键决策发生时 | 为什么选这个方案而不是另一个？ |

README 只保留项目概览、快速开始、能力边界和文档链接；设计细节进入 `docs/`，这样招聘者先快速理解，深入阅读者也能验证设计。

## 9. 测试策略

| 层级 | 关注点 | 示例 |
| --- | --- | --- |
| 单元测试 | 纯逻辑、无需 socket/数据库 | HTTP parser、JWT claims、对象池、路由、帧编解码 |
| 组件测试 | 一个模块及其直接依赖 | Timer + Channel、连接关闭状态机、MySQL repository |
| 集成测试 | 真 socket 和服务进程 | Keep-Alive、部分写、无效 WebSocket、超时、重连 |
| 端到端测试 | 前后端和数据库协作 | 注册、登录、入房、聊天、退出 |
| 非功能测试 | 资源、并发、性能、故障 | Sanitizer、fd 泄漏、限流、长稳和压测 |

关键回归场景至少包括：客户端在请求中途断开、同一 fd 数值被内核复用、慢读客户端、畸形/超大 HTTP 请求、WebSocket 非法 mask/长度、数据库连接失败、JWT 过期、优雅退出期间的新请求。每发现一个线上级 bug，先写能复现它的测试，再修代码。

## 10. 提交、分支与 Code Review 规范

- 分支命名：`feat/ctest`、`fix/connection-lifecycle`、`refactor/http-parser`、`docs/benchmark-report`。
- 提交格式：`type(scope): summary`，例如 `fix(uring): ignore stale completion events`。
- 一个 PR 描述必须含：问题、设计选择、测试命令、风险和回滚方式。性能 PR 还必须附优化前后数据。
- 任何安全或生命周期修改都需要审查“失败路径”：构造失败、系统调用失败、重复关闭、超时、取消和析构。
- 不提交 `build/`、二进制、日志、数据库密码、token、私钥。历史中若曾提交密钥，应立即轮换，不仅仅删除文件。

## 11. 求职材料准备

在 M6 完成前，简历应诚实称它为“个人重构项目”或“学习型网络服务”，不要声称已在线上生产环境承载业务。M6 之后，可依据实际报告填写如下模板，方括号内容必须由自己的代码和数据替换：

```text
个人项目：基于 C++17 与 io_uring 的 HTTP/WebSocket 服务器
- 设计并实现主从 Reactor 事件循环与 io_uring I/O 层；通过连接 generation 校验和统一关闭流程解决 fd 复用导致的过期完成事件问题，并以 [Sanitizer/连接压力测试命令] 验证资源安全。
- 将 HTTP 解析、路由、WebSocket 编解码、认证与聊天室业务分层，补齐 [N] 个单元/集成测试；支持静态文件、Keep-Alive、JWT 鉴权和房间消息广播。
- 建立 CMake + CTest + GitHub Actions 流水线，加入 ASan/UBSan、容器化开发环境、结构化日志与 Prometheus 指标，使项目可复现构建和故障定位。
- 在 [机器配置] 上使用 [工具与命令] 对 [场景] 压测，达到 [真实 QPS/连接数]、p99 [真实值]，并通过 [perf/火焰图] 将 [瓶颈] 优化为 [结果]。
```

面试准备的讲述顺序：先用 30 秒描述用户问题和整体架构；接着挑一个有证据的难点（建议 fd 复用/异步取消）；然后讲测试如何捕获问题、改动如何验证；最后展示 README、架构图、CI 和 benchmark 报告。能讲清失败方案和取舍，比罗列更多技术名词更有说服力。

在 M3、M5、M6 完成时，分别准备 3 分钟功能演示、5 分钟架构演示、10 分钟性能与故障排查演示。重构结束后，应基于最终提交、测试结果和压测报告逐条打磨上述简历表述，而不是现在预先固定数字。

## 12. 第一轮建议任务

下一步从 Phase 0 开始，建议按以下小任务拆分，每项独立提交：

1. 新增 `.gitignore`、CMake Presets 和 Debug/Release 构建说明。
2. 改造 CMake 为 target-based，并显式列出源文件。
3. 接入 GoogleTest/Catch2、CTest 和现有对象池测试。
4. 添加 ASan/UBSan 预设及最小 GitHub Actions。
5. 写连接生命周期图和复现 fd 重用问题的集成测试，然后进入 Phase 1。

每完成一个任务，更新本文对应阶段的验收状态，并补上该任务的设计说明。这样文档会成为项目的真实工程记录，而不是最后补写的说明。
