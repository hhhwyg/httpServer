#!/usr/bin/env bash
set -euo pipefail

mode="${1:-debug}"
case "${mode}" in
  debug|asan|tsan) ;;
  *)
    echo "usage: $0 [debug|asan|tsan]" >&2
    exit 2
    ;;
esac

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "This validation script must run on Linux or WSL." >&2
  exit 2
fi

cmake --preset "${mode}"
cmake --build --preset "${mode}" --parallel
ctest --preset "${mode}"

if [[ "${HTTPSERVER_VALIDATE_DATABASE:-0}" == "1" ]]; then
  docker compose up --build --abort-on-container-exit --exit-code-from builder builder mysql
fi
