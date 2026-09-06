# M1：业务领域与 API

## 完成状态

已于 2026-09-06 完成代码与自动化验收。当前消息历史由内存 Repository 提供，支持服务端消息 ID 游标分页；应用进程重启后的历史恢复按本模块原定边界，在 M3 MySQL 持久化完成后验收。

## 目标

将当前“内存房间 + 字符串广播”升级为边界清晰的聊天室业务模型，为持久化、幂等和多实例提供稳定契约。业务层不得依赖 `HttpData`、fd、Poller 或具体数据库客户端。

完成后，用户能够注册登录、创建房间、加入或退出房间、查看成员、发送消息、游标分页查询历史，并在无权限时得到稳定的业务错误码。

## 非目标

- 不实现好友关系、群管理员体系、文件消息、全文搜索和消息编辑。
- 不设计通用即时通信平台；第一版只支持公开房间和文本消息。
- 不让前端直接依赖 MySQL 自增主键或 Redis key 格式。

## 领域模型

定义纯业务对象 `User`、`Room`、`RoomMember`、`Message`，以及接口 `RoomRepository`、`MessageRepository`、`MembershipRepository`。建议目录：

```text
include/httpserver/domain/
include/httpserver/application/
src/domain/
src/application/
```

房间权限第一版固定为：创建者自动加入；只有成员可以查询历史和发送消息；用户可以主动退出；房间创建者不能在仍为 owner 时退出。错误响应使用稳定代码，例如 `ROOM_NOT_FOUND`、`NOT_ROOM_MEMBER`、`DUPLICATE_MESSAGE`，HTTP 状态与业务 code 分开表达。

## 协议计划

HTTP API 统一放在 `/api/v1`：

| 方法 | 路径 | 行为 |
| --- | --- | --- |
| POST | `/api/v1/rooms` | 创建房间并加入创建者 |
| GET | `/api/v1/rooms` | 查询当前用户已加入的房间 |
| POST | `/api/v1/rooms/{roomId}/members` | 加入房间 |
| DELETE | `/api/v1/rooms/{roomId}/members/me` | 退出房间 |
| GET | `/api/v1/rooms/{roomId}/members` | 查询成员列表 |
| GET | `/api/v1/rooms/{roomId}/messages` | 使用 `before` 和 `limit` 查询历史 |

WebSocket 使用版本化 envelope：

```json
{
  "version": 1,
  "type": "message.send",
  "requestId": "01991f47-1b22-7ad0-8c23-59a221adbd21",
  "payload": {
    "roomId": "10000001",
    "clientMessageId": "01991f47-4600-79d5-a7df-f35ce75ff899",
    "content": "hello"
  }
}
```

服务端消息必须返回 `messageId`、`roomId`、`senderId`、`content`、`createdAt`。所有错误也使用 envelope，不再静默忽略合法 JSON 中的非法业务字段。

## 实施计划

1. 写 `docs/API_v1.md`，冻结字段、大小限制、状态码和错误码。
2. 添加领域对象和 Repository 接口，先用内存实现保证业务单测独立运行。
3. 将房间创建、加入、退出、列表和成员校验迁移到 `ChatService`。
4. 将 WebSocket JSON 解析与业务处理拆开，协议层只生成命令对象。
5. 实现历史接口的游标格式；游标编码服务端消息 ID，不使用 offset。
6. 更新前端，使发送、ACK、错误和服务端推送采用同一版本协议。
7. 删除旧 `/room/create`、`/room/list` 前先保留兼容层，并在文档中给出移除版本。

## 参考

- [Tinode](https://github.com/tinode/chat)：参考消息 ID、历史同步、presence 和协议分层，不复制其完整功能。
- [Drogon](https://github.com/drogonframework/drogon)：参考 controller、filter、业务与 transport 解耦方式。
- [RFC 6455](https://www.rfc-editor.org/rfc/rfc6455.html)：WebSocket 握手、帧和关闭语义。
- 当前项目的 [WebSocket 协议](../WebSocket_Protocol.md) 和 [API](../API.md)。

## 验收方法

自动化验收目标命令：

```bash
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug -L "domain|application|protocol"
python3 tests/e2e/ChatBusinessFlowTest.py ./build/debug/WebServer 18310
```

`ChatBusinessFlowTest.py` 必须验证注册、登录、创建、重复加入、成员列表、非成员发送被拒绝、加入后发送、退出后发送被拒绝。协议单测覆盖缺字段、错误类型、未知版本、超长内容和非法游标。

人工验收使用两个浏览器账户完成上述流程，网络面板中的 WebSocket 帧必须包含版本、请求 ID、客户端消息 ID 和服务端消息 ID；刷新页面后历史恢复在 M3 完成后验收。

## 交付物

- `docs/API_v1.md`
- 领域对象和 Repository 接口
- 内存 Repository 与业务单测
- `/api/v1` HTTP 路由和 v1 WebSocket 协议
- `tests/e2e/ChatBusinessFlowTest.py`

以上交付物均已落地。旧 `/room/*` 和旧 WebSocket `join/chat` 仅作为有明确文档的兼容层保留，新前端已迁移到 HTTP API v1 与 WebSocket v1 envelope。
