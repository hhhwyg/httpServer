# HTTP API

## Public endpoints

- `GET /` and `GET /<file>` serve files below `wwwroot`.
- `HEAD /<file>` returns the same metadata as `GET` without a response body.
- `GET /ping` returns `OK`.
- `POST /register` and `POST /login` require JSON `username` and `password` and
  require the database and JWT configuration to be enabled.

## Authenticated room endpoints

Room endpoints require `Authorization: Bearer <JWT>`:

- `POST /room/create` with `{ "name": "room name" }`.
- `GET /room/list`.

The response and WebSocket protocol use string room IDs consistently.

New clients use the versioned `/api/v1/rooms` room, membership, and cursor-based
message-history endpoints documented in [API_v1.md](API_v1.md). The `/room/*`
routes remain a temporary compatibility layer.

## WebSocket

Connect to `/ws?token=<JWT>`. Send a string `roomId` in `join` before sending
`chat` for that room. See [WebSocket_Protocol.md](WebSocket_Protocol.md) for
frame limits, fragmentation and close behavior.
