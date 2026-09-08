#!/usr/bin/env python3
"""Exercise uniform authentication failures and local abuse-window recovery."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "integration"))

from ChatBusinessFlowTest import memory_only_environment, request_json  # noqa: E402
from WebSocketSmokeTest import wait_for_server  # noqa: E402


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: AuthenticationAbuseTest.py SERVER PORT")
    server, port = sys.argv[1], int(sys.argv[2])
    with tempfile.NamedTemporaryFile(prefix="httpserver-m8-auth-", delete=False) as log:
        log_path = log.name
    environment = memory_only_environment()
    environment.update({
        "HTTPSERVER_JWT_SECRET": "m8-authentication-secret-at-least-32-bytes",
        "HTTPSERVER_AUTH_STORE": "memory",
        "HTTPSERVER_AUTH_MAX_ATTEMPTS": "3",
        "HTTPSERVER_AUTH_WINDOW_SECONDS": "1",
    })
    process = subprocess.Popen(
        [server, "-b", "epoll", "-t", "1", "-p", str(port), "-l", log_path],
        env=environment, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    try:
        wait_for_server(port)
        username = "m8-abuse-user"
        password = "correct-horse-battery-staple"
        status, response = request_json(
            port, "POST", "/register", body={"username": username, "password": password}
        )
        assert status == 201 and response["ok"] is True

        unknown_status, unknown = request_json(
            port, "POST", "/login", body={"username": "m8-unknown", "password": password}
        )
        wrong_status, wrong = request_json(
            port, "POST", "/login", body={"username": username, "password": "wrong-password"}
        )
        assert unknown_status == wrong_status == 401
        assert unknown["msg"] == wrong["msg"] == "Invalid username or password"

        for _ in range(2):
            status, _ = request_json(
                port, "POST", "/login", body={"username": username, "password": "wrong-password"}
            )
            assert status == 401
        status, limited = request_json(
            port, "POST", "/login", body={"username": username, "password": "wrong-password"}
        )
        assert status == 429 and limited["msg"] == "Too many attempts"

        time.sleep(1.2)
        status, recovered = request_json(
            port, "POST", "/login", body={"username": username, "password": password}
        )
        assert status == 200 and recovered["ok"] is True
        assert recovered["token"].count(".") == 2
        print("authentication_abuse_acceptance uniform_failure=1 limited=1 recovered=1")
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
