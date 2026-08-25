# 可切换 Poller 与性能对比

## 目标

服务器的 HTTP、WebSocket、线程模型、定时器和 `Channel` 回调只依赖 `Poller` 接口。`IoUring` 和 `Epoll` 是该接口的两个实现，因此可以在不改变业务逻辑和并发模型的前提下比较内核 I/O 后端。

```text
Main -b <backend>
  -> Server
  -> EventLoopThreadPool
  -> EventLoop
  -> Poller
       |- IoUring: poll SQE + async read/write SQE
       `- Epoll: epoll_wait + nonblocking read/write
```

`io_uring` 使用每个操作独立的 token 关联 CQE；`epoll` 使用 `Channel*` 作为 `epoll_event.data.ptr`，只在本地注册表命中后才处理事件。两者均避免按当前 fd 重新查找连接，从而降低 fd 复用导致错误路由的风险。

## 使用方式

默认后端是 `io_uring`。通过 `-b` 选择后端，`uring` 和 `io_uring` 都表示 io_uring：

```bash
./build/release/WebServer -b uring -t 4 -p 8088
./build/release/WebServer -b epoll -t 4 -p 8088
```

无效值会直接退出并提示可用选项。一个进程中的主 EventLoop 与全部工作 EventLoop 必须使用相同后端，不能混用。

## 接口边界

`Poller` 的职责是 fd 注册、兴趣事件更新、删除、非阻塞读写提交、等待完成事件和驱动超时。它不解析 HTTP，不持有路由状态，也不接触 MySQL 或聊天室业务。

`Poller` 使用 `add`、`modify`、`remove` 表达通用注册、更新和删除语义；这些名称不表示 `IoUring` 内部调用了 epoll。

## 可复现对比

先完成 Release 构建，并保证每轮只运行一个服务实例：

```bash
cmake --preset release
cmake --build --preset release --parallel
ulimit -n 65535

./build/release/WebServer -b epoll -t 4 -p 8088 -l /tmp/httpserver-epoll.log
# 另一个终端：
wrk -t4 -c128 -d30s --latency http://127.0.0.1:8088/ping

./build/release/WebServer -b uring -t 4 -p 8088 -l /tmp/httpserver-uring.log
# 使用完全相同的 wrk 命令重复执行。
```

每个后端至少预热一次、正式运行三次，并记录中位数。固定以下变量：机器与内核版本、CPU governor、线程数、连接数、请求路径、构建类型、日志级别、文件描述符上限和压测工具版本。不要在运行服务的同一 CPU 核上同时启动无关程序。

除 Requests/sec 外，至少记录 p50/p95/p99 延迟、非 2xx/连接错误、进程 CPU 与 RSS、上下文切换和打开 fd 数。例如可在压测期间运行：

```bash
pidstat -rud -p <server-pid> 1
perf stat -p <server-pid> -e cycles,instructions,context-switches,cpu-migrations
ls /proc/<server-pid>/fd | wc -l
```

结果只能说明当前机器、内核和该负载下的表现。短连接、Keep-Alive、小静态文件、大文件、慢客户端和 WebSocket 广播的瓶颈不同，应分别报告，不能用单一 QPS 宣称任意场景都更快。

## 当前限制

`Epoll::submitRead` 和 `Epoll::submitWrite` 立即执行一次非阻塞系统调用；遇到 `EAGAIN` 后由 `HttpData` 重新注册相应的可读或可写事件。它用于保持现有 `Channel` 异步回调路径一致，且适合建立公平的端到端基线，但并不代表 epoll 自身提供内核异步读写。

后续基准工作会增加自动化脚本、机器环境模板、原始数据和报告。没有这些数据前，README 和简历不应声明具体吞吐或延迟数字。
