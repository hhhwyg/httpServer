#!/usr/bin/env python3
"""Exercise disconnect-heavy HTTP traffic against a real server process."""

import concurrent.futures
import os
import socket
import subprocess
import sys
import tempfile
import time


def receive_headers(sock):
    data = b""
    while b"\r\n\r\n" not in data:
        chunk = sock.recv(4096)
        if not chunk:
            raise AssertionError("connection closed before receiving headers")
        data += chunk
        if len(data) > 16384:
            raise AssertionError("response headers exceeded limit")
    return data


def ping(port):
    with socket.create_connection(("127.0.0.1", port), timeout=2) as sock:
        sock.settimeout(2)
        sock.sendall(
            b"GET /ping HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
        )
        response = receive_headers(sock)
        headers, body = response.split(b"\r\n\r\n", 1)
        if b"HTTP/1.1 200 OK" not in headers:
            raise AssertionError("health request did not receive a valid response")
        while len(body) < 2:
            chunk = sock.recv(16)
            if not chunk:
                break
            body += chunk
        if body != b"OK":
            raise AssertionError(f"unexpected health response body: {body!r}")


def disconnect_mid_request(port, iteration):
    with socket.create_connection(("127.0.0.1", port), timeout=2) as sock:
        if iteration % 3 == 0:
            sock.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\n")
        elif iteration % 3 == 1:
            sock.sendall(b"POST /login HTTP/1.1\r\nHost: localhost\r\nContent-Length: 64\r\n\r\n{")
        else:
            # A client that sends a valid request but never drains its response.
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 256)
            sock.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n")
            time.sleep(0.01)


def wait_for_server(port):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        try:
            ping(port)
            return
        except (OSError, AssertionError):
            time.sleep(0.1)
    raise AssertionError("server did not become ready")


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: ConnectionLifecycleStressTest.py SERVER PORT")

    server = sys.argv[1]
    port = int(sys.argv[2])
    log_file = tempfile.NamedTemporaryFile(prefix="httpserver-lifecycle-", delete=False)
    log_path = log_file.name
    log_file.close()
    process = subprocess.Popen(
        [server, "-b", "epoll", "-t", "1", "-p", str(port), "-l", log_path],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        wait_for_server(port)
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
            futures = [
                executor.submit(disconnect_mid_request, port, iteration)
                for iteration in range(320)
            ]
            for future in futures:
                future.result(timeout=5)
        for _ in range(8):
            ping(port)
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
