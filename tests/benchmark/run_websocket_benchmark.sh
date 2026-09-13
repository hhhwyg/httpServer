#!/usr/bin/env bash
# Preserve caller overrides and record the actual endpoint and workload.
set -euo pipefail

scenario="${1:?usage: run_websocket_benchmark.sh SCENARIO.env}"
[[ -f "$scenario" ]]
declare -A overrides=()
while IFS= read -r variable; do
  overrides["$variable"]="${!variable}"
done < <(compgen -v HTTPSERVER_BENCH_)
set -a
source "$scenario"
set +a
for variable in "${!overrides[@]}"; do
  printf -v "$variable" '%s' "${overrides[$variable]}"
  export "$variable"
done
: "${HTTPSERVER_BENCH_URL:?scenario must set HTTPSERVER_BENCH_URL}"
: "${HTTPSERVER_BENCH_DURATION_SECONDS:?scenario must set duration}"
workload="${HTTPSERVER_BENCH_WORKLOAD:-ping}"
case "$workload" in
  chat) ;;
  ping) : "${HTTPSERVER_BENCH_CONNECTIONS:?ping scenario must set connections}" ;;
  *) echo "Unsupported workload: $workload" >&2; exit 2 ;;
esac

results_dir="${HTTPSERVER_BENCH_RESULTS_DIR:-benchmarks}"
mkdir -p "$results_dir"
timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
result="$(mktemp "$results_dir/websocket-scenario-${timestamp}-XXXXXX.txt")"
resource_output="${result%.txt}.resources.jsonl"
transport_args=(--url "$HTTPSERVER_BENCH_URL" --duration "$HTTPSERVER_BENCH_DURATION_SECONDS"
  --timeout "${HTTPSERVER_BENCH_TIMEOUT_SECONDS:-8}"
  --server-pids "${HTTPSERVER_BENCH_SERVER_PIDS:-}"
  --resource-output "$resource_output" --sample-interval "${HTTPSERVER_BENCH_SAMPLE_INTERVAL_SECONDS:-5}")
[[ -z "${HTTPSERVER_BENCH_ORIGIN:-}" ]] || transport_args+=(--origin "$HTTPSERVER_BENCH_ORIGIN")
[[ -z "${HTTPSERVER_BENCH_CA_FILE:-}" ]] || transport_args+=(--ca-file "$HTTPSERVER_BENCH_CA_FILE")
[[ "${HTTPSERVER_BENCH_INSECURE:-0}" != "1" ]] || transport_args+=(--insecure)
{
  echo "scenario=$scenario"
  echo "timestamp_utc=$timestamp"
  echo "git_commit=$(git rev-parse HEAD 2>/dev/null || echo unknown)"
  echo "worktree_dirty=$(test -z "$(git status --porcelain 2>/dev/null)" && echo 0 || echo 1)"
  echo "hostname=$(hostname)"
  echo "kernel=$(uname -srmo)"
  echo "cpu_model=$(awk -F: '/model name/{sub(/^[[:space:]]+/, "", $2); print $2; exit}' /proc/cpuinfo)"
  echo "logical_cpus=$(getconf _NPROCESSORS_ONLN)"
  echo "memory_kib=$(awk '/MemTotal:/{print $2}' /proc/meminfo)"
  echo "fd_limit=$(ulimit -n)"
  echo "io_backend=${HTTPSERVER_BENCH_IO_BACKEND:-unknown}"
  echo "server_threads=${HTTPSERVER_BENCH_SERVER_THREADS:-unknown}"
  echo "server_build_type=${HTTPSERVER_BENCH_BUILD_TYPE:-unknown}"
  echo "workload=$workload"
  echo "fanout=${HTTPSERVER_BENCH_ROOM_FANOUT:-unknown}"
  echo "rooms=${HTTPSERVER_BENCH_ROOMS:-1}"
  echo "message_bytes=${HTTPSERVER_BENCH_MESSAGE_BYTES:-256}"
  echo "cross_instance=${HTTPSERVER_BENCH_CROSS_INSTANCE:-0}"
  echo "receiver_url=${HTTPSERVER_BENCH_RECEIVER_URL:-$HTTPSERVER_BENCH_URL}"
  echo "client_location=${HTTPSERVER_BENCH_CLIENT_LOCATION:-same-host}"
  echo "auth_mode=${HTTPSERVER_BENCH_AUTH_MODE:-api}"
  echo "resource_output=$resource_output"
} | tee "$result"
if [[ "$workload" == "chat" ]]; then
    if [[ "${HTTPSERVER_BENCH_CROSS_INSTANCE:-0}" == "1" ]]; then
      transport_args+=(--require-cross-instance)
    fi
    python3 "$(dirname "$0")/run_chat_benchmark.py" "${transport_args[@]}" \
      --receiver-url "${HTTPSERVER_BENCH_RECEIVER_URL:-$HTTPSERVER_BENCH_URL}" \
      --recipients "${HTTPSERVER_BENCH_ROOM_FANOUT:-1}" \
      --rooms "${HTTPSERVER_BENCH_ROOMS:-1}" \
      --message-bytes "${HTTPSERVER_BENCH_MESSAGE_BYTES:-256}" \
      --message-rate "${HTTPSERVER_BENCH_MESSAGE_RATE:-0}" \
      --auth-mode "${HTTPSERVER_BENCH_AUTH_MODE:-api}" 2>&1 | tee -a "$result"
else
    python3 "$(dirname "$0")/run_websocket_benchmark.py" "${transport_args[@]}" \
      --connections "$HTTPSERVER_BENCH_CONNECTIONS" 2>&1 | tee -a "$result"
fi
echo "Saved scenario baseline to $result"
