# io_uring I/O 设计

## 事件模型

每个 `EventLoop` 持有一个 `IoUring`。`Channel` 表示一个 fd 的事件兴趣与回调；`IoUring` 为 poll、异步 read 和异步 write 分别提交 SQE。CQE 由同一 EventLoop 线程消费，因此操作注册表不需要额外锁。

## 操作令牌

`io_uring` 的 `user_data` 是 64 位 token，不是 fd：

```text
SQE token
  -> UringOperationRegistry
  -> { operation type, original fd, shared_ptr<Channel> }
  -> CQE token
  -> original Channel callback, only when Channel is active
```

Token `0` 留给内部 `POLL_REMOVE` SQE，其 CQE 没有业务回调。poll 取消后，目标 poll 的 `-ECANCELED` CQE 仍通过原 token 被消费，因此能正确释放旧操作持有的 Channel。

## 重注册规则

poll CQE 表示一次性 poll 已完成，`Channel::markPollConsumed()` 会使上一次注册事件失效。回调重新设置事件后，`modify()` 必须提交新的 poll；否则当新旧事件位相同（例如监听 socket 继续等待 `EPOLLIN`）时，服务器会误以为已有监听而停止接收连接。

`modify()` 的顺序为：

1. 仅在事件集合变化时执行。
2. 请求取消旧 poll token。
3. 若新事件集合非空，提交新 poll token。

旧 CQE 和取消 CQE 的到达顺序不重要。注册表只会在被消费 token 与当前 fd 的 poll token 相等时删除映射。

## 关闭语义

`remove()` 先使 Channel inactive，再取消当前 poll，并删除 Poller 中的 Channel 与类型擦除连接所有权。已提交的 read/write 没有向新的 fd 重新路由：它们继续持有旧 Channel，完成后因 inactive 被丢弃并释放。

当前实现每个连接应保持一个活跃 poll；读写操作分别由 `HttpData::isReading_`、`HttpData::isWriting_` 防止并发重复提交。read 收到 `-EAGAIN` 时重新注册 `EPOLLIN`，write 收到 `-EAGAIN` 时重新注册 `EPOLLOUT`；显式 `IORING_OP_ASYNC_CANCEL` 覆盖和长稳压测仍是后续工作。
