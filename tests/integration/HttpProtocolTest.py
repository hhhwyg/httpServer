#!/usr/bin/env python3
"""Exercise HTTP request boundaries against a real server process."""

import os
import socket
import subprocess
import sys
import tempfile
import time


def read_response(sock, pending=b"", read_body=True):
    data = pending
    while b"\r\n\r\n" not in data:
        chunk = sock.recv(4096)
        if not chunk:
            raise AssertionError("connection closed before response headers")
        data += chunk
        if len(data) > 32768:
            raise AssertionError("response headers exceeded limit")

    raw_headers, data = data.split(b"\r\n\r\n", 1)
    lines = raw_headers.decode("iso-8859-1").split("\r\n")
    status = int(lines[0].split(" ", 2)[1])
    headers = {}
    for line in lines[1:]:
        name, value = line.split(":", 1)
        headers[name.lower()] = value.strip()

    body_length = int(headers.get("content-length", "0")) if read_body else 0
    while len(data) < body_length:
        chunk = sock.recv(4096)
        if not chunk:
            raise AssertionError("connection closed before response body")
        data += chunk
    return status, headers, data[:body_length], data[body_length:]


def wait_for_server(port):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=1) as sock:
                sock.settimeout(1)
                sock.sendall(b"GET /ping HTTP/1.1\r\nHost: localhost\r\n\r\n")
                status, _, body, _ = read_response(sock)
                if status == 200 and body == b"OK":
                    return
        except (OSError, AssertionError, ValueError):
            time.sleep(0.1)
    raise AssertionError("server did not become ready")


def test_keep_alive(port):
    with socket.create_connection(("127.0.0.1", port), timeout=2) as sock:
        sock.settimeout(2)
        sock.sendall(
            b"GET /ping HTTP/1.1\r\nHost: localhost\r\n"
            b"Connection: keep-alive\r\n\r\n"
        )
        status, headers, body, pending = read_response(sock)
        assert status == 200 and body == b"OK"
        assert headers.get("connection", "").lower() == "keep-alive"

        sock.sendall(
            b"HEAD /ping HTTP/1.1\r\nHost: localhost\r\n"
            b"Connection: close\r\n\r\n"
        )
        status, headers, body, _ = read_response(sock, pending, read_body=False)
        assert status == 200 and body == b""
        assert headers.get("content-length") == "2"
        assert headers.get("connection", "").lower() == "close"


def expect_status(port, request, expected):
    with socket.create_connection(("127.0.0.1", port), timeout=2) as sock:
        sock.settimeout(2)
        sock.sendall(request)
        status, _, _, _ = read_response(sock)
        assert status == expected, f"expected {expected}, got {status}"


def test_invalid_requests(port):
    expect_status(
        port,
        b"PUT /ping HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
        405,
    )
    expect_status(
        port,
        b"GET /ping HTTP/1.1\r\nHeaderWithoutColon\r\n\r\n",
        400,
    )
    expect_status(
        port,
        b"GET /ping HTTP/1.1\r\nHost: localhost\r\nX-Long: " + b"a" * 17000,
        400,
    )


def test_operational_endpoints(port):
    with socket.create_connection(("127.0.0.1", port), timeout=2) as sock:
        sock.settimeout(2)
        sock.sendall(b"GET /healthz HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
        status, _, body, _ = read_response(sock)
        assert status == 200 and body == b"ok\n"

    with socket.create_connection(("127.0.0.1", port), timeout=2) as sock:
        sock.settimeout(2)
        sock.sendall(b"GET /readyz HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
        status, _, body, _ = read_response(sock)
        assert status == 503 and body == b"not ready\n"

    with socket.create_connection(("127.0.0.1", port), timeout=2) as sock:
        sock.settimeout(2)
        sock.sendall(b"GET /metrics HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
        status, headers, body, _ = read_response(sock)
        assert status == 200
        assert headers.get("content-type", "").startswith("text/plain")
        assert b"httpserver_requests_total " in body
        assert b"httpserver_connections_active " in body


def test_post_body_does_not_pollute_next_request(port):
    body = b'{"username":"alice","password":"password"}'
    with socket.create_connection(("127.0.0.1", port), timeout=2) as sock:
        sock.settimeout(2)
        request = (
            b"POST /login HTTP/1.1\r\nHost: localhost\r\n"
            b"Connection: keep-alive\r\nContent-Type: application/json\r\n"
            + f"Content-Length: {len(body)}\r\n\r\n".encode("ascii")
            + body
        )
        sock.sendall(request)
        status, _, _, pending = read_response(sock)
        assert status == 503

        sock.sendall(
            b"GET /ping HTTP/1.1\r\nHost: localhost\r\n"
            b"Connection: close\r\n\r\n"
        )
        status, _, response_body, _ = read_response(sock, pending)
        assert status == 200 and response_body == b"OK"


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: HttpProtocolTest.py SERVER PORT")

    server = sys.argv[1]
    port = int(sys.argv[2])
    log_file = tempfile.NamedTemporaryFile(prefix="httpserver-http-", delete=False)
    log_path = log_file.name
    log_file.close()
    process = subprocess.Popen(
        [server, "-b", "epoll", "-t", "1", "-p", str(port), "-l", log_path],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        wait_for_server(port)
        test_keep_alive(port)
        test_post_body_does_not_pollute_next_request(port)
        test_invalid_requests(port)
        test_operational_endpoints(port)
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
