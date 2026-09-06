#!/usr/bin/env python3
"""Small standard-library HTTP benchmark fallback for Linux/WSL."""

import argparse
import asyncio
import math
import os
import time
from urllib.parse import urlsplit


def process_metrics(pid):
    if pid is None:
        return {}
    try:
        with open(f"/proc/{pid}/stat", encoding="ascii") as stat_file:
            fields = stat_file.read().split()
        clock_ticks = os.sysconf(os.sysconf_names["SC_CLK_TCK"])
        cpu_seconds = (int(fields[13]) + int(fields[14])) / clock_ticks
        with open(f"/proc/{pid}/status", encoding="ascii") as status_file:
            status = status_file.read()
        rss_kb = next(
            int(line.split()[1])
            for line in status.splitlines()
            if line.startswith("VmRSS:")
        )
        context_switches = {
            line.split(":", 1)[0]: int(line.split()[1])
            for line in status.splitlines()
            if line.startswith(("voluntary_ctxt_switches:", "nonvoluntary_ctxt_switches:"))
        }
        fd_count = len(os.listdir(f"/proc/{pid}/fd"))
        return {
            "cpu_seconds": cpu_seconds,
            "rss_kb": rss_kb,
            "fd_count": fd_count,
            "context_switches": sum(context_switches.values()),
        }
    except (FileNotFoundError, IndexError, StopIteration, ValueError):
        return {}


def percentile(values, fraction):
    if not values:
        return 0.0
    values = sorted(values)
    index = max(0, min(len(values) - 1, math.ceil(fraction * len(values)) - 1))
    return values[index]


async def close_connection(writer):
    if writer is not None:
        writer.close()
        try:
            await writer.wait_closed()
        except OSError:
            pass


async def run_connection(host, port, path, deadline, connection_mode):
    latencies = []
    successes = 0
    errors = 0
    reader = None
    writer = None
    request = (
        f"GET {path} HTTP/1.1\r\nHost: {host}\r\n"
        f"Connection: {connection_mode}\r\n\r\n"
    ).encode("ascii")
    while time.monotonic() < deadline:
        try:
            if writer is None:
                reader, writer = await asyncio.wait_for(
                    asyncio.open_connection(host, port), timeout=1.0
                )
            started = time.perf_counter_ns()
            writer.write(request)
            await asyncio.wait_for(writer.drain(), timeout=1.0)
            header = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), timeout=1.0)
            status = int(header.split(b" ", 2)[1])
            content_length = 0
            for line in header.split(b"\r\n")[1:]:
                if line.lower().startswith(b"content-length:"):
                    content_length = int(line.split(b":", 1)[1].strip())
                    break
            if content_length:
                await asyncio.wait_for(reader.readexactly(content_length), timeout=1.0)
            latencies.append((time.perf_counter_ns() - started) / 1_000_000)
            if 200 <= status < 400:
                successes += 1
            else:
                errors += 1
            if connection_mode == "close":
                await close_connection(writer)
                reader = None
                writer = None
        except (OSError, ValueError, asyncio.TimeoutError, asyncio.IncompleteReadError):
            errors += 1
            await close_connection(writer)
            reader = None
            writer = None
    await close_connection(writer)
    return latencies, successes, errors


async def run_benchmark(host, port, path, deadline, connection_mode, connections):
    return await asyncio.gather(
        *(
            run_connection(host, port, path, deadline, connection_mode)
            for _ in range(connections)
        )
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", required=True)
    parser.add_argument("--duration", type=float, required=True)
    parser.add_argument("--connections", type=int, required=True)
    parser.add_argument("--threads", type=int, default=1)
    parser.add_argument("--server-pid", type=int)
    parser.add_argument(
        "--connection-mode", choices=("keep-alive", "close"), default="keep-alive"
    )
    args = parser.parse_args()
    if args.duration <= 0 or args.connections <= 0 or args.threads <= 0:
        parser.error("duration, connections and threads must be positive")

    target = urlsplit(args.url)
    if target.scheme != "http" or not target.hostname:
        parser.error("--url must be an http:// URL")
    host = target.hostname
    port = target.port or 80
    path = target.path or "/"
    if target.query:
        path += "?" + target.query

    before = process_metrics(args.server_pid)
    started = time.monotonic()
    deadline = started + args.duration
    results = asyncio.run(
        run_benchmark(
            host,
            port,
            path,
            deadline,
            args.connection_mode,
            args.connections,
        )
    )
    elapsed = time.monotonic() - started
    after = process_metrics(args.server_pid)

    latencies = [latency for result in results for latency in result[0]]
    successes = sum(result[1] for result in results)
    errors = sum(result[2] for result in results)
    total = successes + errors
    print("tool=python-asyncio-http-benchmark")
    print(f"url={args.url}")
    print(f"duration_seconds={elapsed:.3f}")
    print(f"threads={args.threads}")
    print(f"connections={args.connections}")
    print("concurrency_model=one-asyncio-event-loop")
    print(f"connection_mode={args.connection_mode}")
    print(f"requests={total}")
    print(f"successful_requests={successes}")
    print(f"errors={errors}")
    print(f"requests_per_second={successes / elapsed:.2f}")
    print(f"error_rate={errors / total:.6f}" if total else "error_rate=1.000000")
    print(f"latency_p50_ms={percentile(latencies, 0.50):.3f}")
    print(f"latency_p95_ms={percentile(latencies, 0.95):.3f}")
    print(f"latency_p99_ms={percentile(latencies, 0.99):.3f}")
    if before and after:
        print(f"server_cpu_seconds={after['cpu_seconds'] - before['cpu_seconds']:.3f}")
        print(f"server_rss_before_kb={before['rss_kb']}")
        print(f"server_rss_after_kb={after['rss_kb']}")
        print(f"server_fd_before={before['fd_count']}")
        print(f"server_fd_after={after['fd_count']}")
        print(
            "server_context_switches_delta="
            f"{after['context_switches'] - before['context_switches']}"
        )


if __name__ == "__main__":
    main()
