#!/usr/bin/env python3
"""Verify SIGTERM stops the listener and exits within the grace window."""

import os
import signal
import socket
import subprocess
import sys
import tempfile
import time


def wait_for_server(port):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.5) as sock:
                sock.sendall(b"GET /healthz HTTP/1.1\r\nHost: localhost\r\n\r\n")
                if b"200 OK" in sock.recv(1024):
                    return
        except OSError:
            time.sleep(0.1)
    raise AssertionError("server did not become ready")


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: GracefulShutdownTest.py SERVER PORT")
    log = tempfile.NamedTemporaryFile(prefix="httpserver-shutdown-", delete=False)
    log_path = log.name
    log.close()
    process = subprocess.Popen(
        [sys.argv[1], "-b", "epoll", "-t", "1", "-p", sys.argv[2], "-l", log_path],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    try:
        wait_for_server(int(sys.argv[2]))
        started = time.monotonic()
        process.send_signal(signal.SIGTERM)
        assert process.wait(timeout=5) == 0
        assert time.monotonic() - started >= 1.5
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.unlink(log_path)


if __name__ == "__main__":
    main()
