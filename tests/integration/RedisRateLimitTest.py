#!/usr/bin/env python3
"""Verify Redis-backed login rate limits are shared by two server instances."""

import concurrent.futures
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import uuid

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from ChatBusinessFlowTest import request_json  # noqa: E402
from WebSocketSmokeTest import wait_for_server  # noqa: E402


def start(server, port, log_path):
    environment = os.environ.copy()
    environment["HTTPSERVER_JWT_SECRET"] = "m4-redis-secret-at-least-32-bytes"
    environment["HTTPSERVER_AUTH_STORE"] = "memory"
    environment["HTTPSERVER_AUTH_MAX_ATTEMPTS"] = "10"
    return subprocess.Popen(
        [server, "-b", "epoll", "-t", "2", "-p", str(port), "-l", log_path],
        env=environment,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def stop(process):
    process.terminate()
    try:
        process.wait(timeout=4)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def failed_login(port, username):
    status, response = request_json(
        port, "POST", "/login", body={
            "username": username,
            "password": "wrong-password",
        }
    )
    assert response["ok"] is False
    return status


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: RedisRateLimitTest.py SERVER PORT_A PORT_B")
    if not os.environ.get("HTTPSERVER_REDIS_HOST"):
        raise SystemExit("HTTPSERVER_REDIS_HOST is required")
    server, first_port, second_port = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    external_servers = server == "external"
    log_a = tempfile.NamedTemporaryFile(prefix="httpserver-m4-redis-a-", delete=False)
    log_b = tempfile.NamedTemporaryFile(prefix="httpserver-m4-redis-b-", delete=False)
    log_a.close()
    log_b.close()
    first = None if external_servers else start(server, first_port, log_a.name)
    second = None if external_servers else start(server, second_port, log_b.name)
    try:
        wait_for_server(first_port)
        wait_for_server(second_port)
        run_id = uuid.uuid4().hex[:12]
        username = f"m4-shared-{run_id}"
        statuses = [failed_login(port, username) for port in (
            first_port, second_port, first_port, second_port, first_port,
            second_port, first_port, second_port, first_port, second_port,
            first_port,
        )]
        assert statuses[:10] == [401] * 10
        assert statuses[10] == 429

        concurrent_user = f"m4-concurrent-{run_id}"
        barrier = threading.Barrier(100)

        def request(index):
            barrier.wait()
            return failed_login(first_port if index % 2 == 0 else second_port,
                                concurrent_user)

        with concurrent.futures.ThreadPoolExecutor(max_workers=100) as executor:
            concurrent_statuses = list(executor.map(request, range(100)))
        accepted = concurrent_statuses.count(401)
        limited = concurrent_statuses.count(429)
        assert accepted == 10 and limited == 90, (
            f"expected 10 accepted and 90 limited, got {accepted} accepted, "
            f"{limited} limited, statuses={sorted(set(concurrent_statuses))}"
        )
        print("redis_rate_limit_acceptance shared_limit=10 concurrent=100")
    finally:
        if second is not None:
            stop(second)
        if first is not None:
            stop(first)
        os.unlink(log_b.name)
        os.unlink(log_a.name)


if __name__ == "__main__":
    main()
