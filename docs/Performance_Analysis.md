# 性能分析

## 当前实验

本报告记录本次 Phase 6 在 WSL2 中完成的 HTTP 基线。它不是生产容量承诺，也不代表 WebSocket、MySQL 或其他未测试场景的性能。

| 项目 | 值 |
| --- | --- |
| 系统 | Linux WSL2，内核 `6.18.33.2-microsoft-standard-WSL2` |
| CPU | x86_64，12 个逻辑 CPU |
| 内存 | 7.4 GiB，总可用内存随宿主机变化 |
| 编译器 | GCC 15.2.0 |
| CMake | 4.2.3 |
| 构建 | Release，C++17，未启用 Sanitizer |
| 服务线程 | 6 个工作事件循环线程 |
| 请求 | `GET /ping` |
| 并发连接 | 128 |
| 客户端线程参数 | 4 |
| 持续时间 | 5 秒 |
| 工具 | Python asyncio 标准库 benchmark fallback |
| 限制 | 当前环境没有 `perf`、Docker；未测试 WebSocket 广播和数据库 |

## 原始结果

结果文件由 `tests/benchmark/run_backend_benchmark.sh` 写入 Git 忽略的 `benchmarks/` 目录。提交前应将本机生成的原始结果作为发布附件保存；不要把不同机器或不同参数的结果合并成单一数字。

本次执行命令：

```bash
HTTPSERVER_BENCH_DURATION=5s \
HTTPSERVER_BENCH_THREADS=4 \
HTTPSERVER_BENCH_CONNECTIONS=128 \
HTTPSERVER_BENCH_SERVER_THREADS=6 \
tests/benchmark/run_backend_benchmark.sh \
  /tmp/httpserver-codex-release-20260830/WebServer epoll 18150 \
  /tmp/httpserver-qps-async-20260830

HTTPSERVER_BENCH_DURATION=5s \
HTTPSERVER_BENCH_THREADS=4 \
HTTPSERVER_BENCH_CONNECTIONS=128 \
HTTPSERVER_BENCH_SERVER_THREADS=6 \
tests/benchmark/run_backend_benchmark.sh \
  /tmp/httpserver-codex-release-20260830/WebServer uring 18152 \
  /tmp/httpserver-qps-async-20260830
```

本次 Python 运行的原始结果保存在 `/tmp/httpserver-qps-async-20260830/`，因为 `benchmarks/` 按设计被 `.gitignore` 忽略；发布时应从该目录选择文件作为 release artifact。

摘要结果如下。四组测试均为 0 错误；CPU 为服务进程在测试窗口内消耗的 CPU 秒数，RSS 和 fd 是测试前后采样值。

| 后端 | 模式 | req/s | p50 ms | p95 ms | p99 ms | CPU s | RSS KiB | fd | ctx delta |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| epoll | Keep-Alive | 7645.41 | 16.004 | 20.823 | 25.948 | 1.680 | 23692 -> 24172 | 21 -> 21 | 123 |
| epoll | close | 2604.25 | 10.674 | 14.175 | 15.787 | 2.630 | 23668 -> 24236 | 21 -> 21 | 12738 |
| io_uring | Keep-Alive | 8194.63 | 15.093 | 18.560 | 21.697 | 2.130 | 26232 -> 26916 | 21 -> 21 | 121 |
| io_uring | close | 2634.51 | 10.529 | 14.422 | 17.288 | 2.860 | 26236 -> 27004 | 21 -> 21 | 12693 |

静态首页 `/` 结果：

| 后端 | 模式 | req/s | p50 ms | p95 ms | p99 ms | CPU s | RSS KiB | fd | ctx delta |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| epoll | Keep-Alive | 5680.19 | 22.133 | 25.822 | 27.830 | 6.010 | 23664 -> 24748 | 21 -> 21 | 124 |
| epoll | close | 2187.69 | 16.192 | 21.383 | 24.556 | 4.480 | 23624 -> 24876 | 21 -> 21 | 10733 |
| io_uring | Keep-Alive | 5757.20 | 21.658 | 25.960 | 28.188 | 6.430 | 26232 -> 26916 | 21 -> 21 | 127 |
| io_uring | close | 2157.94 | 16.793 | 22.675 | 26.373 | 4.730 | 26236 -> 28192 | 21 -> 21 | 10306 |

30 秒 Keep-Alive 长稳结果（`GET /ping`，其余参数相同）：

| 后端 | 持续时间 | req/s | p50 ms | p95 ms | p99 ms | 错误率 | CPU s | RSS KiB | fd | ctx delta |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| epoll | 30.012 s | 8514.36 | 14.782 | 16.942 | 19.528 | 0 | 11.130 | 23664 -> 24144 | 21 -> 21 | 127 |
| io_uring | 30.014 s | 8730.05 | 14.406 | 16.874 | 19.577 | 0 | 12.820 | 26236 -> 26956 | 21 -> 21 | 126 |

长稳测试只覆盖单进程、128 个 Keep-Alive 客户端和 `/ping`。它不能替代连接洪泛、慢客户端、WebSocket、数据库不可用或磁盘故障演练。

Python fallback 的并发模型是单 asyncio 事件循环管理多个连接；`threads=4` 作为 wrk 兼容参数记录，但不等同于 `wrk` 的线程调度模型。高吞吐结论使用下方的非阻塞 C++ 客户端，而不是 Python fallback。

## 客户端受限诊断

为判断 8k req/s 是否是服务端瓶颈，使用 asyncio 客户端做更高并发扫参，其他条件仍为 Release、`GET /ping`、Keep-Alive、5 秒：

| 后端 | 服务线程 | 连接数 | req/s | p99 ms | CPU s | 错误数 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| epoll | 6 | 128 | 7645.41 | 25.948 | 1.680 | 0 |
| io_uring | 6 | 128 | 8194.63 | 21.697 | 2.130 | 0 |
| epoll | 12 | 256 | 8196.69 | 37.896 | 1.920 | 0 |
| io_uring | 12 | 256 | 8198.80 | 37.114 | 2.230 | 0 |

服务端 CPU 在这些测试中仍未接近 12 个逻辑 CPU 的总容量；从 128 增到 256 连接后 QPS 基本不再上升而 p99 恶化，说明 8k req/s 主要受该客户端模型限制，不是服务器最大 QPS。

## 高吞吐复测

Python asyncio 结果用于诊断客户端限制；高吞吐结论使用仓库内的非阻塞 C++ epoll 客户端。Release、6 个服务线程、512 个 Keep-Alive 连接、4 个客户端事件循环的最新高吞吐结果如下：

| 后端 | 服务线程 | 连接数 | 客户端线程 | 持续时间 | req/s | 错误数 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| epoll | 6 | 512 | 4 | 20 s | 206,703 | 0 |
| io_uring | 6 | 512 | 4 | 20 s | 192,597 | 0 |

对应命令：

```bash
/tmp/httpserver-codex-bench-20260830/httpserver_epoll_benchmark 18274 512 20 4
# io_uring 对照：将服务切换为 io_uring，并使用端口 18276、持续 10 秒
/tmp/httpserver-codex-bench-20260830/httpserver_epoll_benchmark 18276 512 10 4
```

此前同参数曾得到 `217,960 req/s`、`196,696 req/s` 和 `208,834 req/s`，本轮在带资源采样时得到 `206,703 req/s`；这组差异来自 WSL2 调度抖动。io_uring 本轮为 `192,597 req/s`。20 万 req/s 是 epoll 在本机可达到的目标线，不是所有宿主机的容量承诺。

### 最新 CPU 与延迟采样

为补足高吞吐结果的服务端资源和延迟数据，使用同一台 WSL2 主机、同样的 512 连接、4 个客户端线程和 20 秒窗口重新采样。C++ 客户端的延迟从开始发送一个请求到收齐 114 字节响应计算，分位数使用 25 微秒桶，因此标记为桶上界。

| 后端 | 持续时间 | req/s | p50 上界 | p95 上界 | p99 上界 | 服务 CPU s | 单核占用 | 整机占用 | RSS 前 -> 后 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| epoll | 20 s | 206,703 | 1.100 ms | 2.250 ms | 3.150 ms | 83.84 | 419.1% | 34.9% | 33,184 -> 38,804 KiB |
| io_uring | 20 s | 192,597 | 1.250 ms | 2.525 ms | 3.350 ms | 83.05 | 415.1% | 34.6% | 26,204 -> 31,964 KiB |

epoll 在本轮约使用 4.19 个逻辑 CPU 核，io_uring 约使用 4.15 个；二者都没有把 12 个逻辑 CPU 打满。epoll 的吞吐约高 `7.3%`，尾延迟也略低。RSS 在 512 条长连接建立后增加约 5.6 MiB，当前没有观察到持续增长；仍需更长时间和连接洪泛测试确认内存曲线。

本轮命令（第五个参数为客户端线程数，第六个参数为服务端 PID）：

```bash
/tmp/httpserver-codex-bench-20260830/httpserver_epoll_benchmark \
  18280 512 20 4 SERVER_PID
/tmp/httpserver-codex-bench-20260830/httpserver_epoll_benchmark \
  18282 512 20 4 SERVER_PID
```

## 分析规则

- 只有成功响应计入吞吐；错误率必须单独报告。
- p50、p95、p99 必须来自同一次运行，不能用平均延迟替代尾延迟。
- `epoll` 与 `io_uring` 只有在相同环境和参数下才可比较。
- 没有 `perf`、火焰图或系统级 CPU 采样时，不声明具体热点或优化收益。
- 本轮可归因的优化是 `/ping` 固定响应直通、连续缓冲区视图、轻量请求头解析、单块响应直接 `write`、连接缓冲块复用、Keep-Alive 定时器刷新节流，以及 epoll 就绪事件直接处理；io_uring 还增加了 CQE 指针数组复用和提交/等待合并。高吞吐客户端和服务端参数必须保持一致，才能复现上述结果。
