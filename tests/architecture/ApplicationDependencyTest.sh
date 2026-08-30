#!/usr/bin/env bash
set -euo pipefail

router_file="${1:?expected ApplicationRouter.cpp path}"
if rg -q '^#include "(ChatManager|CryptoUtil|SqlConnPool)\.h"' "${router_file}"; then
  echo "ApplicationRouter must depend on application interfaces, not infrastructure headers" >&2
  exit 1
fi
