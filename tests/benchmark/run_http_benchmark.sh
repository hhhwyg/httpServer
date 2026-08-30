#!/usr/bin/env bash
set -euo pipefail

if ! command -v wrk >/dev/null 2>&1; then
  echo "wrk is required: install it with your Linux package manager" >&2
  exit 2
fi

server_url="${1:-http://127.0.0.1:8088/ping}"
threads="${HTTPSERVER_BENCH_THREADS:-4}"
connections="${HTTPSERVER_BENCH_CONNECTIONS:-128}"
duration="${HTTPSERVER_BENCH_DURATION:-30s}"
results_dir="${HTTPSERVER_BENCH_RESULTS_DIR:-benchmarks}"
timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
result_file="${results_dir}/http-${timestamp}.txt"

mkdir -p "${results_dir}"
{
  echo "timestamp_utc=${timestamp}"
  echo "url=${server_url}"
  echo "threads=${threads}"
  echo "connections=${connections}"
  echo "duration=${duration}"
  uname -a
  lscpu || true
  ulimit -n
  wrk --version
  echo
  wrk -t "${threads}" -c "${connections}" -d "${duration}" --latency "${server_url}"
} | tee "${result_file}"

echo "Saved raw benchmark output to ${result_file}"
