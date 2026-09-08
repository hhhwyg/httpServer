#!/usr/bin/env python3
"""Verify rooms, memberships, and messages survive a server restart."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from ChatBusinessFlowTest import (  # noqa: E402
    receive_json,
    register_and_login,
    request_json,
    send_json,
    subscribe,
)
from WebSocketSmokeTest import connect_websocket, wait_for_server  # noqa: E402


def start(server, port, log_path):
    return subprocess.Popen(
        [server, "-b", "epoll", "-t", "2", "-p", str(port), "-l", log_path],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def stop(process):
    process.terminate()
    try:
        process.wait(timeout=4)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: PersistenceRestartTest.py SERVER PORT")
    server, port = sys.argv[1], int(sys.argv[2])
    log_file = tempfile.NamedTemporaryFile(prefix="httpserver-m3-restart-", delete=False)
    log_file.close()
    first = start(server, port, log_file.name)
    try:
        wait_for_server(port)
        alice_token = register_and_login(port, f"m3-restart-alice-{os.getpid()}")
        bob_token = register_and_login(port, f"m3-restart-bob-{os.getpid()}")
        status, response = request_json(
            port, "POST", "/api/v1/rooms", alice_token, {"name": "M3 restart"}
        )
        assert status == 201
        room_id = response["data"]["room"]["id"]
        assert request_json(port, "POST", f"/api/v1/rooms/{room_id}/members", bob_token)[0] == 200
        sender = connect_websocket(port, alice_token)
        try:
            subscribe(sender, room_id, "subscribe-before-restart")
            send_json(sender, {"version": 1, "type": "message.send", "requestId": "m3-send", "payload": {"roomId": room_id, "clientMessageId": "m3-restart-message", "content": "durable"}})
            ack = receive_json(sender)
            assert ack["type"] == "message.ack"
            message_id = ack["payload"]["messageId"]
        finally:
            sender.close()
    finally:
        stop(first)

    second = start(server, port, log_file.name)
    try:
        wait_for_server(port)
        status, history = request_json(port, "GET", f"/api/v1/rooms/{room_id}/messages?after=0&limit=100", bob_token)
        assert status == 200
        assert [message["messageId"] for message in history["data"]["messages"]] == [message_id]
        print(f"persistence_restart_acceptance room_id={room_id} message_id={message_id} records=1")
    finally:
        stop(second)
        os.unlink(log_file.name)


if __name__ == "__main__":
    main()
