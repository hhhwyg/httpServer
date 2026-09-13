#!/usr/bin/env python3
"""Bounded-memory WebSocket handshake/ping load probe with strict failure exit."""
import argparse
import asyncio
import math
import os
import time

from benchmark_common import (
    Endpoint, LatencyHistogram, add_transport_arguments, handshake_request,
    make_token, tls_context, validate_handshake,
)
from run_chat_benchmark import ResourceSampler, parse_pids


async def worker(endpoint, auth_token, args, context, ready, go, histogram):
    writer = None
    ok, connected, error = 0, False, None
    try:
        reader, writer = await asyncio.wait_for(asyncio.open_connection(
            endpoint.host, endpoint.port, ssl=context if endpoint.secure else None,
            server_hostname=endpoint.host if endpoint.secure else None), args.timeout)
        request, key = handshake_request(endpoint, auth_token, args.origin)
        writer.write(request)
        await writer.drain()
        response = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), args.timeout)
        validate_handshake(response, key, auth_token)
        connected = True
    except (OSError, AssertionError, ValueError, asyncio.TimeoutError,
            asyncio.IncompleteReadError, asyncio.LimitOverrunError) as failure:
        error = str(failure)
    finally:
        await ready.put(connected)
    try:
        await go.wait()
        while connected and time.monotonic() < args.deadline:
            started = time.perf_counter_ns()
            writer.write(b"\x89\x81\x01\x02\x03\x04\x79")
            await writer.drain()
            pong = await asyncio.wait_for(reader.readexactly(3), args.timeout)
            if pong != b"\x8a\x01x":
                raise ValueError("invalid pong")
            histogram.add((time.perf_counter_ns() - started) / 1_000_000)
            ok += 1
    except (OSError, ValueError, asyncio.TimeoutError, asyncio.IncompleteReadError) as failure:
        error = str(failure)
    finally:
        if writer:
            writer.close()
            try:
                await asyncio.wait_for(writer.wait_closed(), args.timeout)
            except (OSError, asyncio.TimeoutError):
                pass
    return ok, connected, error


async def run_workers(endpoint, args, context, histogram, sampler):
    ready, go = asyncio.Queue(), asyncio.Event()
    run_id = os.urandom(6).hex()
    tasks = [asyncio.create_task(worker(endpoint,
        make_token(args.jwt_secret, f"bp-{run_id}-{index}", math.ceil(args.duration) + 3600),
        args, context, ready, go, histogram)) for index in range(args.connections)]
    established = [await ready.get() for _ in tasks]
    started = time.monotonic()
    args.deadline = started + args.duration if all(established) else started
    sampler.start()
    go.set()
    try:
        results = await asyncio.gather(*tasks)
    finally:
        sampler.stop()
    return results, time.monotonic() - started


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", required=True)
    parser.add_argument("--duration", type=float, required=True)
    parser.add_argument("--connections", type=int, required=True)
    parser.add_argument("--jwt-secret", default=os.environ.get("HTTPSERVER_BENCH_JWT_SECRET"))
    parser.add_argument("--server-pids", type=parse_pids, default=[])
    parser.add_argument("--resource-output")
    parser.add_argument("--sample-interval", type=float, default=5)
    add_transport_arguments(parser)
    args = parser.parse_args()
    if any(not math.isfinite(value) or value <= 0 for value in
           (args.duration, args.timeout, args.sample_interval)) or args.connections <= 0:
        parser.error("duration, connections, sample-interval and timeout must be finite and positive")
    if not args.jwt_secret:
        parser.error("ping fixture auth requires HTTPSERVER_BENCH_JWT_SECRET or --jwt-secret")
    try:
        endpoint = Endpoint.parse(args.url)
        context = tls_context(args.ca_file, args.insecure)
    except ValueError as error:
        parser.error(str(error))
    histogram = LatencyHistogram()
    sampler = ResourceSampler(args.server_pids, args.resource_output, args.sample_interval)
    results, elapsed = asyncio.run(run_workers(endpoint, args, context, histogram, sampler))
    ok = sum(result[0] for result in results)
    connected = sum(result[1] for result in results)
    errors = sum(result[2] is not None for result in results)
    passed = connected == args.connections and ok > 0 and not errors and sampler.valid
    print("tool=python-asyncio-websocket-benchmark")
    print(f"url={endpoint.url}")
    print(f"tls_verification={'disabled-local-only' if args.insecure else 'enabled'}")
    print(f"duration_requested_seconds={args.duration}")
    print(f"duration_seconds={elapsed:.3f}")
    print(f"connections={args.connections}")
    print(f"successful_handshakes={connected}")
    print(f"successful_handshake_ping={ok}")
    print(f"errors={errors}")
    if errors:
        print(f"first_error={next(result[2] for result in results if result[2] is not None)}")
    print(f"operations_per_second={ok / elapsed:.2f}")
    print("latency_method=logarithmic_histogram_upper_bound_1pct")
    for name, fraction in (("p50", .5), ("p95", .95), ("p99", .99)):
        print(f"latency_{name}_ms={histogram.percentile(fraction):.3f}")
    sampler.print_results(elapsed)
    print(f"benchmark_result={'passed' if passed else 'failed'}")
    if not passed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
