#!/usr/bin/env python3
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
INTEGRATION_DIR = Path(__file__).resolve().parents[1] / "integration"
sys.path.insert(0, str(INTEGRATION_DIR))

from WebSocketSmokeTest import (  # noqa: E402
    connect_websocket,
    recv_frame,
    recv_http_response,
    send_masked_frame,
    wait_for_server,
)


SECRET = "m1-business-flow-secret-at-least-32-bytes"


def memory_only_environment():
    environment = os.environ.copy()
    # These E2E tests exercise the in-memory M1/M2 boundary.  Explicitly
    # remove inherited dependency settings so a developer's shell or CI job
    # cannot silently switch only part of the application to MySQL/Redis.
    for name in (
        "HTTPSERVER_CONFIG_FILE",
        "HTTPSERVER_DB_USER",
        "HTTPSERVER_DB_PASSWORD",
        "HTTPSERVER_DB_NAME",
        "HTTPSERVER_REDIS_HOST",
        "HTTPSERVER_REDIS_PASSWORD",
    ):
        environment.pop(name, None)
    environment["HTTPSERVER_JWT_SECRET"] = SECRET
    environment["HTTPSERVER_AUTH_STORE"] = "memory"
    return environment


def request_json(port, method, path, token="", body=None):
    encoded = b"" if body is None else json.dumps(
        body, separators=(",", ":")
    ).encode("utf-8")
    with socket.create_connection(("127.0.0.1", port), timeout=4) as sock:
        sock.settimeout(5)
        request = (
            f"{method} {path} HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Connection: close\r\n"
        )
        if token:
            request += f"Authorization: Bearer {token}\r\n"
        if body is not None:
            request += "Content-Type: application/json\r\n"
        if body is not None or method in {"POST", "PUT", "PATCH"}:
            request += f"Content-Length: {len(encoded)}\r\n"
        sock.sendall((request + "\r\n").encode("ascii") + encoded)
        status, _, response_body = recv_http_response(sock)
        try:
            response = json.loads(response_body.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise AssertionError(
                f"expected JSON for {method} {path}: "
                f"status={status}, body={response_body!r}"
            ) from error
        return status, response


def receive_json(sock):
    opcode, payload = recv_frame(sock)
    assert opcode == 0x1
    return json.loads(payload.decode("utf-8"))


def send_json(sock, value):
    send_masked_frame(
        sock,
        0x1,
        json.dumps(value, separators=(",", ":")).encode("utf-8"),
    )


def subscribe(sock, room_id, request_id):
    send_json(
        sock,
        {
            "version": 1,
            "type": "room.subscribe",
            "requestId": request_id,
            "payload": {"roomId": room_id},
        },
    )
    response = receive_json(sock)
    assert response == {
        "version": 1,
        "type": "room.subscribed",
        "requestId": request_id,
        "payload": {"roomId": room_id},
    }


def register_and_login(port, username):
    password = "M1-test-password-123"
    status, response = request_json(
        port, "POST", "/register", body={"username": username, "password": password}
    )
    assert status == 201 and response["ok"] is True
    status, response = request_json(
        port, "POST", "/login", body={"username": username, "password": "wrong-pass"}
    )
    assert status == 401 and response["ok"] is False
    status, response = request_json(
        port, "POST", "/login", body={"username": username, "password": password}
    )
    assert status == 200 and response["ok"] is True
    assert isinstance(response.get("token"), str) and response["token"]
    return response["token"]


def run_flow(port):
    alice_token = register_and_login(port, "m1-alice")
    bob_token = register_and_login(port, "m1-bob")
    charlie_token = register_and_login(port, "m1-charlie")

    status, response = request_json(
        port,
        "POST",
        "/api/v1/rooms",
        alice_token,
        {"name": "M1 acceptance room"},
    )
    assert status == 201
    room_id = response["data"]["room"]["id"]

    status, response = request_json(port, "GET", "/api/v1/rooms", bob_token)
    assert status == 200 and response["data"]["rooms"] == []

    join_path = f"/api/v1/rooms/{room_id}/members"
    status, response = request_json(port, "POST", join_path, bob_token)
    assert status == 200 and response["data"]["joined"] is True
    status, response = request_json(port, "POST", join_path, bob_token)
    assert status == 200 and response["data"]["joined"] is False
    status, response = request_json(port, "GET", join_path, bob_token)
    assert status == 200
    assert response["data"]["members"] == ["m1-alice", "m1-bob"]

    alice = connect_websocket(port, alice_token)
    bob = connect_websocket(port, bob_token)
    charlie = connect_websocket(port, charlie_token)
    try:
        subscribe(alice, room_id, "subscribe-alice")
        subscribe(bob, room_id, "subscribe-bob")

        send_json(
            charlie,
            {
                "version": 1,
                "type": "message.send",
                "requestId": "forbidden-before-join",
                "payload": {
                    "roomId": room_id,
                    "clientMessageId": "charlie-before-join",
                    "content": "must be rejected",
                },
            },
        )
        response = receive_json(charlie)
        assert response["type"] == "error"
        assert response["error"]["code"] == "room_membership_required"

        status, response = request_json(port, "POST", join_path, charlie_token)
        assert status == 200 and response["data"]["joined"] is True
        subscribe(charlie, room_id, "subscribe-charlie")

        message_command = {
            "version": 1,
            "type": "message.send",
            "requestId": "send-alice-1",
            "payload": {
                "roomId": room_id,
                "clientMessageId": "alice-client-1",
                "content": "hello from M1",
            },
        }
        send_json(alice, message_command)
        ack = receive_json(alice)
        assert ack["type"] == "message.ack"
        ack_payload = ack["payload"]
        assert ack_payload["clientMessageId"] == "alice-client-1"
        assert ack_payload["createdAt"].endswith("Z")

        for peer in (bob, charlie):
            push = receive_json(peer)
            assert push["type"] == "message.created"
            server_message = push["payload"]["message"]
            assert server_message["senderId"] == "m1-alice"
            assert server_message["clientMessageId"] == "alice-client-1"
            assert server_message["messageId"] == ack_payload["messageId"]
            assert server_message["createdAt"] == ack_payload["createdAt"]

        send_json(alice, message_command)
        duplicate = receive_json(alice)
        assert duplicate["type"] == "message.ack"
        assert duplicate["payload"] == ack_payload

        history_path = f"/api/v1/rooms/{room_id}/messages?limit=20"
        status, history = request_json(port, "GET", history_path, bob_token)
        assert status == 200
        assert history["data"]["messages"] == [server_message]
        assert history["data"]["nextBefore"] is None

        status, invalid_cursor = request_json(
            port,
            "GET",
            f"/api/v1/rooms/{room_id}/messages?before=not-a-cursor",
            bob_token,
        )
        assert status == 400
        assert invalid_cursor["error"]["code"] == "invalid_cursor"

        status, _ = request_json(
            port,
            "DELETE",
            f"/api/v1/rooms/{room_id}/members/me",
            charlie_token,
        )
        assert status == 200
        send_json(
            charlie,
            {
                "version": 1,
                "type": "message.send",
                "requestId": "forbidden-after-leave",
                "payload": {
                    "roomId": room_id,
                    "clientMessageId": "charlie-after-leave",
                    "content": "must also be rejected",
                },
            },
        )
        response = receive_json(charlie)
        assert response["type"] == "error"
        assert response["error"]["code"] == "room_membership_required"

        status, response = request_json(port, "GET", history_path, charlie_token)
        assert status == 403
        assert response["error"]["code"] == "room_membership_required"
    finally:
        charlie.close()
        bob.close()
        alice.close()


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: ChatBusinessFlowTest.py SERVER PORT")
    server = sys.argv[1]
    port = int(sys.argv[2])
    log_file = tempfile.NamedTemporaryFile(prefix="httpserver-m1-", delete=False)
    log_path = log_file.name
    log_file.close()
    environment = memory_only_environment()
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
