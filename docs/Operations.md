# 运行操作

## 优雅退出

进程通过 Linux `signalfd` 在主 EventLoop 中处理 `SIGTERM` 与 `SIGINT`。收到信号后会立即停止监听新连接，保留已有连接最多 2 秒，再退出主循环并关闭数据库执行器和工作事件循环。

```bash
kill -TERM "$(pgrep -f WebServer)"
```

不要使用 `SIGKILL` 作为常规停止方式；它会跳过宽限期和资源回收。容器编排的停止宽限期应不少于 5 秒。

`integration.graceful_shutdown` 会验证服务收到 `SIGTERM` 后在宽限期内以状态码 0 退出。

## 健康检查

`/healthz` 适合存活探针；`/readyz` 适合就绪探针。数据库或 JWT 未配置时，进程仍可服务静态文件，但 `/readyz` 返回 `503`，不应接收需要认证的业务流量。

## 当前限制

退出宽限期当前是固定 2 秒，尚未按当前活跃连接数提前完成，也未实现对超时 WebSocket 连接的 close frame 通知。日志轮转、日志磁盘失败降级和 systemd 示例属于后续工作。
