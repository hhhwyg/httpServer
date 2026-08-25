# Architecture

The current migration follows this dependency direction:

```text
app (Main, Server)
  -> transport (EventLoop, Poller, Channel)
  -> protocol/application (HttpData, ApplicationRouter, ChatApplication)
  -> infrastructure (config, MySQL, logging)
```

`include/httpserver/` contains the public boundary. Phase 4 publishes
`httpserver::Channel`, `httpserver::Poller`,
`httpserver::PollerBackend`, `httpserver::PollerConfig`,
`httpserver::ServerConfig`, `httpserver::Logger`, and
`httpserver::LogStream`, along with protocol and application interfaces.
Implementation-only headers remain under `src/` and are not part of the
library's public include path.

Transport code must not depend on URI routing, JWT claims, SQL, or chat state.
Protocol and application code may use transport interfaces, but the reverse
dependency is prohibited. `ApplicationRouter` owns HTTP route dispatch and
route-level authentication responses. `ChatApplication` owns room membership
and chat message use cases; it receives a callback for connection writes
instead of reaching into the session's output buffer.
