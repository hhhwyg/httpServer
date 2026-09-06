#!/usr/bin/env bash
set -euo pipefail

server_url="${1:-http://127.0.0.1:8088/ping}"
threads="${HTTPSERVER_BENCH_THREADS:-4}"
connections="${HTTPSERVER_BENCH_CONNECTIONS:-128}"
server_threads="${HTTPSERVER_BENCH_SERVER_THREADS:-unknown}"
duration="${HTTPSERVER_BENCH_DURATION:-30s}"
connection_mode="${HTTPSERVER_BENCH_CONNECTION_MODE:-keep-alive}"
results_dir="${HTTPSERVER_BENCH_RESULTS_DIR:-benchmarks}"
timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
result_file="${results_dir}/http-${timestamp}.txt"

mkdir -p "${results_dir}"
{
  echo "timestamp_utc=${timestamp}"
  echo "url=${server_url}"
  echo "threads=${threads}"
  echo "connections=${connections}"
  echo "server_threads=${server_threads}"
  echo "duration=${duration}"
  echo "connection_mode=${connection_mode}"
  uname -a
  lscpu || true
  ulimit -n
  if command -v wrk >/dev/null 2>&1 && [[ "${connection_mode}" == "keep-alive" ]]; then
    echo "tool=wrk"
    wrk --version
    echo
    wrk -t "${threads}" -c "${connections}" -d "${duration}" --latency "${server_url}"
  else
    python3 "$(dirname "$0")/run_http_benchmark.py" \
      --url "${server_url}" \
      --duration "${duration%s}" \
      --threads "${threads}" \
      --connections "${connections}" \
      --connection-mode "${connection_mode}" \
      ${HTTPSERVER_BENCH_SERVER_PID:+--server-pid "${HTTPSERVER_BENCH_SERVER_PID}"}
  fi
} | tee "${result_file}"

echo "Saved raw benchmark output to ${result_file}"
