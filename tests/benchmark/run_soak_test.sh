#!/usr/bin/env bash
set -euo pipefail

scenario="${1:?usage: run_soak_test.sh SCENARIO.env}"
[[ -f "$scenario" ]]
if [[ "${HTTPSERVER_ALLOW_LONG_SOAK:-0}" != "1" ]]; then
  echo "Set HTTPSERVER_ALLOW_LONG_SOAK=1 to run the configured long soak." >&2
  exit 2
fi
# For remote clients set URL/receiver URL to the actual target and record
# HTTPSERVER_BENCH_CLIENT_LOCATION=separate-host. Local server PID sampling is
# enabled with HTTPSERVER_BENCH_SERVER_PIDS; collect remote Prometheus separately.
"$(dirname "$0")/run_websocket_benchmark.sh" "$scenario"
