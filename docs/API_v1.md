# HTTP API v1

## 状态

当前版本冻结房间、成员、内存消息历史和 WebSocket v1 协议。消息当前只保存在进程内；MySQL 持久化与重启恢复在 M3 验收。

## 通用约定

所有 `/api/v1` 接口使用 JSON。受保护接口要求：

```http
Authorization: Bearer <JWT>
```

成功响应格式：

```json
{
  "ok": true,
  "data": {}
}
```

失败响应格式：

```json
{
  "ok": false,
  "error": {
    "code": "stable_machine_readable_code",
    "message": "human readable message"
  }
}
```

客户端只使用 `error.code` 判断错误类型，不匹配 `message` 文本。

## 房间模型

```json
{
  "id": "10000000",
  "name": "engineering",
  "ownerId": "alice",
  "memberCount": 2,
  "joined": true
}
```

`joined` 表示当前认证用户是否已加入。房间 ID 对外固定为字符串；当前内存实现使用单调递增数字字符串，后续持久化可以替换生成方式而不改变协议类型。

## 创建房间

```http
POST /api/v1/rooms
Content-Type: application/json

{"name":"engineering"}
```

名称长度必须为 1 至 128 字节，不能全部为空白字符。创建者成为 owner 并自动加入。成功返回 `201`：

```json
{
  "ok": true,
  "data": {
    "room": {
      "id": "10000000",
      "name": "engineering",
      "ownerId": "alice",
      "memberCount": 1,
      "joined": true
    }
  }
}
```

## 发现房间

```http
GET /api/v1/rooms
```

成功返回 `200`，`data.rooms` 只包含当前用户已加入的房间，并按 room ID 升序排列。

## 加入房间

```http
POST /api/v1/rooms/{roomId}/members
```

成功加入返回：

```json
{"ok":true,"data":{"roomId":"10000000","joined":true}}
```

重复加入是幂等成功，返回 `200` 且 `joined` 为 `false`。房间不存在返回 `404 room_not_found`。

## 退出房间

```http
DELETE /api/v1/rooms/{roomId}/members/me
```

普通成员退出成功返回 `200`。owner 暂时不能退出，返回 `409 room_owner_cannot_leave`；后续加入转移 owner 或删除房间用例后再调整该规则。

## 查询成员

```http
GET /api/v1/rooms/{roomId}/members
```

只有房间成员可以查询。成功返回按用户 ID 排序的数组：

```json
{
  "ok": true,
  "data": {
    "roomId": "10000000",
    "members": ["alice", "bob"]
  }
}
```

非成员返回 `403 room_membership_required`，房间不存在返回 `404 room_not_found`。

## 查询历史消息

```http
GET /api/v1/rooms/{roomId}/messages?before=20000010&limit=50
```

`limit` 默认为 50，范围为 1 至 100。`before` 是上一页返回的服务端消息 ID，结果不包含该消息；不传时从最新消息向前查询。消息按时间正序返回：

```json
{
  "ok": true,
  "data": {
    "messages": [
      {
        "messageId": "20000000",
        "roomId": "10000000",
        "senderId": "alice",
        "clientMessageId": "01991f47-4600-79d5-a7df-f35ce75ff899",
        "content": "hello",
        "createdAt": "2026-09-06T10:00:00.000Z"
      }
    ],
    "nextBefore": null
  }
}
```

存在更早一页时，`nextBefore` 是下一次请求应使用的游标；非法游标返回 `400 invalid_cursor`。只有房间成员可以查询。

## WebSocket v1

连接仍使用：

```text
/ws?token=<JWT>
```

握手成功后，JWT `sub` 会绑定到稳定 session ID。客户端先订阅已经通过 HTTP 加入的房间：

```json
{
  "version": 1,
  "type": "room.subscribe",
  "requestId": "request-1",
  "payload": {"roomId": "10000000"}
}
```

订阅成功返回 `room.subscribed`。客户端发送消息：

```json
{
  "version": 1,
  "type": "message.send",
  "requestId": "request-2",
  "payload": {
    "roomId": "10000000",
    "clientMessageId": "client-message-1",
    "content": "hello"
  }
}
```

服务端向发送者返回 `message.ack`，向同房间其他在线连接推送 `message.created`。二者的 `payload.message` 都包含 `messageId`、`roomId`、`senderId`、`clientMessageId`、`content` 和 `createdAt`。`requestId` 与一次请求关联，`clientMessageId` 标识一次客户端消息提交，两者长度均为 1 至 128 字节。

WebSocket 错误也使用 v1 envelope：

```json
{
  "version": 1,
  "type": "error",
  "requestId": "request-2",
  "error": {
    "code": "room_membership_required",
    "message": "room membership and subscription are required"
  }
}
```

旧 `join/chat` 协议和旧 `/room/*` 接口暂时保留兼容，但新前端不再使用。旧 `join` 仍会自动加入房间，该行为将在兼容接口移除时一并删除。

## 当前错误码

| HTTP | code | 含义 |
| ---: | --- | --- |
| 400 | `invalid_argument` | 请求字段、类型或长度非法 |
| 401 | `unauthorized` | JWT 缺失、无效或过期 |
| 403 | `room_membership_required` | 当前用户不是房间成员 |
| 404 | `room_not_found` | 房间不存在 |
| 409 | `room_owner_cannot_leave` | owner 未转移前不能退出 |
| WS | `duplicate_message` | 同一发送者重复使用 `clientMessageId` |
| 400 | `invalid_cursor` | 历史消息游标不是正整数消息 ID |

## 本地 E2E 认证存储

`ChatBusinessFlowTest.py` 会为子进程显式设置 `HTTPSERVER_AUTH_STORE=memory`，从而在不依赖 MySQL 的情况下验证真实注册与登录接口。未设置该变量时，应用仍使用 MySQL 用户 Repository；内存认证存储不得用于部署环境。
