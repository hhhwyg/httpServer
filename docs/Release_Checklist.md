# 发布检查表

- [ ] `scripts/validate_linux.sh debug` 和 Linux CI 通过。
- [ ] Docker Compose 数据库集成测试通过。
- [ ] ASan/UBSan 和连接生命周期测试通过。
- [ ] `/healthz`、`/readyz`、`/metrics` 与优雅退出测试通过。
- [ ] `.env`、JWT、数据库口令、日志和构建产物未进入提交。
- [ ] README、配置、部署、Runbook 和已知限制与代码一致。
- [ ] 性能报告包含环境、原始命令、原始数据和局限。
- [ ] 版本号、CHANGELOG、Git tag 与发布说明已准备。
