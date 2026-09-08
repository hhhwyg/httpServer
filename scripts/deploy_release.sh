#!/usr/bin/env bash
# Deploy one immutable image with backup, migration, readiness checks and
# best-effort application rollback. Database migrations are forward-only.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
compose_file="$repo_root/docker-compose.production.yml"
backup_dir="${HTTPSERVER_BACKUP_DIR:-$repo_root/backups}"
if [[ -f "$repo_root/.env" ]]; then
  set -a
  # The server-owned 0600 file is an operational input, never committed.
  source "$repo_root/.env"
  set +a
fi
: "${IMAGE_TAG:?IMAGE_TAG must be an immutable commit SHA}"
: "${HTTPSERVER_IMAGE_REPOSITORY:?HTTPSERVER_IMAGE_REPOSITORY is required}"
: "${MYSQL_ROOT_PASSWORD:?MYSQL_ROOT_PASSWORD is required}"

if [[ ! "$IMAGE_TAG" =~ ^[0-9a-f]{40}$ ]]; then
  echo "IMAGE_TAG must be a 40-character lowercase commit SHA" >&2
  exit 2
fi

compose=(docker compose -f "$compose_file")
export HTTPSERVER_IMAGE="${HTTPSERVER_IMAGE_REPOSITORY}:${IMAGE_TAG}"
mkdir -p "$backup_dir"

previous_image=""
previous_container="$("${compose[@]}" ps -q webserver-a)"
if [[ -n "$previous_container" ]]; then
  previous_image="$(docker inspect --format '{{.Config.Image}}' "$previous_container")"
fi

rollback() {
  local exit_code=$?
  if [[ -n "$previous_image" ]]; then
    echo "Deployment failed; restoring application image $previous_image" >&2
    HTTPSERVER_IMAGE="$previous_image" "${compose[@]}" up -d --no-deps \
      webserver-a webserver-b nginx || true
  fi
  exit "$exit_code"
}
trap rollback ERR

"${compose[@]}" pull webserver-a webserver-b nginx prometheus grafana alertmanager node-exporter
"${compose[@]}" up -d mysql redis prometheus grafana alertmanager node-exporter
mysql_container="$("${compose[@]}" ps -q mysql)"
redis_container="$("${compose[@]}" ps -q redis)"
for _ in $(seq 1 30); do
  if [[ "$(docker inspect -f '{{.State.Health.Status}}' "$mysql_container")" == "healthy" ]] &&
     [[ "$(docker inspect -f '{{.State.Health.Status}}' "$redis_container")" == "healthy" ]]; then
    break
  fi
  sleep 2
done
[[ "$(docker inspect -f '{{.State.Health.Status}}' "$mysql_container")" == "healthy" ]]
[[ "$(docker inspect -f '{{.State.Health.Status}}' "$redis_container")" == "healthy" ]]

timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
backup_file="$backup_dir/webserver-${timestamp}.sql.gz"
"${compose[@]}" exec -T -e "MYSQL_PWD=$MYSQL_ROOT_PASSWORD" mysql \
  mysqldump --single-transaction --routines --triggers -uroot webserver | gzip > "$backup_file"

for migration in "$repo_root"/db/migrations/*.sql; do
  [[ -f "$migration" ]] || continue
  "${compose[@]}" exec -T -e "MYSQL_PWD=$MYSQL_ROOT_PASSWORD" mysql \
    mysql -uroot webserver < "$migration"
done

"${compose[@]}" up -d --no-deps webserver-a webserver-b nginx
for service in webserver-a webserver-b; do
  for _ in $(seq 1 30); do
    if "${compose[@]}" exec -T "$service" \
         curl --fail --silent http://127.0.0.1:8088/readyz >/dev/null; then
      break
    fi
    sleep 2
  done
  "${compose[@]}" exec -T "$service" \
    curl --fail --silent http://127.0.0.1:8088/readyz >/dev/null
done
"${compose[@]}" exec -T nginx nginx -t

if [[ -n "${HTTPSERVER_SMOKE_URL:-}" ]]; then
  curl --fail --silent --show-error "$HTTPSERVER_SMOKE_URL/healthz" >/dev/null
fi
if [[ -n "${HTTPSERVER_PUBLIC_WSS_URL:-}" ]]; then
  public_wss_args=("$HTTPSERVER_PUBLIC_WSS_URL")
  if [[ -n "${HTTPSERVER_PUBLIC_ORIGIN:-}" ]]; then
    public_wss_args+=(--origin "$HTTPSERVER_PUBLIC_ORIGIN")
  fi
  if [[ -n "${HTTPSERVER_PUBLIC_CA_FILE:-}" ]]; then
    public_wss_args+=(--ca-file "$HTTPSERVER_PUBLIC_CA_FILE")
  fi
  python3 "$repo_root/tests/e2e/PublicWssSmokeTest.py" "${public_wss_args[@]}"
fi
trap - ERR
echo "deployment_acceptance image=$HTTPSERVER_IMAGE backup=$backup_file instances=2"
