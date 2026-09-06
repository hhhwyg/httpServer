#!/usr/bin/env python3
"""Verify an ACK-lost retry persists and publishes one logical message."""

import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode = True
INTEGRATION_DIR = Path(__file__).resolve().parents[1] / "integration"
sys.path.insert(0, str(INTEGRATION_DIR))

from ChatBusinessFlowTest import (  # noqa: E402
    SECRET,
    receive_json,
    register_and_login,
    request_json,
    send_json,
    subscribe,
)
from WebSocketSmokeTest import connect_websocket, recv_frame, wait_for_server  # noqa: E402


def message_command(room_id, request_id):
    return {
        "version": 1,
        "type": "message.send",
        "requestId": request_id,
        "payload": {
            "roomId": room_id,
            "clientMessageId": "retry-after-ack-loss",
            "content": "one logical message",
        },
    }


def run_flow(port):
    alice_token = register_and_login(port, "m2-idempotency-alice")
    bob_token = register_and_login(port, "m2-idempotency-bob")
    status, response = request_json(
        port, "POST", "/api/v1/rooms", alice_token, {"name": "M2 retries"}
    )
    assert status == 201
    room_id = response["data"]["room"]["id"]
    status, _ = request_json(
        port, "POST", f"/api/v1/rooms/{room_id}/members", bob_token
    )
    assert status == 200

    receiver = connect_websocket(port, bob_token)
    first_sender = connect_websocket(port, alice_token)
    retry_sender = None
    try:
        subscribe(receiver, room_id, "receiver-subscribe")
        subscribe(first_sender, room_id, "first-subscribe")

        # The client writes but abandons the connection without consuming ACK.
        send_json(first_sender, message_command(room_id, "lost-ack-request"))
        time.sleep(0.1)
        first_sender.close()

        retry_sender = connect_websocket(port, alice_token)
        subscribe(retry_sender, room_id, "retry-subscribe")
        send_json(retry_sender, message_command(room_id, "retry-request"))
        ack = receive_json(retry_sender)
        assert ack["type"] == "message.ack"
        assert ack["payload"]["clientMessageId"] == "retry-after-ack-loss"

        published = receive_json(receiver)
        assert published["type"] == "message.created"
        message = published["payload"]["message"]
        assert message["messageId"] == ack["payload"]["messageId"]

        receiver.settimeout(0.3)
        try:
            recv_frame(receiver)
            raise AssertionError("retry must not publish a second message")
        except socket.timeout:
            pass

        status, history = request_json(
            port,
            "GET",
            f"/api/v1/rooms/{room_id}/messages?after=0&limit=100",
            bob_token,
        )
        assert status == 200
        assert history["data"]["messages"] == [message]
        print(
            "idempotency_acceptance "
            f"retries=2 persisted_message_records={len(history['data']['messages'])} "
            f"receiver_messages=1 message_ids={[message['messageId']]}"
        )
    finally:
        if retry_sender is not None:
            retry_sender.close()
        first_sender.close()
        receiver.close()


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: MessageIdempotencyTest.py SERVER PORT")
    server, port = sys.argv[1], int(sys.argv[2])
    log_file = tempfile.NamedTemporaryFile(prefix="httpserver-m2-idempotency-", delete=False)
    log_path = log_file.name
    log_file.close()
    environment = os.environ.copy()
    environment["HTTPSERVER_JWT_SECRET"] = SECRET
    environment["HTTPSERVER_AUTH_STORE"] = "memory"
    process = subprocess.Popen(
        [server, "-b", "epoll", "-t", "2", "-p", str(port), "-l", log_path],
        env=environment,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        wait_for_server(port)
        run_flow(port)
    finally:
        process.terminate()
        try:
            process.wait(timeout=4)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        try:
            os.unlink(log_path)
        except FileNotFoundError:
            pass


if __name__ == "__main__":
    main()
