#!/usr/bin/env bash
# Apply ordered migrations from a deployment job, never from a running server.
set -Eeuo pipefail

: "${HTTPSERVER_DB_HOST:?HTTPSERVER_DB_HOST is required}"
: "${HTTPSERVER_DB_USER:?HTTPSERVER_DB_USER is required}"
: "${HTTPSERVER_DB_PASSWORD:?HTTPSERVER_DB_PASSWORD is required}"
: "${HTTPSERVER_DB_NAME:?HTTPSERVER_DB_NAME is required}"

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
db_port="${HTTPSERVER_DB_PORT:-3306}"
export MYSQL_PWD="$HTTPSERVER_DB_PASSWORD"

source "$repo_root/scripts/lib/migrations.sh"
apply_migrations "$repo_root/db/migrations" \
  mysql --protocol=TCP --host="$HTTPSERVER_DB_HOST" --port="$db_port" \
  --user="$HTTPSERVER_DB_USER" "$HTTPSERVER_DB_NAME"
