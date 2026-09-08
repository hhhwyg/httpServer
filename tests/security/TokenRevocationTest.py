#!/usr/bin/env python3
"""Verify logout persists a JWT jti denylist entry in a real Redis instance."""

import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "integration"))

from ChatBusinessFlowTest import request_json as plain_request  # noqa: E402
from WebSocketSmokeTest import (  # noqa: E402
    recv_http_response, request_json as authenticated_request, wait_for_server,
)


def logout(port, token):
    with socket.create_connection(("127.0.0.1", port), timeout=3) as sock:
        sock.settimeout(3)
        request = (
            "POST /logout HTTP/1.1\r\n"
            "Host: localhost\r\n"
            f"Authorization: Bearer {token}\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n\r\n"
        )
        sock.sendall(request.encode("ascii"))
        return recv_http_response(sock)[0]


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: TokenRevocationTest.py SERVER PORT")
    if not os.environ.get("HTTPSERVER_REDIS_HOST"):
        raise SystemExit("HTTPSERVER_REDIS_HOST is required")
    server, port = sys.argv[1], int(sys.argv[2])
    with tempfile.NamedTemporaryFile(prefix="httpserver-m8-revoke-", delete=False) as log:
        log_path = log.name
    environment = os.environ.copy()
    environment.update({
        "HTTPSERVER_JWT_SECRET": "m8-token-revocation-secret-at-least-32-bytes",
        "HTTPSERVER_AUTH_STORE": "memory",
    })
    process = None if server == "external" else subprocess.Popen(
        [server, "-b", "epoll", "-t", "1", "-p", str(port), "-l", log_path],
        env=environment, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    try:
        wait_for_server(port)
        status, response = plain_request(
            port, "POST", "/register",
            body={"username": "m8-revoke-user", "password": "correct-horse-battery-staple"},
        )
        assert status == 201 and response["ok"] is True
        status, response = plain_request(
            port, "POST", "/login",
            body={"username": "m8-revoke-user", "password": "correct-horse-battery-staple"},
        )
        assert status == 200 and response["ok"] is True
        token = response["token"]
        status, _ = authenticated_request(port, token, "GET", "/api/v1/rooms")
        assert status == 200
        assert logout(port, token) == 204
        status, _ = authenticated_request(port, token, "GET", "/api/v1/rooms")
        assert status == 401
        print("token_revocation_acceptance before=200 logout=204 after=401")
    finally:
        if process is not None:
            process.terminate()
            try:
                process.wait(timeout=4)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        os.unlink(log_path)


if __name__ == "__main__":
    main()
