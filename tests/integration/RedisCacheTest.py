#!/usr/bin/env python3
"""Verify room/recent-message Cache Aside paths publish hit/miss metrics."""

from pathlib import Path
import http.client
import os
import subprocess
import sys
import tempfile
import uuid

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "e2e"))

from ChatBusinessFlowTest import register_and_login, request_json  # noqa: E402
from WebSocketSmokeTest import wait_for_server  # noqa: E402


ROOT = Path(__file__).resolve().parents[2]
COMPOSE = ["docker", "compose", "-f", str(ROOT / "docker-compose.test.yml")]


def metrics(port):
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=3)
    try:
        connection.request("GET", "/metrics")
        response = connection.getresponse()
        assert response.status == 200
        return response.read().decode("utf-8")
    finally:
        connection.close()


def value(output, name):
    for line in output.splitlines():
        if line.startswith(name + " "):
            return int(line.rsplit(" ", 1)[1])
    raise AssertionError(f"missing metric {name}")


def start_server(server, port, log_path):
    environment = os.environ.copy()
    environment["HTTPSERVER_JWT_SECRET"] = "m4-cache-secret-at-least-32-bytes"
    return subprocess.Popen(
        [server, "-b", "epoll", "-t", "2", "-p", str(port), "-l", log_path],
        env=environment,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def stop_server(process):
    process.terminate()
    try:
        process.wait(timeout=4)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def main():
    if len(sys.argv) == 2:
        server, port = "external", int(sys.argv[1])
    elif len(sys.argv) == 3:
        server, port = sys.argv[1], int(sys.argv[2])
    else:
        raise SystemExit("usage: RedisCacheTest.py [SERVER|external] PORT")
    log_file = tempfile.NamedTemporaryFile(prefix="httpserver-m4-cache-", delete=False)
    log_file.close()
    process = None if server == "external" else start_server(server, port, log_file.name)
    try:
        wait_for_server(port)
        username = f"m4-cache-{uuid.uuid4().hex[:12]}"
        token = register_and_login(port, username)
        status, room = request_json(port, "POST", "/api/v1/rooms", token,
                                    {"name": "M4 cache"})
        assert status == 201
        room_id = room["data"]["room"]["id"]
        path = f"/api/v1/rooms/{room_id}/messages?after=0&limit=50"
        assert request_json(port, "GET", path, token)[0] == 200
        assert request_json(port, "GET", path, token)[0] == 200
        output = metrics(port)
        assert value(output, "httpserver_redis_cache_hits_total") >= 1
        assert value(output, "httpserver_redis_cache_misses_total") >= 1
        misses_before_invalidation = value(output, "httpserver_redis_cache_misses_total")
        subprocess.run(
            [*COMPOSE, "exec", "-T", "redis", "redis-cli", "DEL",
             f"chat:room:{room_id}", f"chat:room:{room_id}:recent"],
            check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        assert request_json(port, "GET", path, token)[0] == 200
        output = metrics(port)
        assert value(output, "httpserver_redis_cache_misses_total") >= (
            misses_before_invalidation + 2
        )
        print("redis_cache_acceptance cache_hit=1 cache_miss=1 invalidation_refill=1")
    finally:
        if process is not None:
            stop_server(process)
        os.unlink(log_file.name)


if __name__ == "__main__":
    main()
