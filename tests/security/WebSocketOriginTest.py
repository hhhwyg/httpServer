#!/usr/bin/env python3
"""Verify configured browser origins are required for WebSocket upgrades."""

import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "integration"))

from WebSocketSmokeTest import make_token, recv_until, wait_for_server  # noqa: E402


def handshake(port, token, origin=None, use_subprotocol=False):
    with socket.create_connection(("127.0.0.1", port), timeout=3) as sock:
        sock.settimeout(3)
        headers = "" if origin is None else f"Origin: {origin}\r\n"
        authorization = "" if use_subprotocol else f"Authorization: Bearer {token}\r\n"
        subprotocol = (
            f"Sec-WebSocket-Protocol: httpserver.v1, bearer.{token}\r\n"
            if use_subprotocol else ""
        )
        request = (
            "GET /ws HTTP/1.1\r\n"
            "Host: localhost\r\n"
            f"{authorization}"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            f"{subprotocol}"
            f"{headers}\r\n"
        )
        sock.sendall(request.encode("ascii"))
        return recv_until(sock, b"\r\n\r\n")


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: WebSocketOriginTest.py SERVER PORT")
    server, port = sys.argv[1], int(sys.argv[2])
    with tempfile.NamedTemporaryFile(prefix="httpserver-m8-origin-", delete=False) as log:
        log_path = log.name
    environment = os.environ.copy()
    environment.update({
        "HTTPSERVER_JWT_SECRET": "phase2-websocket-test-secret-change-me-123456",
        "HTTPSERVER_WS_ALLOWED_ORIGINS": "https://chat.example.test",
    })
    process = subprocess.Popen(
        [server, "-b", "epoll", "-t", "1", "-p", str(port), "-l", log_path],
        env=environment, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    try:
        wait_for_server(port)
        token = make_token("m8-origin-user")
        missing_origin = handshake(port, token)
        untrusted_origin = handshake(port, token, "https://evil.example.test")
        assert b"403 Forbidden" in missing_origin, missing_origin
        assert b"403 Forbidden" in untrusted_origin, untrusted_origin
        assert b"101 Switching Protocols" in handshake(
            port, token, "https://chat.example.test"
        )
        browser_protocol = handshake(
            port, token, "https://chat.example.test", use_subprotocol=True
        )
        assert b"101 Switching Protocols" in browser_protocol, browser_protocol
        assert b"Sec-WebSocket-Protocol: httpserver.v1" in browser_protocol
        assert token.encode() not in browser_protocol
        print("websocket_origin_acceptance missing=403 untrusted=403 trusted=101 browser_protocol=101")
    finally:
        process.terminate()
        try:
            process.wait(timeout=4)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        os.unlink(log_path)


if __name__ == "__main__":
    main()
