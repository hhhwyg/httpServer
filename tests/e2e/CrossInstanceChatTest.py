#!/usr/bin/env python3
"""Verify transactional Outbox fan-out reaches WebSockets on both instances."""

from pathlib import Path
import os
import subprocess
import sys
import time
import uuid

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "integration"))

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


def outbox_published(message_id):
    query = (
        "SELECT COUNT(*) FROM message_outbox "
        f"WHERE aggregate_id={message_id} AND published_at IS NOT NULL"
    )
    return subprocess.check_output(
        [*COMPOSE, "exec", "-T", "-e", f"MYSQL_PWD={DATABASE_PASSWORD}",
         "mysql", "mysql", "-N", "-uhttpserver", "webserver", "-e", query],
        text=True,
    ).strip() == "1"


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: CrossInstanceChatTest.py PORT_A PORT_B")
    port_a, port_b = map(int, sys.argv[1:])
    wait_for_server(port_a)
    wait_for_server(port_b)
    suffix = uuid.uuid4().hex[:12]
    alice_token = register_and_login(port_a, f"m5-alice-{suffix}")
    bob_token = register_and_login(port_b, f"m5-bob-{suffix}")
    status, room = request_json(port_a, "POST", "/api/v1/rooms", alice_token,
                                {"name": "M5 cross instance"})
    assert status == 201
    room_id = room["data"]["room"]["id"]
    status, joined = request_json(
        port_b, "POST", f"/api/v1/rooms/{room_id}/members", bob_token
    )
    assert status == 200 and joined["data"]["joined"] is True
    alice = connect_websocket(port_a, alice_token)
    bob = connect_websocket(port_b, bob_token)
    try:
        subscribe(alice, room_id, "subscribe-alice")
        subscribe(bob, room_id, "subscribe-bob")
        send_json(alice, {
            "version": 1, "type": "message.send", "requestId": "send",
            "payload": {"roomId": room_id, "clientMessageId": "m5-message",
                        "content": "cross instance delivery"},
        })
        ack = receive_json(alice)
        assert ack["type"] == "message.ack"
        message_id = ack["payload"]["messageId"]
        received_by_alice = receive_json(alice)
        received_by_bob = receive_json(bob)
        for message in (received_by_alice, received_by_bob):
            assert message["type"] == "message.created"
            assert message["payload"]["message"]["messageId"] == message_id
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and not outbox_published(message_id):
            time.sleep(0.1)
        assert outbox_published(message_id)
        print("cross_instance_chat_acceptance ack=1 broadcast_a=1 broadcast_b=1 outbox=published")
    finally:
        bob.close()
        alice.close()


if __name__ == "__main__":
    main()
