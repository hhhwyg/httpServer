# ADR 0002: Public Module Boundaries

## Status

Accepted, migration in progress.

## Decision

Public headers are placed under `include/httpserver/<module>` and namespaced
as `httpserver`. Source-only implementation details remain under `src`.
During the migration, old include paths may forward to the public header, but
new code and tests must use the public path.

## Consequences

Consumers no longer need the complete source directory list to include a
public transport or configuration interface. Compatibility forwarding headers
temporarily preserve existing targets while modules are migrated one at a
time. They are tracked debt and must be removed before Phase 4 is complete.
