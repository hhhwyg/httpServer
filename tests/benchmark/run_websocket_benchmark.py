#!/usr/bin/env python3
"""Dependency-free WebSocket handshake/ping load probe with raw results."""
import argparse
import asyncio
import base64
import hashlib
import hmac
import json
import math
import os
import time
from urllib.parse import urlsplit


def token(secret):
    now = int(time.time())
    encode = lambda value: base64.urlsafe_b64encode(value).rstrip(b"=").decode()
    header = encode(b'{"alg":"HS256","typ":"JWT"}')
    payload = encode(json.dumps({"sub": "benchmark-user", "iss": "httpServer",
        "jti": encode(os.urandom(16)), "iat": now, "exp": now + 3600},
        separators=(",", ":")).encode())
    signature = encode(hmac.new(secret.encode(), f"{header}.{payload}".encode(), hashlib.sha256).digest())
    return f"{header}.{payload}.{signature}"


def percentile(values, fraction):
    if not values: return 0.0
    values.sort()
    return values[min(len(values) - 1, math.ceil(len(values) * fraction) - 1)]


async def worker(host, port, path, auth_token, deadline):
    ok = errors = 0
    latencies = []
    request = (f"GET {path} HTTP/1.1\r\nHost: {host}\r\nAuthorization: Bearer {auth_token}\r\n"
        "Origin: https://chat.example.test\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n").encode()
    writer = None
    try:
        reader, writer = await asyncio.wait_for(asyncio.open_connection(host, port), 2)
        writer.write(request); await writer.drain()
        response = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), 2)
        if b"101 Switching Protocols" not in response: raise ValueError("upgrade rejected")
        while time.monotonic() < deadline:
            started = time.perf_counter_ns()
            writer.write(b"\x89\x81\x01\x02\x03\x04\x79"); await writer.drain()
            pong = await asyncio.wait_for(reader.readexactly(3), 2)
            if pong[:2] != b"\x8a\x01" or pong[2:] != b"x": raise ValueError("invalid pong")
            latencies.append((time.perf_counter_ns() - started) / 1_000_000); ok += 1
    except (OSError, ValueError, asyncio.TimeoutError, asyncio.IncompleteReadError):
        errors += 1
    finally:
        if writer:
            writer.close()
            try: await writer.wait_closed()
            except OSError: pass
    return ok, errors, latencies


async def run_workers(host, port, path, secret, deadline, connections):
    return await asyncio.gather(*[
        worker(host, port, path, token(secret), deadline)
        for _ in range(connections)
    ])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", required=True); parser.add_argument("--duration", type=float, required=True)
    parser.add_argument("--connections", type=int, required=True); parser.add_argument("--jwt-secret", required=True)
    args = parser.parse_args()
    target = urlsplit(args.url)
    if target.scheme not in ("ws", "http") or not target.hostname or args.duration <= 0 or args.connections <= 0:
        parser.error("url must be ws:// or http:// and duration/connections must be positive")
    started = time.monotonic(); deadline = started + args.duration
    results = asyncio.run(run_workers(target.hostname, target.port or 80,
                                      target.path or "/ws", args.jwt_secret,
                                      deadline, args.connections))
    elapsed = time.monotonic() - started; ok = sum(r[0] for r in results); errors = sum(r[1] for r in results)
    latencies = [value for r in results for value in r[2]]
    print("tool=python-asyncio-websocket-benchmark"); print(f"duration_seconds={elapsed:.3f}")
    print(f"connections={args.connections}"); print(f"successful_handshake_ping={ok}"); print(f"errors={errors}")
    print(f"operations_per_second={ok / elapsed:.2f}"); print(f"latency_p50_ms={percentile(latencies,.5):.3f}")
    print(f"latency_p95_ms={percentile(latencies,.95):.3f}"); print(f"latency_p99_ms={percentile(latencies,.99):.3f}")


if __name__ == "__main__": main()
