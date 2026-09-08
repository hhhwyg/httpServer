#!/usr/bin/env bash
set -euo pipefail

scenario="${1:?usage: run_soak_test.sh SCENARIO.env}"
[[ -f "$scenario" ]]
if [[ "${HTTPSERVER_ALLOW_LONG_SOAK:-0}" != "1" ]]; then
  echo "Set HTTPSERVER_ALLOW_LONG_SOAK=1 to run the configured long soak." >&2
  exit 2
fi
"$(dirname "$0")/run_websocket_benchmark.sh" "$scenario"
