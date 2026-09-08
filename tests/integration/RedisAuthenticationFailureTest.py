#!/usr/bin/env python3
"""Verify an invalid Redis password falls back without stalling login."""

from pathlib import Path
import os
import subprocess
import sys
import tempfile
import time
import uuid

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))

from ChatBusinessFlowTest import request_json  # noqa: E402
from WebSocketSmokeTest import wait_for_server  # noqa: E402


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: RedisAuthenticationFailureTest.py SERVER PORT")
    server, port = sys.argv[1], int(sys.argv[2])
    process = None
    log_path = None
    if server != "external":
        log_file = tempfile.NamedTemporaryFile(
            prefix="httpserver-m4-redis-auth-", delete=False
        )
        log_path = log_file.name
        log_file.close()
        environment = {
            **os.environ,
            "HTTPSERVER_JWT_SECRET": "m4-auth-failure-secret-at-least-32-bytes",
            "HTTPSERVER_AUTH_STORE": "memory",
            "HTTPSERVER_REDIS_PASSWORD": "invalid-password",
            "HTTPSERVER_REDIS_COMMAND_TIMEOUT_MS": "500",
        }
        process = subprocess.Popen(
            [server, "-b", "epoll", "-t", "2", "-p", str(port), "-l", log_path],
            env=environment, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
    try:
        wait_for_server(port)
        started = time.monotonic()
        status, response = request_json(
            port, "POST", "/login", body={
                "username": f"m4-auth-{uuid.uuid4().hex[:12]}",
                "password": "wrong-password",
            }
        )
        assert status == 401 and response["ok"] is False
        assert time.monotonic() - started < 2.0
        print("redis_auth_failure_acceptance fallback_lt=2s")
    finally:
        if process is not None:
            process.terminate()
            try:
                process.wait(timeout=4)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        if log_path is not None:
            Path(log_path).unlink(missing_ok=True)


if __name__ == "__main__":
    main()
