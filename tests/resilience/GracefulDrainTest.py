#!/usr/bin/env python3
"""Verify SIGTERM stops accepting and closes a WebSocket with status 1001."""

import base64
import hashlib
import hmac
import json
import os
import signal
import socket
import subprocess
import sys
import tempfile
import time


SECRET = b"drain-test-jwt-secret-change-me-1234567890"


def b64url(value):
    return base64.urlsafe_b64encode(value).rstrip(b"=").decode()


def token():
    now = int(time.time())
    header = b64url(json.dumps({"alg": "HS256", "typ": "JWT"}, separators=(",", ":")).encode())
    payload = b64url(json.dumps({"sub": "drain-user", "iss": "httpServer", "jti": b64url(b"graceful-drain-test-jti"), "iat": now, "exp": now + 60}, separators=(",", ":")).encode())
    signature = b64url(hmac.new(SECRET, f"{header}.{payload}".encode(), hashlib.sha256).digest())
    return f"{header}.{payload}.{signature}"


def wait_ready(port):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.5) as sock:
                sock.sendall(b"GET /ping HTTP/1.1\r\nHost: localhost\r\n\r\n")
                if b"200 OK" in sock.recv(512):
                    return
        except OSError:
            time.sleep(0.1)
    raise AssertionError("server did not become ready")


def recv_until(sock, marker):
    data = b""
    while marker not in data:
        chunk = sock.recv(4096)
        if not chunk:
            raise AssertionError("connection closed before handshake")
        data += chunk
    return data


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: GracefulDrainTest.py SERVER PORT")
    server, port = sys.argv[1], int(sys.argv[2])
    log = tempfile.NamedTemporaryFile(prefix="httpserver-drain-", delete=False)
    log_path = log.name
    log.close()
    environment = {**os.environ, "HTTPSERVER_JWT_SECRET": SECRET.decode(),
                   "HTTPSERVER_GRACEFUL_SHUTDOWN_SECONDS": "2"}
    process = subprocess.Popen(
        [server, "-b", "epoll", "-t", "1", "-p", str(port), "-l", log_path],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=environment,
    )
    websocket = None
    try:
        wait_ready(port)
        websocket = socket.create_connection(("127.0.0.1", port), timeout=2)
        websocket.settimeout(3)
        key = base64.b64encode(b"graceful-drain-key").decode()
        websocket.sendall((
            f"GET /ws?token={token()} HTTP/1.1\r\nHost: localhost\r\n"
            "Upgrade: websocket\r\nConnection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n"
        ).encode())
        assert b"101 Switching Protocols" in recv_until(websocket, b"\r\n\r\n")
        started = time.monotonic()
        process.send_signal(signal.SIGTERM)
        header = websocket.recv(2)
        assert len(header) == 2 and header[0] & 0x0F == 0x8
        length = header[1] & 0x7F
        payload = websocket.recv(length)
        assert int.from_bytes(payload[:2], "big") == 1001
        assert process.wait(timeout=5) == 0
        assert 1.5 <= time.monotonic() - started < 4.5
        try:
            socket.create_connection(("127.0.0.1", port), timeout=0.3)
            raise AssertionError("listener still accepted a connection")
        except OSError:
            pass
        print("graceful_drain_acceptance websocket_close=1001 listener=stopped")
    finally:
        if websocket:
            websocket.close()
        if process.poll() is None:
            process.kill()
            process.wait()
        os.unlink(log_path)


if __name__ == "__main__":
    main()
