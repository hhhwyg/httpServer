# 性能验证

本项目的性能结论只来自已保存的原始结果。本文件定义 Phase 6 的可复现实验记录格式，所有结果必须附机器、内核、构建和原始命令。

## 固定记录项

- CPU 型号与核心数、内存、磁盘类型。
- Linux 发行版、内核版本、`ulimit -n`、编译器和 CMake 版本。
- Git commit、构建预设、I/O 后端、工作线程数和服务配置。
- 压测工具及版本、并发数、连接数、持续时间、请求大小。

## 场景

1. 静态文件短连接与 Keep-Alive。
2. `/ping` 的低延迟基线。
3. WebSocket 单房间广播与多房间广播。
4. 连接洪泛、慢读取客户端和断连压力。
5. `epoll` 与 `io_uring` 的同环境对照。

每个场景至少记录 QPS、错误率、p50/p95/p99 延迟、CPU、RSS、上下文切换和打开 fd 数。优化必须保留优化前后的原始命令和数据；没有对照数据时不得使用“高性能”描述。

## HTTP 基线脚本

先构建 Release 版本并启动服务。`run_http_benchmark.sh` 优先使用 `wrk`；未安装时使用仓库内基于 asyncio 的 Python 标准库后端，因此 Linux/WSL 不需要额外安装压测工具：

```bash
tests/benchmark/run_http_benchmark.sh http://127.0.0.1:8088/ping
```

可用 `HTTPSERVER_BENCH_THREADS`、`HTTPSERVER_BENCH_CONNECTIONS`、`HTTPSERVER_BENCH_SERVER_THREADS`、`HTTPSERVER_BENCH_DURATION` 和 `HTTPSERVER_BENCH_CONNECTION_MODE` 调整参数。脚本会记录内核、CPU、fd 上限、工具和原始输出到 Git 忽略的 `benchmarks/`。设置 `HTTPSERVER_BENCH_SERVER_PID` 后，Python 后端还会记录服务进程 CPU、RSS、fd 和上下文切换。

`run_backend_benchmark.sh` 默认执行 `keep-alive close` 两种模式；长稳测试可设置 `HTTPSERVER_BENCH_MODES=keep-alive`，避免重复执行短连接场景。服务端工作线程数可用 `HTTPSERVER_BENCH_SERVER_THREADS` 设置，原始结果会记录该值。

指定后端并自动执行 Keep-Alive/短连接两组测试：

```bash
HTTPSERVER_BENCH_DURATION=30s \
HTTPSERVER_BENCH_THREADS=4 \
HTTPSERVER_BENCH_CONNECTIONS=128 \
tests/benchmark/run_backend_benchmark.sh \
  ./build/release/WebServer epoll 18090 benchmarks
```

第五个参数可替换请求路径，例如 `run_backend_benchmark.sh ... benchmarks /` 用首页静态文件作为基线。

`io_uring` 与 `epoll` 的对照必须在相同机器、构建、线程数、连接数、持续时间和请求路径下分别执行。当前报告不包含 WebSocket 广播、连接洪泛或 `perf` 火焰图数据时，不对这些场景作性能结论。

## 高吞吐 `/ping` 客户端

Python fallback 适合无额外依赖的协议回归，但单 asyncio 事件循环会限制高吞吐测试。需要验证 `/ping` 高 QPS 时，打开手工测试目标并使用非阻塞 C++ 客户端：

```bash
cmake -S . -B /tmp/httpserver-bench -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DHTTPSERVER_BUILD_MANUAL_TESTS=ON
cmake --build /tmp/httpserver-bench --target httpserver_epoll_benchmark --parallel

./build/release/WebServer -b epoll -t 6 -p 18090 -l /tmp/httpserver.log &
/tmp/httpserver-bench/httpserver_epoll_benchmark 18090 512 10 4
```

参数依次为端口、连接数、持续秒数、客户端事件循环线程数和可选的服务端 PID。传入服务端 PID 后，工具还会输出服务 CPU 秒数、按单核和整机逻辑 CPU 归一化的占用率、RSS 和上下文切换增量。它固定验证 `GET /ping` Keep-Alive，适合吞吐上限；延迟分位数来自每个完整请求/响应，使用 25 微秒桶并报告桶上界。

每轮优化的动机、改动、对照结果和限制记录在 [Optimization_History.md](Optimization_History.md)；不要只保留最终 QPS。
