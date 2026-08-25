#!/usr/bin/env bash
set -euo pipefail

server="$1"
port="$2"
base_url="http://127.0.0.1:${port}"
log_file="$(mktemp)"
server_pid=""

cleanup() {
  if [[ -n "$server_pid" ]] && kill -0 "$server_pid" 2>/dev/null; then
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
  fi
  rm -f "$log_file"
}
trap cleanup EXIT

"$server" -b epoll -t 1 -p "$port" -l "$log_file" >/dev/null 2>&1 &
server_pid="$!"

for _ in $(seq 1 40); do
  if curl --silent --show-error --max-time 1 "$base_url/ping" | grep -qx 'OK'; then
    break
  fi
  sleep 0.1
done

test "$(curl --silent --show-error --max-time 2 "$base_url/ping")" = 'OK'
[[ "$(curl --silent --show-error --max-time 2 "$base_url/")" == *'<title>'* ]]

head_headers="$(curl --silent --show-error --max-time 2 --head "$base_url/ping")"
grep -qi '^Content-Length: 2' <<<"$head_headers"

traversal_status="$(curl --silent --output /dev/null --write-out '%{http_code}' \
  --path-as-is --max-time 2 "$base_url/../CMakeLists.txt")"
test "$traversal_status" = '400'

room_status="$(curl --silent --output /dev/null --write-out '%{http_code}' \
  --max-time 2 "$base_url/room/list")"
test "$room_status" = '401'
