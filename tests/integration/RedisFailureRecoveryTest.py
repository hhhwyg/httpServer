#!/usr/bin/env python3
"""Exercise Redis outage fallback, reconnect, and WebSocket presence cleanup."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import uuid

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from ChatBusinessFlowTest import register_and_login, request_json  # noqa: E402
from WebSocketSmokeTest import connect_websocket, wait_for_server  # noqa: E402


ROOT = Path(__file__).resolve().parents[2]
COMPOSE = ["docker", "compose", "-f", str(ROOT / "docker-compose.test.yml")]
REDIS_PORT = os.environ.get("HTTPSERVER_REDIS_PORT", "36379")
INSTANCE_ID = "redis-failure-recovery-test"


def compose(*arguments):
    return subprocess.run(
        [*COMPOSE, *arguments], check=True, stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )


def redis(*arguments):
    return subprocess.check_output(
        [*COMPOSE, "exec", "-T", "redis", "redis-cli", *arguments], text=True
    ).strip()


def wait_for_redis():
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        try:
            if redis("ping") == "PONG":
                return
        except subprocess.CalledProcessError:
            time.sleep(0.2)
    raise AssertionError("Redis did not become ready")


def failed_login(port, username):
    return request_json(
        port, "POST", "/login", body={
            "username": username,
            "password": "wrong-password",
        }
    )


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: RedisFailureRecoveryTest.py SERVER PORT")
    if not os.environ.get("HTTPSERVER_REDIS_HOST"):
        raise SystemExit("HTTPSERVER_REDIS_HOST is required")
    server, port = sys.argv[1], int(sys.argv[2])
    external_server = server == "external"
    compose("up", "-d", "redis")
    wait_for_redis()
    environment = os.environ.copy()
    environment.update({
        "HTTPSERVER_JWT_SECRET": "m4-recovery-secret-at-least-32-bytes",
        "HTTPSERVER_AUTH_STORE": "memory",
        "HTTPSERVER_AUTH_MAX_ATTEMPTS": "2",
        "HTTPSERVER_INSTANCE_ID": INSTANCE_ID,
        "HTTPSERVER_REDIS_COMMAND_TIMEOUT_MS": "500",
    })
    log_file = tempfile.NamedTemporaryFile(prefix="httpserver-m4-redis-recovery-", delete=False)
    log_file.close()
    process = None if external_server else subprocess.Popen(
        [server, "-b", "epoll", "-t", "2", "-p", str(port), "-l", log_file.name],
        env=environment, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    try:
        wait_for_server(port)
        username = f"m4-presence-{uuid.uuid4().hex[:12]}"
        token = register_and_login(port, username)
        socket = connect_websocket(port, token)
        try:
            key = f"chat:presence:{username}:{INSTANCE_ID}"
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline and redis("get", key) != "1":
                time.sleep(0.1)
            assert redis("get", key) == "1"
        finally:
            socket.close()
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline and redis("get", key) != "":
            time.sleep(0.1)
        assert redis("get", key) == ""

        compose("stop", "redis")
        started = time.monotonic()
        status, response = failed_login(port, f"m4-outage-{uuid.uuid4().hex[:12]}")
        elapsed = time.monotonic() - started
        assert status == 401 and response["ok"] is False
        assert elapsed < 2.0

        compose("up", "-d", "redis")
        wait_for_redis()
        recovered_user = f"m4-recovered-{uuid.uuid4().hex[:12]}"
        assert failed_login(port, recovered_user)[0] == 401
        assert failed_login(port, recovered_user)[0] == 401
        assert failed_login(port, recovered_user)[0] == 429
        print("redis_failure_recovery_acceptance fallback_lt=2s reconnected=1 presence=clean")
    finally:
        if process is not None:
            process.terminate()
            try:
                process.wait(timeout=4)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        os.unlink(log_file.name)
        compose("up", "-d", "redis")


if __name__ == "__main__":
    main()
