#!/usr/bin/env bash
# Run deterministic local resilience checks. Toxiproxy is opt-in because it
# needs the dedicated chaos Compose stack and real MySQL/Redis dependencies.
set -euo pipefail

build_dir="${1:-build/debug}"
ctest --test-dir "$build_dir" -R 'resilience.slow_consumer|resilience.graceful_drain|integration.graceful_shutdown' --output-on-failure

if [[ "${HTTPSERVER_RUN_TOXIPROXY:-0}" == "1" ]]; then
  : "${HTTPSERVER_CHAOS_SERVER_PORT:?set chaos server port}"
  : "${HTTPSERVER_TOXIPROXY_PORT:?set toxiproxy API port}"
  python3 tests/resilience/DependencyLatencyTest.py \
    "$HTTPSERVER_CHAOS_SERVER_PORT" "$HTTPSERVER_TOXIPROXY_PORT"
fi

echo "fault_matrix_acceptance lifecycle=passed toxiproxy=${HTTPSERVER_RUN_TOXIPROXY:-0}"
