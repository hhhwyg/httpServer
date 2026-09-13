#!/usr/bin/env bash

apply_migrations() {
  local migration_dir=$1
  shift
  local migration version applied
  local LC_ALL=C
  local mysql_command=("$@")

  "${mysql_command[@]}" -e 'CREATE TABLE IF NOT EXISTS schema_migrations (
    version VARCHAR(64) PRIMARY KEY,
    applied_at TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
  ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;'

  for migration in "$migration_dir"/*.sql; do
    [[ -f "$migration" ]] || continue
    version="${migration##*/}"
    version="${version%.sql}"
    if [[ ! "$version" =~ ^[0-9]{3}_[a-zA-Z0-9_]+$ || ${#version} -gt 64 ]]; then
      echo "Invalid migration filename: $migration" >&2
      return 2
    fi
    applied="$("${mysql_command[@]}" -N -B -e \
      "SELECT COUNT(*) FROM schema_migrations WHERE version = '$version';")" || return "$?"
    if [[ "$applied" == "1" ]]; then
      echo "migration_acceptance version=$version status=already_applied"
      continue
    fi
    if [[ "$applied" != "0" ]]; then
      echo "Invalid migration state for $version: $applied" >&2
      return 1
    fi
    # MySQL DDL may auto-commit. A failed migration needs forward repair.
    if ! "${mysql_command[@]}" < "$migration"; then
      echo "Migration $version failed; inspect partial DDL before retrying" >&2
      return 1
    fi
    "${mysql_command[@]}" -e "INSERT IGNORE INTO schema_migrations (version) VALUES ('$version');"
    echo "migration_acceptance version=$version status=applied"
  done
}
