#!/usr/bin/env bash
# Records a reproducible transport baseline alongside a named WebSocket
# scenario. Business fan-out correctness remains covered by E2E message IDs.
set -euo pipefail

scenario="${1:?usage: run_websocket_benchmark.sh SCENARIO.env}"
[[ -f "$scenario" ]]
url_override="${HTTPSERVER_BENCH_URL:-}"
duration_override="${HTTPSERVER_BENCH_DURATION_SECONDS:-}"
connections_override="${HTTPSERVER_BENCH_CONNECTIONS:-}"
fanout_override="${HTTPSERVER_BENCH_ROOM_FANOUT:-}"
set -a
source "$scenario"
set +a
[[ -z "$url_override" ]] || HTTPSERVER_BENCH_URL="$url_override"
[[ -z "$duration_override" ]] || HTTPSERVER_BENCH_DURATION_SECONDS="$duration_override"
[[ -z "$connections_override" ]] || HTTPSERVER_BENCH_CONNECTIONS="$connections_override"
[[ -z "$fanout_override" ]] || HTTPSERVER_BENCH_ROOM_FANOUT="$fanout_override"
: "${HTTPSERVER_BENCH_URL:?scenario must set HTTPSERVER_BENCH_URL}"
: "${HTTPSERVER_BENCH_DURATION_SECONDS:?scenario must set duration}"
: "${HTTPSERVER_BENCH_CONNECTIONS:?scenario must set connections}"
: "${HTTPSERVER_BENCH_JWT_SECRET:?set the server JWT secret for the benchmark}"

results_dir="${HTTPSERVER_BENCH_RESULTS_DIR:-benchmarks}"
mkdir -p "$results_dir"
timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
result="$results_dir/websocket-scenario-${timestamp}.txt"
{
  echo "scenario=$scenario"
  echo "timestamp_utc=$timestamp"
  echo "fanout=${HTTPSERVER_BENCH_ROOM_FANOUT:-unknown}"
  echo "cross_instance=${HTTPSERVER_BENCH_CROSS_INSTANCE:-0}"
  if [[ "${HTTPSERVER_BENCH_WORKLOAD:-ping}" == "chat" ]]; then
    benchmark_port="${HTTPSERVER_BENCH_URL##*:}"
    benchmark_port="${benchmark_port%%/*}"
    python3 "$(dirname "$0")/run_chat_benchmark.py" --port "$benchmark_port" \
      --duration "$HTTPSERVER_BENCH_DURATION_SECONDS" --jwt-secret "$HTTPSERVER_BENCH_JWT_SECRET" \
      --recipients "${HTTPSERVER_BENCH_ROOM_FANOUT:-1}"
  else
    python3 "$(dirname "$0")/run_websocket_benchmark.py" \
      --url "$HTTPSERVER_BENCH_URL" --duration "$HTTPSERVER_BENCH_DURATION_SECONDS" \
      --connections "$HTTPSERVER_BENCH_CONNECTIONS" --jwt-secret "$HTTPSERVER_BENCH_JWT_SECRET"
  fi
} | tee "$result"
echo "Saved scenario baseline to $result"
