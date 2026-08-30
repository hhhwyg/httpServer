#!/usr/bin/env python3
"""Verify registration and login against the configured MySQL instance."""

import json
import os
import socket
import subprocess
import sys
import tempfile
import time


def read_response(sock):
    data = b""
    while b"\r\n\r\n" not in data:
        chunk = sock.recv(4096)
        if not chunk:
            raise AssertionError("connection closed before response headers")
        data += chunk
    raw_headers, body = data.split(b"\r\n\r\n", 1)
    lines = raw_headers.decode("iso-8859-1").split("\r\n")
    status = int(lines[0].split(" ", 2)[1])
    headers = {}
    for line in lines[1:]:
        name, value = line.split(":", 1)
        headers[name.lower()] = value.strip()
    content_length = int(headers.get("content-length", "0"))
    while len(body) < content_length:
        chunk = sock.recv(content_length - len(body))
        if not chunk:
            raise AssertionError("connection closed before response body")
        body += chunk
    return status, body[:content_length]


def request(port, method, path, payload):
    body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
    with socket.create_connection(("127.0.0.1", port), timeout=3) as sock:
        sock.settimeout(3)
        headers = (
            f"{method} {path} HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Content-Type: application/json\r\n"
            f"Content-Length: {len(body)}\r\n"
            "Connection: close\r\n\r\n"
        )
        sock.sendall(headers.encode("ascii") + body)
        return read_response(sock)


def wait_for_server(port):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=1) as sock:
                sock.settimeout(1)
                sock.sendall(b"GET /ping HTTP/1.1\r\nHost: localhost\r\n\r\n")
                status, body = read_response(sock)
                if status == 200 and body == b"OK":
                    return
        except (OSError, AssertionError, ValueError):
            time.sleep(0.1)
    raise AssertionError("server did not become ready")


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: DatabaseAuthenticationTest.py SERVER PORT")

    server = sys.argv[1]
    port = int(sys.argv[2])
    username = f"phase3_{os.getpid()}"
    password = "correct-horse-battery-staple"
    log_file = tempfile.NamedTemporaryFile(prefix="httpserver-db-", delete=False)
    log_path = log_file.name
    log_file.close()
    process = subprocess.Popen(
        [server, "-b", "epoll", "-t", "1", "-p", str(port), "-l", log_path],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        wait_for_server(port)
        status, body = request(port, "POST", "/register", {"username": username, "password": password})
        assert status == 201 and json.loads(body.decode("utf-8")) == {"ok": True}

        status, _ = request(port, "POST", "/register", {"username": username, "password": password})
        assert status == 400

        status, body = request(port, "POST", "/login", {"username": username, "password": password})
        response = json.loads(body.decode("utf-8"))
        assert status == 200 and response["ok"] is True and response["token"]

        status, _ = request(port, "POST", "/login", {"username": username, "password": "wrong-password"})
        assert status == 401

        status, _ = request(
            port,
            "POST",
            "/register",
            {"username": "phase3' OR '1'='1", "password": password},
        )
        assert status == 400
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
