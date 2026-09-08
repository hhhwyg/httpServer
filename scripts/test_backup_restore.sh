#!/usr/bin/env bash
# Exercise migration, dump, and restore using isolated databases in test MySQL.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
compose_file="$repo_root/docker-compose.test.yml"
source_db="httpserver_m3_backup_source"
restore_db="httpserver_m3_backup_restore"
dump_file="$(mktemp /tmp/httpserver-m3-backup.XXXXXX.sql)"
cleanup() { rm -f "$dump_file"; }
trap cleanup EXIT

compose=(docker compose -f "$compose_file")
"${compose[@]}" up -d mysql >/dev/null
for _ in $(seq 1 30); do
  if "${compose[@]}" exec -T mysql mysqladmin ping -h localhost -uroot -ptest-root-password --silent; then
    break
  fi
  sleep 1
done
"${compose[@]}" exec -T mysql mysqladmin ping -h localhost -uroot -ptest-root-password --silent >/dev/null

"${compose[@]}" exec -T mysql mysql -uroot -ptest-root-password -e \
  "DROP DATABASE IF EXISTS ${source_db}; DROP DATABASE IF EXISTS ${restore_db}; CREATE DATABASE ${source_db} CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;"
"${compose[@]}" exec -T mysql mysql -uroot -ptest-root-password "$source_db" < "$repo_root/db/migrations/001_initial.sql"
# Applying the same migration again must retain existing rows and version state.
"${compose[@]}" exec -T mysql mysql -uroot -ptest-root-password "$source_db" < "$repo_root/db/migrations/001_initial.sql"
"${compose[@]}" exec -T mysql mysql -uroot -ptest-root-password "$source_db" -e "
  INSERT INTO users (username, password_hash) VALUES ('backup-user', 'hash');
  INSERT INTO rooms (name, owner_id) VALUES ('Backup room', 1);
  INSERT INTO room_members (room_id, user_id) VALUES (1, 1);
  INSERT INTO messages (room_id, sender_id, client_message_id, content) VALUES (1, 1, 'backup-message', 'durable');
  INSERT INTO message_outbox (event_id, aggregate_id, event_type, payload) VALUES ('00000000-0000-4000-8000-000000000003', 1, 'message.created', JSON_OBJECT('messageId', 1));"

before="$("${compose[@]}" exec -T mysql mysql -N -B -uroot -ptest-root-password "$source_db" -e "SELECT (SELECT COUNT(*) FROM users), (SELECT COUNT(*) FROM rooms), (SELECT COUNT(*) FROM room_members), (SELECT COUNT(*) FROM messages), (SELECT COUNT(*) FROM message_outbox), (SELECT COUNT(*) FROM schema_migrations);")"
"${compose[@]}" exec -T mysql mysqldump -uroot -ptest-root-password --single-transaction --routines --triggers "$source_db" > "$dump_file"
"${compose[@]}" exec -T mysql mysql -uroot -ptest-root-password -e "CREATE DATABASE ${restore_db} CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;"
"${compose[@]}" exec -T mysql mysql -uroot -ptest-root-password "$restore_db" < "$dump_file"
after="$("${compose[@]}" exec -T mysql mysql -N -B -uroot -ptest-root-password "$restore_db" -e "SELECT (SELECT COUNT(*) FROM users), (SELECT COUNT(*) FROM rooms), (SELECT COUNT(*) FROM room_members), (SELECT COUNT(*) FROM messages), (SELECT COUNT(*) FROM message_outbox), (SELECT COUNT(*) FROM schema_migrations);")"

test "$before" = "$after"
echo "backup_restore_acceptance rows=$after"
