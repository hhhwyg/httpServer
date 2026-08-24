# 构建与测试

## 支持环境

服务器依赖 Linux 的 `io_uring`、`epoll`、`eventfd` 和 `sendfile`，应在 Ubuntu 或其他 Linux 发行版中构建。Windows 上请通过 WSL2 使用项目，项目目录建议放在 Linux 文件系统（例如 `~/code/httpServer`）以获得更稳定的文件权限和 I/O 行为。

最低工具版本：CMake 3.21、支持 C++17 的 GCC/Clang、Ninja。Ubuntu 安装依赖：

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build \
  liburing-dev libssl-dev default-libmysqlclient-dev
```

## CMake 预设

所有构建产物均写入 `build/<preset>/`，不要在源码目录直接执行 `make` 或编译出可执行文件。

```bash
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug
```

可用预设：

| 预设 | 用途 |
| --- | --- |
| `debug` | 日常开发与单元测试 |
| `release` | 性能测试前的优化构建 |
| `asan` | AddressSanitizer + UBSanitizer，检查泄漏、越界和未定义行为 |
| `tsan` | ThreadSanitizer，仅用于定位并发数据竞争；不作为常规发布构建 |

Sanitizer 示例：

```bash
cmake --preset asan
cmake --build --preset asan --parallel
ctest --preset asan
```

当前自动测试为 `base.object_pool`。`tests/base/LoggingTest.cpp` 是高输出量的手工压力程序，不会由 CTest 自动执行；需要时使用：

```bash
cmake --preset debug -DHTTPSERVER_BUILD_MANUAL_TESTS=ON
cmake --build build/debug --target LoggingStressTest --parallel
./build/debug/tests/LoggingStressTest
```

HTTP 示例客户端也是可选目标：

```bash
cmake --preset debug -DHTTPSERVER_BUILD_EXAMPLES=ON
cmake --build build/debug --target HTTPClient --parallel
```

## 常用选项

`HTTPSERVER_WARNINGS_AS_ERRORS=ON` 用于新增代码已无警告的分支；在清理历史警告前，默认关闭。`BUILD_TESTING=OFF` 可用于仅交付服务器的最小构建。

## 失败排查

- 找不到 `liburing.h`：安装 `liburing-dev`，并确认在 Linux/WSL 中执行构建。
- 找不到 MySQL 头文件或库：安装 `default-libmysqlclient-dev`；MariaDB 环境下 CMake 也接受 `libmariadb`。
- `io_uring_queue_init` 运行失败：检查 WSL 内核版本和宿主安全策略；该问题属于运行环境，不应通过静默回退来掩盖。
