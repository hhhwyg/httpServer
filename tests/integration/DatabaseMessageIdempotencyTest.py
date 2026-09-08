#!/usr/bin/env python3
"""Verify concurrent retries create one durable message row."""

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


def receive_ack(sock, request_id):
    for _ in range(4):
        frame = receive_json(sock)
        if frame["type"] == "message.ack" and frame["requestId"] == request_id:
            return frame
    raise AssertionError("message acknowledgement was not received")


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: DatabaseMessageIdempotencyTest.py SERVER PORT")
    server, port = sys.argv[1], int(sys.argv[2])
    log_file = tempfile.NamedTemporaryFile(prefix="httpserver-m3-idempotency-", delete=False)
    log_file.close()
    process = subprocess.Popen(
        [server, "-b", "epoll", "-t", "2", "-p", str(port), "-l", log_file.name],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        wait_for_server(port)
        username = f"m3-idempotency-{os.getpid()}"
        token = register_and_login(port, username)
        status, response = request_json(
            port, "POST", "/api/v1/rooms", token, {"name": "M3 idempotency"}
        )
        assert status == 201
        room_id = response["data"]["room"]["id"]
        first = connect_websocket(port, token)
        second = connect_websocket(port, token)
        try:
            subscribe(first, room_id, "idempotency-subscribe-1")
            subscribe(second, room_id, "idempotency-subscribe-2")
            command = {
                "version": 1,
                "type": "message.send",
                "requestId": "concurrent-retry",
                "payload": {
                    "roomId": room_id,
                    "clientMessageId": "one-durable-row",
                    "content": "deduplicate in mysql",
                },
            }
            send_json(first, command)
            send_json(second, command)
            first_ack = receive_ack(first, "concurrent-retry")
            second_ack = receive_ack(second, "concurrent-retry")
            assert first_ack["payload"] == second_ack["payload"]
        finally:
            second.close()
            first.close()
        status, history = request_json(
            port, "GET", f"/api/v1/rooms/{room_id}/messages?after=0", token
        )
        assert status == 200
        assert len(history["data"]["messages"]) == 1
        print("database_message_idempotency_acceptance durable_rows=1")
    finally:
        process.terminate()
        try:
            process.wait(timeout=4)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        os.unlink(log_file.name)


if __name__ == "__main__":
    main()
