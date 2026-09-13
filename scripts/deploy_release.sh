#!/usr/bin/env bash
# Database migrations are forward-only; application rollback verifies readiness.
set -Eeuo pipefail
umask 077

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
compose_file="$repo_root/docker-compose.production.yml"
requested_image_tag="${IMAGE_TAG:-}"
if [[ -f "$repo_root/.env" ]]; then
  set -a
  # The server-owned 0600 file is an operational input, never committed.
  source "$repo_root/.env"
  set +a
fi
IMAGE_TAG="${requested_image_tag:-${IMAGE_TAG:-}}"
: "${IMAGE_TAG:?IMAGE_TAG must be an immutable commit SHA}"
: "${HTTPSERVER_IMAGE_REPOSITORY:?HTTPSERVER_IMAGE_REPOSITORY is required}"
: "${MYSQL_ROOT_PASSWORD:?MYSQL_ROOT_PASSWORD is required}"

if [[ ! "$IMAGE_TAG" =~ ^[0-9a-f]{40}$ ]]; then
  echo "IMAGE_TAG must be a 40-character lowercase commit SHA" >&2
  exit 2
fi

compose=(docker compose -f "$compose_file")
export HTTPSERVER_IMAGE="${HTTPSERVER_IMAGE_REPOSITORY}:${IMAGE_TAG}"
backup_dir="${HTTPSERVER_BACKUP_DIR:-$repo_root/backups}"
mkdir -p "$backup_dir"
exec 9>"$backup_dir/.deployment.lock"
if ! flock -n 9; then
  echo "Another deployment is already running" >&2
  exit 75
fi

declare -A previous_images=()
for service in webserver-a webserver-b; do
  previous_container="$("${compose[@]}" ps -a -q "$service")"
  if [[ -n "$previous_container" ]]; then
    # Store the local image ID, so re-tagging cannot change the rollback image.
    previous_images[$service]="$(docker inspect --format '{{.Image}}' "$previous_container")"
  fi
done

wait_ready() {
  local service=$1
  local attempt
  for ((attempt = 1; attempt <= 30; ++attempt)); do
    if "${compose[@]}" exec -T "$service" curl --fail --silent \
        --connect-timeout 2 --max-time 3 http://127.0.0.1:8088/readyz >/dev/null; then
      return 0
    fi
    sleep 2
  done
  echo "Readiness failed for $service" >&2
  return 1
}

reload_nginx() {
  "${compose[@]}" exec -T nginx nginx -t &&
    "${compose[@]}" exec -T nginx nginx -s reload
}

check_public_readiness() {
  if [[ -n "${HTTPSERVER_SMOKE_URL:-}" ]]; then
    curl --fail --silent --show-error --connect-timeout 5 --max-time 15 \
      "$HTTPSERVER_SMOKE_URL/readyz" >/dev/null
  fi
}

activation_started=0
backup_partial=""
rollback() {
  local exit_code=$1
  local service
  local restored=0
  local failed=0
  trap - ERR INT TERM
  if [[ -n "$backup_partial" && -f "$backup_partial" ]]; then
    rm -f -- "$backup_partial"
  fi
  if ((activation_started)); then
    for service in webserver-a webserver-b; do
      if [[ -n "${previous_images[$service]:-}" ]]; then
        echo "Restoring $service to ${previous_images[$service]}" >&2
        if HTTPSERVER_IMAGE="${previous_images[$service]}" "${compose[@]}" \
            up -d --no-deps --no-build "$service" && wait_ready "$service"; then
          restored=$((restored + 1))
        else
          failed=1
        fi
      else
        # On a first deployment there is no previous application to restore.
        "${compose[@]}" stop "$service" || failed=1
      fi
    done
    if ((restored > 0)); then
      reload_nginx || failed=1
      check_public_readiness || failed=1
    fi
    if ((failed)); then
      echo "rollback_acceptance status=failed; manual recovery required" >&2
    elif ((restored == 2)); then
      echo "rollback_acceptance status=passed instances=2" >&2
    else
      echo "rollback_acceptance status=unavailable restored_instances=$restored" >&2
    fi
  fi
  exit "$exit_code"
}
trap 'rollback $?' ERR
trap 'rollback 130' INT
trap 'rollback 143' TERM

"${compose[@]}" pull webserver-a webserver-b nginx prometheus grafana alertmanager node-exporter
"${compose[@]}" up -d mysql redis prometheus grafana alertmanager node-exporter
mysql_container="$("${compose[@]}" ps -q mysql)"
redis_container="$("${compose[@]}" ps -q redis)"
for ((attempt = 1; attempt <= 30; ++attempt)); do
  if [[ "$(docker inspect -f '{{.State.Health.Status}}' "$mysql_container")" == "healthy" ]] &&
     [[ "$(docker inspect -f '{{.State.Health.Status}}' "$redis_container")" == "healthy" ]]; then
    break
  fi
  sleep 2
done
[[ "$(docker inspect -f '{{.State.Health.Status}}' "$mysql_container")" == "healthy" ]]
[[ "$(docker inspect -f '{{.State.Health.Status}}' "$redis_container")" == "healthy" ]]

timestamp="$(date -u +%Y%m%dT%H%M%S%NZ)"
backup_file="$backup_dir/webserver-${timestamp}.sql.gz"
backup_partial="$(mktemp "$backup_file.partial.XXXXXX")"
"${compose[@]}" exec -T -e "MYSQL_PWD=$MYSQL_ROOT_PASSWORD" mysql \
  mysqldump --single-transaction --routines --triggers -uroot webserver | gzip > "$backup_partial"
gzip -t "$backup_partial"
mv -- "$backup_partial" "$backup_file"
backup_partial=""

source "$repo_root/scripts/lib/migrations.sh"
apply_migrations "$repo_root/db/migrations" \
  "${compose[@]}" exec -T -e "MYSQL_PWD=$MYSQL_ROOT_PASSWORD" mysql mysql -uroot webserver

activation_started=1
"${compose[@]}" up -d --no-deps --no-build webserver-a webserver-b nginx
for service in webserver-a webserver-b; do
  wait_ready "$service"
done
# Existing Nginx workers otherwise keep upstream IPs from the old containers.
reload_nginx

check_public_readiness
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
trap - ERR INT TERM
echo "deployment_acceptance image=$HTTPSERVER_IMAGE backup=$backup_file instances=2"
