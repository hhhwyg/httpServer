# Coding Style

- C++ standard: C++17.
- Public interfaces live below `include/httpserver/` and use the
  `httpserver::` namespace.
- Implementation-only headers remain below `src/`.
- Do not add `using namespace std` to headers. Prefer explicit standard-library
  names and include every type used by a header directly.
- Keep ownership expressed by RAII and smart pointers. Comments explain an
  invariant or a platform constraint, not the syntax of the code.
- New module boundaries require a focused unit or component test before old
  compatibility includes are removed.
