#!/usr/bin/env python3
"""Verify a single slow client cannot retain output beyond the hard limit."""

import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def request(port, target):
    with socket.create_connection(("127.0.0.1", port), timeout=2) as sock:
        sock.settimeout(2)
        sock.sendall(
            f"GET {target} HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n".encode()
        )
        data = b""
        while b"\r\n\r\n" not in data:
            chunk = sock.recv(4096)
            if not chunk:
                return data
            data += chunk
        raw_headers, body = data.split(b"\r\n\r\n", 1)
        content_length = 0
        for line in raw_headers.split(b"\r\n")[1:]:
            if line.lower().startswith(b"content-length:"):
                content_length = int(line.split(b":", 1)[1].strip())
        while len(body) < content_length:
            chunk = sock.recv(4096)
            if not chunk:
                break
            body += chunk
        return raw_headers + b"\r\n\r\n" + body


def wait_ready(port):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        try:
            if b"200 OK" in request(port, "/ping"):
                return
        except OSError:
            time.sleep(0.1)
    raise AssertionError("server did not become ready")


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: SlowConsumerTest.py SERVER PORT")
    server, port = sys.argv[1], int(sys.argv[2])
    source_root = Path.cwd()
    with tempfile.TemporaryDirectory(prefix="httpserver-slow-consumer-") as temp:
        root = Path(temp)
        shutil.copytree(source_root / "wwwroot", root / "wwwroot")
        fixture = root / "wwwroot" / "slow-consumer.bin"
        fixture.write_bytes(b"x" * (96 * 1024))
        environment = os.environ.copy()
        for name in list(environment):
            if name.startswith("HTTPSERVER_DB_") or name.startswith("HTTPSERVER_REDIS_"):
                environment.pop(name)
        environment.pop("HTTPSERVER_CONFIG_FILE", None)
        environment["HTTPSERVER_OUTPUT_BUFFER_HIGH_WATER_BYTES"] = "32768"
        environment["HTTPSERVER_OUTPUT_BUFFER_HARD_LIMIT_BYTES"] = "65536"
        log_path = str(root / "server.log")
        process = subprocess.Popen(
            [server, "-b", "epoll", "-t", "1", "-p", str(port), "-l", log_path],
            cwd=root, env=environment, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        try:
            wait_ready(port)
            with socket.create_connection(("127.0.0.1", port), timeout=2) as slow:
                slow.settimeout(2)
                slow.sendall(b"GET /slow-consumer.bin HTTP/1.1\r\nHost: localhost\r\n\r\n")
                try:
                    received = slow.recv(1)
                except ConnectionResetError:
                    received = b""
                assert received == b"", "slow client unexpectedly received a buffered response"
            assert b"200 OK" in request(port, "/ping")
            metrics = request(port, "/metrics")
            assert b"httpserver_slow_consumer_closed_total 1" in metrics
            print("slow_consumer_acceptance hard_limit=65536 closed=1 ping=healthy")
        finally:
            process.terminate()
            try:
                process.wait(timeout=4)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


if __name__ == "__main__":
    main()
