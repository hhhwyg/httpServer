# 构建与测试

## 支持环境

服务器依赖 Linux 的 `io_uring`、`epoll`、`eventfd` 和 `sendfile`，应在 Ubuntu 或其他 Linux 发行版中构建。Windows 上请通过 WSL2 使用项目，项目目录建议放在 Linux 文件系统（例如 `~/code/httpServer`）以获得更稳定的文件权限和 I/O 行为。

不要把日常 Linux 构建目录放在 `/mnt/c/...`。它是 Windows 的 DrvFs 挂载，Git 锁文件和 CMake 的配置文件都可能因权限元数据限制而创建失败。完成一次 Git 提交后，应在 WSL 的 Linux 文件系统中重新 clone 项目；Windows 资源管理器可通过 `\\wsl.localhost\Ubuntu\home\<用户名>\...` 直接访问和编辑该目录。

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

若源码暂时仍在 `/mnt/c/...`，可将构建产物放在 WSL 临时目录：

```bash
cmake --preset debug -S . -B /tmp/httpserver-debug
cmake --build /tmp/httpserver-debug --parallel
ctest --test-dir /tmp/httpserver-debug --output-on-failure
```

`/tmp` 下的构建结果可能在重启后被清除，因此它只适合临时验证，不适合日常开发。

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

自动测试包含基础组件、生命周期、Poller、配置和加密单元测试，以及会启动真实服务的 `integration.http_smoke`。只运行 HTTP 黑盒测试：

```bash
ctest --preset debug -L integration --output-on-failure
```

`tests/base/LoggingTest.cpp` 是高输出量的手工压力程序，不会由 CTest 自动执行；需要时使用：

```bash
cmake --preset debug -DHTTPSERVER_BUILD_MANUAL_TESTS=ON
cmake --build build/debug --target LoggingStressTest --parallel
./build/debug/tests/LoggingStressTest
```

聊天室成员资格和断线清理由 `chat.room_membership` 单元测试覆盖。配置测试同时覆盖
`HTTPSERVER_MAX_FDS` 和 `HTTPSERVER_IO_URING_QUEUE_SIZE` 的环境变量解析。

HTTP 示例客户端也是可选目标：

```bash
cmake --preset debug -DHTTPSERVER_BUILD_EXAMPLES=ON
cmake --build build/debug --target HTTPClient --parallel
```

## 常用选项

`HTTPSERVER_WARNINGS_AS_ERRORS=ON` 用于新增代码已无警告的分支；在清理历史警告前，默认关闭。`BUILD_TESTING=OFF` 可用于仅交付服务器的最小构建。

开发容器包含在 `Dockerfile` 和 `docker-compose.yml` 中。它们使用仅供本地开发的示例凭据，不应直接用于部署：

```bash
docker compose up --build builder mysql
```

## 失败排查

- 找不到 `liburing.h`：安装 `liburing-dev`，并确认在 Linux/WSL 中执行构建。
- 找不到 MySQL 头文件或库：安装 `default-libmysqlclient-dev`；MariaDB 环境下 CMake 也接受 `libmariadb`。
- `io_uring_queue_init` 运行失败：检查 WSL 内核版本和宿主安全策略；该问题属于运行环境，不应通过静默回退来掩盖。
