# ADR 0001：io_uring 完成事件使用操作令牌

## 状态

已采纳。

## 背景

文件描述符数字会被内核复用。用 fd 作为 `io_uring` `user_data` 时，连接 A 关闭后的 CQE 可能通过同一 fd 命中新连接 B，导致错误回调、串包或关闭错误连接。

## 决策

每个 SQE 分配单调递增的 64 位 token。`UringOperationRegistry` 将 token 映射到操作类型、提交时的 fd 和提交时持有的 `Channel`。CQE 只从该注册表取操作，不按当前 fd 查找 Channel；inactive Channel 的回调被忽略。

## 备选方案

| 方案 | 结论 |
| --- | --- |
| fd 直接编码 | 拒绝，无法区分 fd 复用前后的连接。 |
| `Channel*` 直接作为 user_data | 拒绝，Channel 销毁后 CQE 会悬垂。 |
| token + 操作注册表 | 采用，明确控制 CQE 生命周期并保留对象所有权。 |

## 后果

操作登记需要少量哈希表内存，并且必须消费每个 CQE；换来关闭后不串线的确定性语义。长时间未完成的 I/O 仍需在后续工作中增加显式取消与压力测试。
