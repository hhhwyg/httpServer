#!/usr/bin/env python3
"""Exercise bounded-label HTTP metrics through real protocol requests."""

import http.client
import os
import subprocess
import sys
import tempfile
import time


def request(port, method, path):
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=3)
    connection.request(method, path)
    response = connection.getresponse()
    payload = response.read()
    connection.close()
    return response.status, payload.decode("utf-8", "replace")


def wait_ready(port):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        try:
            if request(port, "GET", "/ping")[0] == 200:
                return
        except OSError:
            time.sleep(0.1)
    raise AssertionError("server did not become ready")


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: MetricsScenarioTest.py SERVER PORT")
    server, port = sys.argv[1], int(sys.argv[2])
    log = tempfile.NamedTemporaryFile(prefix="httpserver-observability-", delete=False)
    log_path = log.name
    log.close()
    environment = os.environ.copy()
    for name in (
        "HTTPSERVER_CONFIG_FILE",
        "HTTPSERVER_JWT_SECRET",
        "HTTPSERVER_DB_USER",
        "HTTPSERVER_DB_PASSWORD",
        "HTTPSERVER_DB_NAME",
        "HTTPSERVER_REDIS_HOST",
        "HTTPSERVER_REDIS_PASSWORD",
    ):
        environment.pop(name, None)
    environment["HTTPSERVER_GRACEFUL_SHUTDOWN_SECONDS"] = "1"
    process = subprocess.Popen(
        [server, "-b", "epoll", "-t", "1", "-p", str(port), "-l", log_path],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        env=environment,
    )
    try:
        wait_ready(port)
        assert request(port, "GET", "/healthz")[0] == 200
        assert request(port, "POST", "/login")[0] == 503
        assert request(port, "GET", "/api/v1/rooms/42/messages")[0] == 401
        status, metrics = request(port, "GET", "/metrics")
        assert status == 200
        assert 'httpserver_http_requests_total{method="GET",route="/healthz",status_class="2xx"} 1' in metrics
        assert 'httpserver_http_requests_total{method="POST",route="/login",status_class="5xx"} 1' in metrics
        assert 'httpserver_http_requests_total{method="GET",route="/api/v1/rooms/:id",status_class="4xx"} 1' in metrics
        assert "httpserver_websocket_connections 0" in metrics
        assert "httpserver_database_executor_queue_depth " in metrics
        print("metrics_scenario_acceptance routes=3 status_classes=3 bounded_labels=1")
    finally:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        os.unlink(log_path)


if __name__ == "__main__":
    main()
