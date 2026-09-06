# Changelog

## Unreleased

- 修复日志轮转编译依赖和 io_uring registry 单测链接依赖。
- 修复辅助线程继承未屏蔽信号导致的非优雅退出。
- 增加 Release HTTP 基准脚本、Python fallback 和 Phase 6 性能记录模板。
- 增加非阻塞 C++ epoll 高吞吐客户端；优化后的 Release `/ping` 热路径在 WSL2 重复测试中超过 20 万 req/s。
- 增加 [HTTP 性能优化历程](docs/Optimization_History.md)，记录客户端受限诊断、各轮热点改动、对照数据和未完成的 Phase 6 场景。
- CI 增加 ASan/UBSan 构建与测试。
