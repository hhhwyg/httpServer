# Module Guide

| Module | Public boundary | Responsibility |
| --- | --- | --- |
| `transport` | `include/httpserver/transport` | Event loops, pollers, channels and socket-facing ownership |
| `protocol` | `include/httpserver/protocol` | HTTP and WebSocket parsing/serialization |
| `application` | `include/httpserver/application` | Routing and authentication use cases |
| `infrastructure` | `include/httpserver/config` and planned repository interfaces | Configuration, persistence, logging and metrics |

The stable logging boundary is `include/httpserver/base/Logging.h` and
`include/httpserver/base/LogStream.h`.

The first protocol boundary is `include/httpserver/protocol/MimeType.h`.
`HttpData` uses this query interface without owning the MIME table or its
initialization state.

Request-line, header, and query parsing now live behind
`include/httpserver/protocol/HttpRequest.h`; the connection session only
translates parser results into its existing state machine.

HTTP status-line and response-header serialization live behind
`include/httpserver/protocol/HttpResponse.h`; `HttpData` only appends the
serialized head/body to its transport buffer and schedules the write.

Static file path validation, size limits, loading, and MIME selection live
behind `include/httpserver/protocol/StaticFile.h`; `HttpData` only maps the
result to an HTTP response and schedules the write.

WebSocket frame masking, length validation, decoding, and encoding live behind
`include/httpserver/protocol/WebSocket.h`; `HttpData` retains only connection
policy, fragmentation state, and chat message dispatch.

Token extraction and JWT verification live behind
`include/httpserver/application/Authentication.h`; `HttpData` only asks for a
verified subject when a route or handshake requires authentication.

HTTP application routes live behind
`include/httpserver/application/ApplicationRouter.h`. The router delegates
business operations to the public `UserController` and `RoomController`
application interfaces and owns route-level authentication responses.
`HttpData` keeps only the WebSocket handshake and transport-specific fallback
branches.

Chat message validation and room membership operations live behind
`include/httpserver/application/ChatApplication.h`. The connection session
passes decoded message payloads in and supplies a callback for outbound
messages; room cleanup is likewise delegated when the session closes.

The public transport slices expose event-loop ownership, poller selection,
channels, and server capacity configuration through
`include/httpserver/transport`. The former forwarding headers under `src/`
were removed after the in-repository consumers migrated.

`src/epoll/` and `src/eventloop/EventLoopThreadPool.*` are active transport
implementations. The legacy `src/thread/ThreadPool.*` is retained as an
independent experiment but is excluded from `httpserver_core`; it has no
runtime or test consumers. The scripts under `scripts/` are one-off source
rewriting tools and are not part of the server build.
