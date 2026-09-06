#!/usr/bin/env bash
set -euo pipefail

router_file="${1:?expected ApplicationRouter.cpp path}"
domain_dir="${2:?expected public domain directory}"
chat_service_file="${3:?expected ChatService.cpp path}"

search_files() {
  local pattern="$1"
  shift
  if command -v rg >/dev/null 2>&1; then
    rg -q "${pattern}" "$@"
  else
    grep -R -E -q "${pattern}" "$@"
  fi
}

if search_files '^#include "(ChatManager|CryptoUtil|SqlConnPool)\.h"' \
    "${router_file}"; then
  echo "ApplicationRouter must depend on application interfaces, not infrastructure headers" >&2
  exit 1
fi

if search_files '#include ".*(application|transport|HttpData|Poller|SqlConnPool|CryptoUtil|mysql)' \
    "${domain_dir}"; then
  echo "Domain headers must not depend on application, transport or infrastructure" >&2
  exit 1
fi

if search_files '#include "(HttpData|Poller|SqlConnPool|CryptoUtil)\.h"' \
    "${chat_service_file}"; then
  echo "ChatService must not depend on transport or infrastructure implementations" >&2
  exit 1
fi
