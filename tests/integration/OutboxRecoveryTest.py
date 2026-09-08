#!/usr/bin/env python3
"""Verify Redis outage leaves Outbox durable and recovery drains it."""

from pathlib import Path
import os
import subprocess
import sys
import time
import uuid

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from ChatBusinessFlowTest import register_and_login, request_json, receive_json, send_json, subscribe  # noqa: E402
from WebSocketSmokeTest import connect_websocket, wait_for_server  # noqa: E402


ROOT = Path(__file__).resolve().parents[2]
COMPOSE_FILE = Path(os.environ.get("HTTPSERVER_TEST_COMPOSE_FILE",
                                   "docker-compose.test.yml"))
if not COMPOSE_FILE.is_absolute():
    COMPOSE_FILE = ROOT / COMPOSE_FILE
COMPOSE = ["docker", "compose", "-f", str(COMPOSE_FILE)]
DATABASE_PASSWORD = os.environ.get("HTTPSERVER_DB_PASSWORD",
                                   "test-database-password")


def mysql_scalar(query):
    return subprocess.check_output(
        [*COMPOSE, "exec", "-T", "-e", f"MYSQL_PWD={DATABASE_PASSWORD}",
         "mysql", "mysql", "-N", "-uhttpserver", "webserver", "-e", query],
        text=True,
    ).strip()


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: OutboxRecoveryTest.py PORT")
    port = int(sys.argv[1])
    wait_for_server(port)
    suffix = uuid.uuid4().hex[:12]
    token = register_and_login(port, f"m5-recovery-{suffix}")
    status, room = request_json(port, "POST", "/api/v1/rooms", token,
                                {"name": "M5 recovery"})
    assert status == 201
    room_id = room["data"]["room"]["id"]
    socket = connect_websocket(port, token)
    try:
        subscribe(socket, room_id, "subscribe")
        subprocess.run([*COMPOSE, "stop", "redis"], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        send_json(socket, {
            "version": 1, "type": "message.send", "requestId": "send",
            "payload": {"roomId": room_id, "clientMessageId": "outage-message",
                        "content": "persist before publish"},
        })
        ack = receive_json(socket)
        assert ack["type"] == "message.ack"
        message_id = ack["payload"]["messageId"]
        assert mysql_scalar(
            f"SELECT COUNT(*) FROM message_outbox WHERE aggregate_id={message_id} "
            "AND published_at IS NULL"
        ) == "1"
        subprocess.run([*COMPOSE, "up", "-d", "redis"], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            if mysql_scalar(
                f"SELECT COUNT(*) FROM message_outbox WHERE aggregate_id={message_id} "
                "AND published_at IS NOT NULL"
            ) == "1":
                print("outbox_recovery_acceptance persisted=1 retried=1 published=1")
                return
            time.sleep(0.2)
        raise AssertionError("Outbox event was not published after Redis recovery")
    finally:
        socket.close()
        subprocess.run([*COMPOSE, "up", "-d", "redis"], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


if __name__ == "__main__":
    main()
