# 性能验证

本项目尚未发布任何吞吐或延迟结论。本文件定义 Phase 6 的可复现实验记录格式，所有结果必须附机器、内核、构建和原始命令。

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

服务启动后，在 Linux 上安装 `wrk`，执行：

```bash
tests/benchmark/run_http_benchmark.sh http://127.0.0.1:8088/ping
```

可用 `HTTPSERVER_BENCH_THREADS`、`HTTPSERVER_BENCH_CONNECTIONS` 和 `HTTPSERVER_BENCH_DURATION` 调整参数。脚本会记录内核、CPU、fd 上限、wrk 版本和原始输出到 Git 忽略的 `benchmarks/`。
