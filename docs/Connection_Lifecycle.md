# 连接生命周期

## 目标

本项目的一个 TCP 连接必须只有一个关闭入口，并且在 fd 被内核复用后，旧连接的 `io_uring` 完成事件不能驱动新连接。这里的设计处理资源正确性，不承诺吞吐或容量指标；性能结论将在压测阶段给出。

## 所有权

| 对象 | 创建方 | 强所有者 | 释放时机 |
| --- | --- | --- | --- |
| `HttpData` | `Server::handNewConn()` | `IoUring::connectionOwners_` | 连接从 Poller 删除后 |
| `Channel` | `HttpData` 构造函数 | `HttpData` 与未完成的 `UringOperation` | `HttpData` 和旧 CQE 均释放后 |
| 当前 poll | `IoUring::submitPollAdd()` | `UringOperationRegistry` | 收到 poll/CANCELLED CQE 后 |
| 异步 read/write | `submitRead()` / `submitWrite()` | `UringOperationRegistry` | 收到对应 CQE 后 |
| `TimerNode` | `TimerManager` | 定时器队列 | 超时或 `HttpData::seperateTimer()` 后 |

`Channel::holder_` 和 Channel 回调均使用 `weak_ptr<HttpData>`。这样不会形成 `HttpData -> Channel -> callback -> HttpData` 的引用环。Poller 的 `connectionOwners_` 是连接处于注册状态时唯一明确的强所有者；它按 `Channel*` 而不是 fd 索引，避免 fd 复用影响所有权。

## 状态与关闭顺序

```text
accept
  -> HttpData / Channel 初始化
  -> Poller 持有 HttpData，提交 poll token
  -> CQE 根据 token 找回原始 Channel
  -> HTTP/WebSocket 回调
  -> handleClose()
       1. Channel 失活，取消当前 poll，移出 Poller 所有权表
       2. 解除 TimerNode 对 HttpData 的引用
       3. 离开聊天室并清理房间引用
       4. close(fd)，fd_ 置为 -1
  -> 旧 read/write/poll CQE 到达后只释放操作记录，不再调用回调
```

`handleClose()` 由 `closed_` 保护，重复调用不会重复关闭 fd。关闭前保留一个本地 `shared_ptr<HttpData>`，因为分离定时器时可能释放最后一个外部所有者。

## fd 复用保护

旧实现将 `(操作类型, fd)` 写入 `io_uring` 的 `user_data`，并在 CQE 到达时通过当前 fd 查找 `Channel`。关闭后内核可能把同一个数字 fd 分给新连接，此时旧 CQE 会错误调用新连接。

现在每次提交 SQE 都分配单调递增的 token。注册表保存 `token -> {类型, fd, 原始 Channel}`，CQE 只按 token 取出该操作记录。即使新 poll 已覆盖同一 fd 的映射，旧 token 被消费时也不会删除新 token；关闭后的 Channel 会被标记为 inactive，旧 CQE 因而被安全忽略。

## 验证

```bash
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug

cmake --preset asan
cmake --build --preset asan --parallel
ctest --preset asan
```

测试覆盖：

- `base.uring_operation_registry`：旧 poll token 到达时不能删除同 fd 的新 poll token。
- `httpdata.lifecycle`：弱回调不会形成对象环；关闭会释放 fd；重复关闭安全。
- `base.object_pool`：缓存内存重新构造对象，不复用旧连接状态。

已用真实 `WebServer` 对本机 `GET /ping` 做过回归验证。后续需要增加长时间连接风暴、慢客户端和 fd 复用压力测试，才能对泄漏与容量做出更强结论。
