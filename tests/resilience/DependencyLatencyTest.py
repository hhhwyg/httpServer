#!/usr/bin/env python3
"""Inject MySQL latency through Toxiproxy and verify the EventLoop stays live."""

import http.client
import json
import socket
import sys
import threading
import time
import urllib.request


def request(port, method, target, body=b""):
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
    connection.request(method, target, body, {"Content-Type": "application/json"})
    response = connection.getresponse()
    payload = response.read()
    connection.close()
    return response.status, payload


def proxy_request(url, method, payload=None):
    data = json.dumps(payload).encode() if payload is not None else None
    request_object = urllib.request.Request(url, data=data, method=method)
    if data is not None:
        request_object.add_header("Content-Type", "application/json")
    with urllib.request.urlopen(request_object, timeout=3) as response:
        return response.read()


def wait_ready(port):
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        try:
            if request(port, "GET", "/ping")[0] == 200:
                return
        except (OSError, http.client.HTTPException):
            time.sleep(0.2)
    raise AssertionError("chaos server did not become ready")


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: DependencyLatencyTest.py SERVER_PORT TOXIPROXY_PORT")
    server_port, proxy_port = map(int, sys.argv[1:])
    wait_ready(server_port)
    base = f"http://127.0.0.1:{proxy_port}/proxies/mysql/toxics"
    proxy_request(base, "POST", {
        "name": "mysql-latency", "type": "latency", "stream": "downstream",
        "attributes": {"latency": 2000, "jitter": 0},
    })
    result = {}

    def dependent_request():
        started = time.monotonic()
        result["status"], _ = request(
            server_port, "POST", "/register",
            b'{"username":"chaosuser","password":"password123"}',
        )
        result["elapsed"] = time.monotonic() - started

    thread = threading.Thread(target=dependent_request)
    thread.start()
    latencies = []
    while thread.is_alive():
        started = time.monotonic()
        assert request(server_port, "GET", "/ping")[0] == 200
        latencies.append(time.monotonic() - started)
        time.sleep(0.05)
    thread.join(timeout=1)
    proxy_request(base + "/mysql-latency", "DELETE")
    assert result["status"] == 503, result
    assert result["elapsed"] < 4.0, result
    assert latencies and max(latencies) < 0.5, latencies
    print("dependency_latency_acceptance ping_max_ms=%d dependency_status=%d" %
          (max(latencies) * 1000, result["status"]))


if __name__ == "__main__":
    main()
