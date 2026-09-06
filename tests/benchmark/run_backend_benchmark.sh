#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 3 || $# -gt 5 ]]; then
  echo "usage: $0 SERVER BACKEND PORT [RESULTS_DIR] [REQUEST_PATH]" >&2
  exit 2
fi

server="$1"
backend="$2"
port="$3"
results_dir="${4:-benchmarks}"
request_path="${5:-/ping}"
duration="${HTTPSERVER_BENCH_DURATION:-30s}"
threads="${HTTPSERVER_BENCH_THREADS:-4}"
connections="${HTTPSERVER_BENCH_CONNECTIONS:-128}"
server_threads="${HTTPSERVER_BENCH_SERVER_THREADS:-1}"
benchmark_modes="${HTTPSERVER_BENCH_MODES:-keep-alive close}"
log_file="$(mktemp "${TMPDIR:-/tmp}/httpserver-${backend}.XXXXXX.log")"
server_pid=""

cleanup() {
  if [[ -n "${server_pid}" ]] && kill -0 "${server_pid}" 2>/dev/null; then
    kill -TERM "${server_pid}" 2>/dev/null || true
    wait "${server_pid}" 2>/dev/null || true
  fi
  rm -f "${log_file}"
}
trap cleanup EXIT

if [[ "${request_path}" != /* ]]; then
  echo "REQUEST_PATH must start with /" >&2
  exit 2
fi

"${server}" -b "${backend}" -t "${server_threads}" -p "${port}" -l "${log_file}" \
  >/dev/null 2>&1 &
server_pid="$!"

for _ in $(seq 1 80); do
  if curl --silent --max-time 1 "http://127.0.0.1:${port}/ping" | grep -qx 'OK'; then
    break
  fi
  sleep 0.1
done
curl --silent --max-time 2 "http://127.0.0.1:${port}/ping" | grep -qx 'OK'
request_url="http://127.0.0.1:${port}${request_path}"

for connection_mode in ${benchmark_modes}; do
  HTTPSERVER_BENCH_RESULTS_DIR="${results_dir}" \
  HTTPSERVER_BENCH_THREADS="${threads}" \
  HTTPSERVER_BENCH_CONNECTIONS="${connections}" \
  HTTPSERVER_BENCH_SERVER_THREADS="${server_threads}" \
  HTTPSERVER_BENCH_DURATION="${duration}" \
  HTTPSERVER_BENCH_CONNECTION_MODE="${connection_mode}" \
  HTTPSERVER_BENCH_SERVER_PID="${server_pid}" \
    "$(dirname "$0")/run_http_benchmark.sh" \
      "${request_url}"
done
