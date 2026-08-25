# 持续集成

GitHub Actions 工作流位于 `.github/workflows/ci.yml`，在向 `main` 推送或创建指向 `main` 的 Pull Request 时执行。

当前流水线包含：

1. Ubuntu 24.04 安装编译、`io_uring`、OpenSSL 和 MySQL client 依赖。
2. 以 `debug` 和 `asan` 两个 CMake 预设分别配置、构建和执行 CTest。
3. 检查提交中的空白错误、变更 C++ 的 clang-format，以及变更实现文件的 clang-tidy。

`tsan` 没有作为默认 CI 任务：它运行更慢，并且应先为 I/O 并发模型建立稳定的无误报测试场景。Phase 1 之后会增加专门的并发测试，再决定是否纳入定时或合并前任务。

格式检查和 clang-tidy 只针对当前提交变更的 C++ 文件，避免把历史存量格式问题混入本轮改动；完整历史代码清理仍是 Phase 4 的后续工作。
