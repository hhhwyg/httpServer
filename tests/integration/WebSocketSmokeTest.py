#!/usr/bin/env python3
import base64
import hashlib
import hmac
import json
import os
import socket
import subprocess
import sys
import tempfile
import time


SECRET = b"phase2-websocket-test-secret-change-me-123456"
ISSUER = "httpServer"


def b64url(value):
    return base64.urlsafe_b64encode(value).rstrip(b"=").decode("ascii")


def make_token(username):
    header = {"alg": "HS256", "typ": "JWT"}
    now = int(time.time())
    payload = {"sub": username, "iss": ISSUER, "iat": now, "exp": now + 300}
    encoded_header = b64url(json.dumps(header, separators=(",", ":")).encode())
    encoded_payload = b64url(json.dumps(payload, separators=(",", ":")).encode())
    signing_input = f"{encoded_header}.{encoded_payload}".encode("ascii")
    signature = b64url(hmac.new(SECRET, signing_input, hashlib.sha256).digest())
    return f"{encoded_header}.{encoded_payload}.{signature}"


def recv_until(sock, marker, limit=16384):
    data = b""
    while marker not in data:
        chunk = sock.recv(4096)
        if not chunk:
            raise AssertionError("connection closed before expected response")
        data += chunk
        if len(data) > limit:
            raise AssertionError("response exceeded test limit")
    return data


def recv_exact(sock, size):
    data = b""
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise AssertionError("connection closed during WebSocket frame")
        data += chunk
    return data


def recv_http_response(sock):
    data = recv_until(sock, b"\r\n\r\n")
    raw_headers, body = data.split(b"\r\n\r\n", 1)
    lines = raw_headers.decode("iso-8859-1").split("\r\n")
    status = int(lines[0].split(" ", 2)[1])
    headers = {}
    for line in lines[1:]:
        name, value = line.split(":", 1)
        headers[name.lower()] = value.strip()
    content_length = int(headers.get("content-length", "0"))
    while len(body) < content_length:
        body += recv_exact(sock, content_length - len(body))
    return status, headers, body[:content_length]


def send_masked_frame(sock, opcode, payload):
    mask = b"\x01\x02\x03\x04"
    encoded = bytes(value ^ mask[index % 4] for index, value in enumerate(payload))
    if len(payload) <= 125:
        header = bytes([0x80 | opcode, 0x80 | len(payload)])
    elif len(payload) <= 0xFFFF:
        header = bytes([0x80 | opcode, 0x80 | 126]) + len(payload).to_bytes(2, "big")
    else:
        raise AssertionError("test frame is unexpectedly large")
    sock.sendall(header + mask + encoded)


def recv_frame(sock):
    first, second = recv_exact(sock, 2)
    if second & 0x80:
        raise AssertionError("server response must not be masked")
    length = second & 0x7F
    if length == 126:
        length = int.from_bytes(recv_exact(sock, 2), "big")
    elif length == 127:
        length = int.from_bytes(recv_exact(sock, 8), "big")
    return first & 0x0F, recv_exact(sock, length)


def wait_for_server(port):
    deadline = time.time() + 8
    while time.time() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=1) as sock:
                sock.sendall(b"GET /ping HTTP/1.1\r\nHost: localhost\r\n\r\n")
                if b"200 OK" in recv_until(sock, b"\r\n\r\n"):
                    return
        except (OSError, AssertionError):
            time.sleep(0.1)
    raise AssertionError("server did not become ready")


def create_room(port, token):
    body = json.dumps({"name": "phase2-test-room"}, separators=(",", ":")).encode()
    with socket.create_connection(("127.0.0.1", port), timeout=3) as sock:
        sock.settimeout(3)
        request = (
            "POST /room/create HTTP/1.1\r\n"
            "Host: localhost\r\n"
            f"Authorization: Bearer {token}\r\n"
            "Content-Type: application/json\r\n"
            f"Content-Length: {len(body)}\r\n"
            "Connection: close\r\n\r\n"
        )
        sock.sendall(request.encode("ascii") + body)
        status, _, response_body = recv_http_response(sock)
        assert status == 200
        response = json.loads(response_body.decode("utf-8"))
        assert response["ok"] is True and isinstance(response["roomId"], str)
        return response["roomId"]


def assert_room_listed(port, token, room_id):
    with socket.create_connection(("127.0.0.1", port), timeout=3) as sock:
        sock.settimeout(3)
        request = (
            "GET /room/list HTTP/1.1\r\n"
            "Host: localhost\r\n"
            f"Authorization: Bearer {token}\r\n"
            "Connection: close\r\n\r\n"
        )
        sock.sendall(request.encode("ascii"))
        status, _, response_body = recv_http_response(sock)
        assert status == 200
        response = json.loads(response_body.decode("utf-8"))
        assert any(room["id"] == room_id for room in response["rooms"])


def connect_websocket(port, token):
    sock = socket.create_connection(("127.0.0.1", port), timeout=3)
    sock.settimeout(3)
    key = "dGhlIHNhbXBsZSBub25jZQ=="
    request = (
        f"GET /ws?token={token} HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n"
    )
    sock.sendall(request.encode("ascii"))
    handshake = recv_until(sock, b"\r\n\r\n")
    assert b"101 Switching Protocols" in handshake
    assert b"Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=" in handshake
    return sock


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: WebSocketSmokeTest.py SERVER PORT")
    server = sys.argv[1]
    port = int(sys.argv[2])
    log_file = tempfile.NamedTemporaryFile(prefix="httpserver-ws-", delete=False)
    log_path = log_file.name
    log_file.close()
    environment = os.environ.copy()
    environment["HTTPSERVER_JWT_SECRET"] = SECRET.decode("ascii")
    process = subprocess.Popen(
        [server, "-b", "epoll", "-t", "1", "-p", str(port), "-l", log_path],
        env=environment,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        wait_for_server(port)
        alice_token = make_token("phase2-alice")
        bob_token = make_token("phase2-bob")
        room_id = create_room(port, alice_token)
        assert_room_listed(port, alice_token, room_id)

        alice = connect_websocket(port, alice_token)
        bob = connect_websocket(port, bob_token)
        try:
            send_masked_frame(alice, 0x9, b"ping")
            opcode, payload = recv_frame(alice)
            assert opcode == 0xA and payload == b"ping"

            missing_join = json.dumps(
                {"type": "join", "roomId": "missing"}, separators=(",", ":")
            )
            send_masked_frame(alice, 0x1, missing_join.encode("ascii"))
            opcode, payload = recv_frame(alice)
            assert opcode == 0x1
            assert json.loads(payload.decode("utf-8")) == {
                "type": "error",
                "code": "room_not_found",
            }

            join = json.dumps({"type": "join", "roomId": room_id}, separators=(",", ":"))
            send_masked_frame(alice, 0x1, join.encode("ascii"))
            send_masked_frame(bob, 0x1, join.encode("ascii"))
            time.sleep(0.1)

            message = json.dumps(
                {"type": "chat", "roomId": room_id, "content": "hello from alice"},
                separators=(",", ":"),
            )
            send_masked_frame(alice, 0x1, message.encode("utf-8"))
            opcode, payload = recv_frame(bob)
            assert opcode == 0x1
            assert json.loads(payload.decode("utf-8")) == json.loads(message)

            reply = json.dumps(
                {"type": "chat", "roomId": room_id, "content": "hello from bob"},
                separators=(",", ":"),
            )
            send_masked_frame(bob, 0x1, reply.encode("utf-8"))
            opcode, payload = recv_frame(alice)
            assert opcode == 0x1
            assert json.loads(payload.decode("utf-8")) == json.loads(reply)
        finally:
            bob.close()
            alice.close()
        wait_for_server(port)
    finally:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        try:
            os.unlink(log_path)
        except FileNotFoundError:
            pass


if __name__ == "__main__":
    main()
