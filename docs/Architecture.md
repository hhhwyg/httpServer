# Architecture

The current migration follows this dependency direction:

```text
app (Main, Server)
  -> transport (EventLoop, Poller, Channel)
  -> protocol/application (HttpData, ApplicationRouter, ChatApplication)
  -> infrastructure (config, MySQL, logging)
```

`include/httpserver/` contains the public boundary. The current Phase 4 work publishes
`httpserver::EventLoop`, `httpserver::Channel`, `httpserver::Poller`,
`httpserver::PollerBackend`, `httpserver::PollerConfig`,
`httpserver::ServerConfig`, `httpserver::Logger`, and
`httpserver::LogStream`, along with protocol and application interfaces.
Implementation-only headers remain under `src/` and are not part of the
library's public include path.

Transport code must not depend on URI routing, JWT claims, SQL, or chat state.
Poller implementations keep connection lifetime through a type-erased owner and
receive timeout behavior through callbacks; they do not include or name
`HttpData`. The public `Poller` operations are `add`, `modify`, and `remove`.
Protocol and application code may use transport interfaces, but the reverse
dependency is prohibited. `ApplicationRouter` owns HTTP route dispatch and
route-level authentication responses; `OperationalStatus` is the only
application-facing adapter for readiness and infrastructure metrics.
`ChatApplication` owns room membership
and chat message use cases; it receives a callback for connection writes
instead of reaching into the session's output buffer.

`architecture.application_dependencies` is a lightweight regression test for
this rule: the HTTP router may include public application interfaces, but must
not include `ChatManager`, `SqlConnPool`, or `CryptoUtil` implementation headers.
