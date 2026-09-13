# 2026-09-12 复验与优化记录

本轮从 2026-09-09 的未提交工作区继续，验收结果对应当前工作区。此前的 ASan/UBSan 44/44、TSan、安全扫描和短时吞吐数字仍保留原日期；它们不代表本轮重新执行了所有扫描，也不代表云端最终验收已完成。

本轮最终回归：Debug + 真实 MySQL/Redis 为 44/44，通过耗时 130.94 秒；Release 压测器回归 17/17；部署回归 13/13；浏览器模拟故障与真实双账号主流程全部通过。当前目录 Gitleaks 扫描 6.48 MB 无泄露，三份 GitHub Actions 配置及内嵌 Shell 语法通过。原始 CTest 记录保存在 `build/debug/Testing/Temporary/LastTest.log`。

## M1-M3：聊天恢复与交互

前端修复及浏览器验收：

| 场景 | 修复后的预期结果 | 验证 |
| --- | --- | --- |
| 中文输入法回车选词 | composition 和 keyCode 229 不发送消息，普通 Enter 发送，Shift+Enter 换行 | 通过 |
| 中文消息边界 | 使用 UTF-8 字节数，4095 字节可发送、4098 字节禁用发送 | 通过 |
| 切换房间 | 每房间保留独立的当前页面草稿，退出登录清空草稿 | 通过 |
| 查看历史时收到消息 | 保持阅读位置，长列表在消息区域内滚动，输入框始终可见 | 通过 |
| 断线后重连 | 订阅成功后按每房间历史游标补拉，和实时推送去重 | 通过 |
| 消息已持久化但 ACK 丢失 | 历史/广播确认相同 clientMessageId，清理待确认状态，只显示一次 | 通过 |
| 订阅时依赖暂不可用 | 自动重连并重新订阅，依赖恢复后继续补拉与重发 | 通过 |
| 未确认消息遇到 1001 条离线积压 | 缓存裁剪保留未确认消息，补拉后以原 clientMessageId 重试 | 通过 |
| 退出后重新加入房间 | 重置房间游标、缓存和在途请求版本，历史可再次加载 | 通过 |
| 退出后登录另一个账户 | 旧异步房间请求不能覆盖新账户房间列表 | 通过 |
| 移动端 390x844 | 房间抽屉可关闭，主聊天占满宽度，输入框可见，无横向溢出 | 通过 |

测试入口：`node tests/ui/ChatInteractionTest.cjs`。依赖 Playwright 1.62.1 和 Chromium；CI 使用独立临时依赖目录，不引入前端构建框架。本机使用 Edge channel。

```bash
export NODE_PATH=/path/to/playwright/node_modules
node tests/ui/ChatInteractionTest.cjs
# 额外连接真实服务，创建两位临时用户并执行实际业务流程：
HTTPSERVER_UI_BASE_URL=http://127.0.0.1:18094 node tests/ui/ChatInteractionTest.cjs
```

真实服务路径已验证两个浏览器用户的注册、登录、房间加入、消息 ACK、广播、刷新后历史恢复及退出登录。该验证使用本机独立内存仓库服务；MySQL 持久化由 CTest 的真实依赖测试覆盖。截图默认保存在系统临时目录 `httpserver-ui-acceptance`，可用 `HTTPSERVER_UI_ARTIFACTS` 指定位置。

## 界面改版复验

本次界面优化保留原生 HTML/CSS/JavaScript 与消息协议，不引入前端构建框架。移除重复覆盖的内联样式，采用浅色全窗口聊天布局，统一登录、房间列表、消息气泡、输入框与弹窗；调整辅助文字对比度并适配移动端抽屉。Lucide 图标随应用本地分发，来源和许可证位于 `wwwroot/assets/icons/`。

新增并验证密码显隐、切换登录/注册后恢复隐藏、复制房间 ID 的成功与拒绝反馈、回到最新消息、抽屉关闭与 Escape/焦点恢复。原有输入法、字节边界、草稿、阅读位置、断线恢复、丢 ACK 与账户隔离回归保持通过。

生产兼容修复：脚本提取为同源 `wwwroot/chat.js`，保持 Nginx 的严格 CSP，不添加 `unsafe-inline`；服务端补充 `.svg` 的 `image/svg+xml` 类型，避免图标被当作 HTML 返回。

- 浏览器 fixture 在生产 CSP 与 `X-Content-Type-Options: nosniff` 下运行，检查脚本、样式、图标加载与资源 MIME，无 CSP 拒绝。
- 1440x900、1280x720、390x844、320x740 四种视口全部通过，涵盖超长房间名、64 字符用户名、中英文长消息、登录/注册、创建/加入房间，未发现横向溢出、标题与工具栏重叠或输入框不可见。
- 真实本地服务的 JS/CSS/SVG 均返回 200 和正确 MIME；双浏览器注册、登录、加入房间、ACK、广播、刷新历史恢复与退出登录全部通过。
- 本次生成 30 张截图，输出目录通过 `HTTPSERVER_UI_ARTIFACTS` 指定为系统临时目录中的 `httpserver-ui-redesign`；正常对话展示图仅使用隔离测试 fixture 数据。
- 重新构建后，`httpdata.mime_type`、`httpdata.static_file`、`integration.http_protocol` 共 3/3 通过，脚本语法与 `git diff --check` 通过。

此次真实浏览器验证仍使用本机内存仓库预览服务，不代表公网发布或持久化验收。

### 第二轮审美与会话列表优化

按 `redesign-existing-projects` 的先审查后改版流程，保留已有品牌、原生技术栈和页面入口，重新调整字体、区域层次及消息排版。侧栏使用白色、会话区使用浅灰、自己的消息使用深绿底色；补充稳定的房间/用户标识配色、选中状态、日期分组、消息摘要、草稿摘要与全部/未读筛选。

字体采用官方 Noto Sans SC 2.004，可变字重 100-900。57 个本地 WOFF2 分片覆盖原字体 30,890 个字符，合计 9,782,620 字节，单片不超过 311,716 字节。真实本机浏览器已验证：有本地字体时零下载；禁用本地字体时首屏加载 1,196,372 字节，罕用字额外按需加载，实际渲染使用 Noto Sans SC。来源、OFL、覆盖 manifest 和重建脚本在 `wwwroot/assets/fonts/`，运行时不依赖字体 CDN 或 Python 工具。

会话列表必须显示真实状态，相应补充以下行为：

- 连接/重连时按队列订阅全部已加入房间，优先当前房间；收到订阅确认后才允许发送。
- 消息合并和 ACK 更新后统一按服务端 ID 排序，临时消息保持待确认顺序；系统提示锚定在产生时的已确认消息后，不占据最新会话摘要。
- 未读采用当前浏览器会话内的已读水位和有界 ID 集合，达到 100 后显示 `99+`。它不承诺刷新、重新登录或跨设备后的未读恢复。
- 断线补拉在初始历史全部分页成功后才开始计算增量未读，分页 checkpoint 不等同于初载完成；空历史也记录完成状态。
- 用户切入房间时清理未读；退出当前房间导致自动切换时恢复目标房间草稿。

测试入口仍为 `tests/ui/ChatInteractionTest.cjs`，默认执行完整套件。`HTTPSERVER_UI_SUITE=visual` 可单独执行视觉检查，不能代替完整门禁。第二轮截图目录为系统临时目录中的 `httpserver-ui-redesign-v2`。

最终稳定版完整 UI 套件与真实本地双账号后台消息流程全部通过，包含分页初载失败重试、系统提示顺序和强制下载字体路径；生成 32 张最终截图。相关 MIME、静态文件及 HTTP 协议测试为 3/3，脚本语法与 `git diff --check` 通过。

## M9：迁移、失败发布与安全门禁

修复重复执行已应用 migration 002 导致 `ADD COLUMN` 失败的问题。两个迁移入口共用 `schema_migrations` 判定；MySQL DDL 部分提交后失败仍需向前修复，不自动回滚数据库结构。

发布脚本分别保存 A/B 发布前的不可变镜像 ID，只在应用切换后触发回滚；回滚后检查两实例 readiness，并重载 Nginx 与验证入口。部署锁阻止并发发布；备份使用 0600 权限，写入失败清理 partial，`/backups/` 已加入忽略规则。

部署工作流将服务器脚本、Compose 与迁移切换到指定 commit SHA。镜像发布工作流在第一次推送前独立执行 Gitleaks 和 Trivy，不依赖另一个 CI 工作流是否成功。工作流仅通过本地 YAML/内嵌 Shell 校验，尚未在实际 GitHub Actions/registry 执行。

```bash
python3 -B tests/deployment/DeployReleaseTest.py
python3 -B tests/deployment/DeployRollbackDockerTest.py
```

- 部署回归 13/13 通过，涵盖新库/重复迁移、A/B readiness、部分启动、Nginx 校验与重载、公网探针失败、信号中断、前置失败、回滚失败报告和部署锁。
- 真实独立 Docker 演练：`readiness_failure=detected restored_images=2 proxy_ready=1 migrations=already_applied elapsed_seconds=153.82`。
- 演练使用临时 MySQL 8.4、Redis、两个应用与 Nginx；结束后已清理该演练的容器、卷和临时镜像。

本地失败发布回滚已验收完成；目标云主机上的 registry 拉取、可信 TLS 和实际发布仍待外部环境。

## M10：压测结果真实性

压测器改为保留完整 URL 的主机、端口、路径和查询参数，支持 HTTPS/WSS、Origin 和自定义 CA，默认校验证书。握手与消息校验覆盖子协议、服务端消息 ID、房间、客户端消息 ID、时间和正文；错误退出非零，结果文件保留 stderr。

chat/ping 使用固定 2048 桶直方图，chat 不再无限保存全部消息 ID。资源 JSONL 按间隔记录本机客户端和显式指定的本机服务 PID；识别进程消失/PID 复用。上下文切换字段明确为主线程计数。远端服务器资源曲线需单独从 Prometheus 采集。

```bash
HTTPSERVER_BENCH_TEST_SERVER="$PWD/build/release/WebServer" \
  python3 -B tests/benchmark/BenchmarkClientTest.py
```

17/17 通过，涵盖远端地址、TLS 信任/拒绝、帧边界/超时、尾部重复、资源采样、真实 API 重跑、chat/ping 和 soak 参数覆盖。Release 已重新构建并用于本轮测试。

结果使用约 1% 精度的对数直方图上界估计百分位，不能与旧版精确排序的短测数字直接当作优化前后对照。当前业务负载为 `closed-loop-round-robin-one-message-in-flight`，多房间轮流发送；它用于正确性和受控负载验收，尚未覆盖多房间独立发送者并发容量。不同端点也需结合 Compose/instanceId 确认实例映射。

## 剩余验收

1. 提供目标云主机、域名和可信证书，验证空白主机部署、registry/Actions 发布及公网 WSS。
2. 补充多发送者并发业务负载、登录/历史查询容量与持续资源曲线；当前闭环 fanout 不代替这些场景。
3. 在目标机执行单/双实例各三轮 30 分钟，以及 24 小时长稳；压测端与服务端尽量分机。
4. 将 commit/tag、环境参数、原始结果和资源曲线归档为 release artifact，再提取简历数字。

未执行的长时或公网验收继续保持未完成，不以这次浏览器和本地故障测试替代。
