#!/usr/bin/env python3
"""Verify a failed outbox insert rolls back its message insert."""

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


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: TransactionRollbackTest.py SERVER PORT")
    server, port = sys.argv[1], int(sys.argv[2])
    log_file = tempfile.NamedTemporaryFile(prefix="httpserver-m3-rollback-", delete=False)
    log_file.close()
    environment = os.environ.copy()
    environment["HTTPSERVER_TEST_FAIL_OUTBOX_INSERT"] = "1"
    process = subprocess.Popen(
        [server, "-b", "epoll", "-t", "2", "-p", str(port), "-l", log_file.name],
        env=environment,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        wait_for_server(port)
        username = f"m3-rollback-{os.getpid()}"
        token = register_and_login(port, username)
        status, response = request_json(
            port, "POST", "/api/v1/rooms", token, {"name": "M3 rollback"}
        )
        assert status == 201
        room_id = response["data"]["room"]["id"]
        sender = connect_websocket(port, token)
        try:
            subscribe(sender, room_id, "rollback-subscribe")
            send_json(
                sender,
                {
                    "version": 1,
                    "type": "message.send",
                    "requestId": "rollback-send",
                    "payload": {
                        "roomId": room_id,
                        "clientMessageId": "rollback-message",
                        "content": "must roll back",
                    },
                },
            )
            error = receive_json(sender)
            assert error["type"] == "error"
            assert error["error"]["code"] == "dependency_unavailable"
        finally:
            sender.close()
        status, history = request_json(
            port, "GET", f"/api/v1/rooms/{room_id}/messages?after=0", token
        )
        assert status == 200 and history["data"]["messages"] == []
        print("transaction_rollback_acceptance messages=0 outbox=0")
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
