#!/usr/bin/env python3
"""Closed-loop WebSocket fanout benchmark with bounded correctness evidence."""

import argparse
import json
import math
import os
import selectors
import sys
import threading
import time
from pathlib import Path

from benchmark_common import (
    Endpoint, LatencyHistogram, add_transport_arguments, connect_websocket,
    make_token, receive_frame, request_json, tls_context,
)

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "integration"))
from WebSocketSmokeTest import send_masked_frame  # noqa: E402


def send_json(sock, value):
    send_masked_frame(sock, 0x1, json.dumps(value, separators=(",", ":")).encode())


def receive_json(sock, deadline):
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("message delivery deadline exceeded")
        opcode, payload = receive_frame(sock, deadline)
        if opcode == 0x9:
            send_masked_frame(sock, 0xA, payload)
            continue
        if opcode != 0x1:
            raise AssertionError(f"expected text frame, got opcode={opcode}")
        return json.loads(payload)


def subscribe(sock, room_id, request_id, timeout):
    send_json(sock, {"version": 1, "type": "room.subscribe",
                     "requestId": request_id, "payload": {"roomId": room_id}})
    reply = receive_json(sock, time.monotonic() + timeout)
    assert reply.get("type") == "room.subscribed" and reply.get("requestId") == request_id, reply


def register_and_login(endpoint, username, context, timeout):
    password = "Benchmark-password-2026"
    status, response = request_json(endpoint, "", "POST", "/register",
        {"username": username, "password": password}, context, timeout)
    assert status == 201 and response.get("ok") is True, response
    status, response = request_json(endpoint, "", "POST", "/login",
        {"username": username, "password": password}, context, timeout)
    assert status == 200 and isinstance(response.get("token"), str), response
    return response["token"]


def read_process_totals(pids):
    totals = {"processes": 0, "rss_kib": 0, "threads": 0, "fds": 0,
              "cpu_ticks": 0, "context_switches": 0, "identities": {}}
    for pid in pids:
        proc = Path("/proc") / str(pid)
        try:
            status = (proc / "status").read_text(encoding="ascii")
            # comm may contain spaces and parentheses; the final ')' ends it.
            stat = (proc / "stat").read_text(encoding="ascii").rpartition(")")[2].split()
            fds = len(list((proc / "fd").iterdir()))
            cpu_ticks = int(stat[11]) + int(stat[12])
            started = int(stat[19])
        except (FileNotFoundError, ProcessLookupError, PermissionError, IndexError, ValueError):
            continue
        totals["processes"] += 1
        totals["cpu_ticks"] += cpu_ticks
        totals["fds"] += fds
        totals["identities"][str(pid)] = started
        for line in status.splitlines():
            if line.startswith("VmRSS:"):
                totals["rss_kib"] += int(line.split()[1])
            elif line.startswith("Threads:"):
                totals["threads"] += int(line.split()[1])
            elif line.startswith(("voluntary_ctxt_switches:", "nonvoluntary_ctxt_switches:")):
                totals["context_switches"] += int(line.split()[1])
    return totals


class ResourceSampler:
    def __init__(self, pids, output=None, interval=5):
        self.pids, self.output, self.interval = pids, output, interval
        self.started = read_process_totals(pids)
        self.finished = self.started
        self.valid = self.started["processes"] == len(pids)
        self.peak_rss_kib = self.started["rss_kib"]
        self.peak_fds = self.started["fds"]
        self._stop = threading.Event()
        self._thread = self._file = self.error = None

    def start(self):
        if self.output:
            self._file = open(self.output, "w", encoding="utf-8")
        self._record("start")
        self._thread = threading.Thread(target=self._sample, daemon=True)
        self._thread.start()

    def _record(self, phase):
        current = read_process_totals(self.pids)
        self.valid = self.valid and current["identities"] == self.started["identities"]
        self.finished = current
        self.peak_rss_kib = max(self.peak_rss_kib, current["rss_kib"])
        self.peak_fds = max(self.peak_fds, current["fds"])
        if self._file:
            self._file.write(json.dumps({"timestamp_unix": time.time(), "phase": phase,
                "server": current, "client": read_process_totals([os.getpid()])}) + "\n")
            self._file.flush()

    def _sample(self):
        try:
            while not self._stop.wait(self.interval):
                self._record("sample")
        except OSError as error:
            self.error, self.valid = error, False

    def stop(self):
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=2)
        try:
            self._record("end")
        except OSError as error:
            self.error, self.valid = error, False
        finally:
            if self._file:
                self._file.close()

    def print_results(self, elapsed):
        if self.error:
            print(f"resource_sampling_error={self.error}")
        if not self.pids:
            print("server_resource_sampling=disabled")
            return
        cpu_seconds = max(0, self.finished["cpu_ticks"] - self.started["cpu_ticks"]) / os.sysconf("SC_CLK_TCK")
        print(f"server_resource_sampling={'valid' if self.valid else 'invalid'}")
        print(f"server_processes_sampled={self.finished['processes']}")
        print(f"server_cpu_seconds={cpu_seconds:.3f}")
        print(f"server_cpu_cores_average={cpu_seconds / elapsed:.3f}")
        for field, label in (("rss_kib", "rss"), ("fds", "fd"), ("threads", "threads")):
            unit = "_kib" if field == "rss_kib" else ""
            print(f"server_{label}_start{unit}={self.started[field]}")
            print(f"server_{label}_end{unit}={self.finished[field]}")
        print(f"server_rss_peak_kib={self.peak_rss_kib}")
        print(f"server_fd_peak={self.peak_fds}")
        print("server_main_thread_context_switches_delta="
              f"{max(0, self.finished['context_switches'] - self.started['context_switches'])}")


def parse_pids(value):
    if not value:
        return []
    try:
        pids = [int(item) for item in value.split(",")]
    except ValueError as error:
        raise argparse.ArgumentTypeError("server PIDs must be comma-separated integers") from error
    if any(pid <= 0 for pid in pids) or len(set(pids)) != len(pids):
        raise argparse.ArgumentTypeError("server PIDs must be unique and positive")
    return pids


def await_ack(sender, request_id, deadline):
    sender_echoes = 0
    for _ in range(32):
        reply = receive_json(sender, deadline)
        if reply.get("type") == "message.created":
            sender_echoes += 1
            continue
        if reply.get("type") == "message.ack" and reply.get("requestId") == request_id:
            return reply, sender_echoes
        raise AssertionError(f"unexpected sender frame: {reply}")
    raise AssertionError(f"ACK not received for requestId={request_id}")


def validate_delivery(event, ack, room_id, client_message_id, content):
    message = event.get("payload", {}).get("message", {})
    if event.get("type") != "message.created":
        raise AssertionError(f"invalid delivery type: {event.get('type')}")
    expected = {"roomId": room_id, "clientMessageId": client_message_id,
                "messageId": ack["messageId"], "createdAt": ack["createdAt"], "content": content}
    for key, value in expected.items():
        if message.get(key) != value:
            raise AssertionError(f"delivery {key} mismatch for {client_message_id}")


def drain_receivers(room_states, duration):
    duplicates = errors = 0
    deadline = time.monotonic() + duration
    with selectors.DefaultSelector() as selector:
        for room_index, state in enumerate(room_states):
            for receiver_index, receiver in enumerate(state["receivers"]):
                selector.register(receiver, selectors.EVENT_READ,
                    (room_index, state["last_sequence"][receiver_index]))
        while time.monotonic() < deadline:
            pending = [key for key in selector.get_map().values()
                       if hasattr(key.fileobj, "pending") and key.fileobj.pending()]
            ready = pending or [key for key, _ in selector.select(deadline - time.monotonic())]
            if not ready:
                break
            for key in ready:
                try:
                    event = receive_json(key.fileobj, deadline)
                    observed_id = event.get("payload", {}).get("message", {}).get("clientMessageId", "")
                    prefix, _, observed_sequence = observed_id.rpartition("-")
                    room_index, last_sequence = key.data
                    if (event.get("type") == "message.created" and prefix == f"bench-{room_index}"
                            and observed_sequence.isdigit() and int(observed_sequence) <= last_sequence):
                        duplicates += 1
                    else:
                        errors += 1
                except (AssertionError, OSError, ValueError):
                    errors += 1
                    selector.unregister(key.fileobj)
    return duplicates, errors


def main():
    parser = argparse.ArgumentParser()
    target_group = parser.add_mutually_exclusive_group(required=True)
    target_group.add_argument("--url")
    target_group.add_argument("--port", type=int, help="legacy loopback endpoint")
    receiver_group = parser.add_mutually_exclusive_group()
    receiver_group.add_argument("--receiver-url")
    receiver_group.add_argument("--receiver-port", type=int)
    parser.add_argument("--require-cross-instance", action="store_true")
    parser.add_argument("--duration", type=float, required=True)
    parser.add_argument("--jwt-secret", default=os.environ.get("HTTPSERVER_BENCH_JWT_SECRET"))
    parser.add_argument("--recipients", type=int, default=1, help="receiver connections per room")
    parser.add_argument("--rooms", type=int, default=1)
    parser.add_argument("--message-bytes", type=int, default=256)
    parser.add_argument("--message-rate", type=float, default=0, help="aggregate messages/s; 0 is closed-loop saturation")
    parser.add_argument("--server-pids", type=parse_pids, default=[])
    parser.add_argument("--resource-output")
    parser.add_argument("--sample-interval", type=float, default=5)
    parser.add_argument("--drain-seconds", type=float, default=.2)
    parser.add_argument("--auth-mode", choices=("api", "fixture"), default="api")
    add_transport_arguments(parser)
    args = parser.parse_args()
    if any(not math.isfinite(value) or value <= 0 for value in
           (args.duration, args.timeout, args.sample_interval, args.drain_seconds)):
        parser.error("duration, timeout, sample-interval and drain-seconds must be finite and positive")
    if not math.isfinite(args.message_rate) or args.message_rate < 0:
        parser.error("message-rate must be finite and nonnegative")
    if args.recipients <= 0 or args.rooms <= 0:
        parser.error("recipients and rooms must be positive")
    if args.receiver_port is not None and not 1 <= args.receiver_port <= 65535:
        parser.error("receiver-port must be between 1 and 65535")
    if args.message_bytes <= 0 or args.message_bytes > 4096:
        parser.error("message-bytes must be between 1 and 4096")
    if args.auth_mode == "fixture" and not args.jwt_secret:
        parser.error("fixture auth requires HTTPSERVER_BENCH_JWT_SECRET or --jwt-secret")
    try:
        sender_endpoint = Endpoint.parse(args.url or f"ws://127.0.0.1:{args.port}/ws")
        receiver_endpoint = Endpoint.parse(args.receiver_url or (
            f"ws://127.0.0.1:{args.receiver_port}/ws" if args.receiver_port else sender_endpoint.url))
        if args.require_cross_instance and sender_endpoint.address == receiver_endpoint.address:
            parser.error("cross-instance workload requires distinct sender and receiver endpoints")
        context = tls_context(args.ca_file, args.insecure)
    except ValueError as error:
        parser.error(str(error))

    suffix = os.urandom(6).hex()
    room_states, all_sockets = [], []
    content = "x" * args.message_bytes
    try:
        for room_index in range(args.rooms):
            sender_name = f"bs-{suffix}-{room_index}"
            receiver_names = [f"br-{suffix}-{room_index}-{index}" for index in range(args.recipients)]
            if args.auth_mode == "fixture":
                lifetime = math.ceil(args.duration) + 3600
                sender_token = make_token(args.jwt_secret, sender_name, lifetime)
                receiver_tokens = [make_token(args.jwt_secret, name, lifetime) for name in receiver_names]
            else:
                sender_token = register_and_login(sender_endpoint, sender_name, context, args.timeout)
                receiver_tokens = [register_and_login(receiver_endpoint, name, context, args.timeout)
                                   for name in receiver_names]
            status, response = request_json(sender_endpoint, sender_token, "POST", "/api/v1/rooms",
                {"name": f"benchmark-{suffix}-{room_index}"}, context, args.timeout)
            assert status == 201 and response.get("ok") is True, response
            room_id = response["data"]["room"]["id"]
            for receiver_token in receiver_tokens:
                status, response = request_json(receiver_endpoint, receiver_token, "POST",
                    f"/api/v1/rooms/{room_id}/members", context=context, timeout=args.timeout)
                assert status == 200 and response.get("ok") is True, response
            sender = connect_websocket(sender_endpoint, sender_token, context, args.origin, args.timeout)
            all_sockets.append(sender)
            receivers = []
            for receiver_token in receiver_tokens:
                receiver = connect_websocket(receiver_endpoint, receiver_token, context, args.origin, args.timeout)
                all_sockets.append(receiver)
                receivers.append(receiver)
            subscribe(sender, room_id, f"sender-subscribe-{room_index}", args.timeout)
            for receiver_index, receiver in enumerate(receivers):
                subscribe(receiver, room_id, f"receiver-{room_index}-{receiver_index}", args.timeout)
            room_states.append({"id": room_id, "sender": sender, "receivers": receivers,
                                "last_sequence": [-1 for _ in receivers]})

        latencies = LatencyHistogram()
        attempts = acknowledged = delivered = errors = duplicates = out_of_order = 0
        sender_echoes, first_error = 0, None
        sampler = ResourceSampler(args.server_pids, args.resource_output, args.sample_interval)
        sampler.start()
        started_at = time.monotonic()
        deadline = started_at + args.duration
        try:
            while time.monotonic() < deadline:
                if args.message_rate:
                    due = started_at + attempts / args.message_rate
                    if due >= deadline:
                        time.sleep(max(0, deadline - time.monotonic()))
                        break
                    time.sleep(max(0, due - time.monotonic()))
                room_index = attempts % args.rooms
                state = room_states[room_index]
                sequence = attempts // args.rooms
                client_message_id = f"bench-{room_index}-{sequence}"
                request_id = f"request-{room_index}-{sequence}"
                started = time.perf_counter_ns()
                message_deadline = time.monotonic() + args.timeout
                attempts += 1
                try:
                    state["sender"].settimeout(args.timeout)
                    send_json(state["sender"], {"version": 1, "type": "message.send",
                        "requestId": request_id, "payload": {"roomId": state["id"],
                        "clientMessageId": client_message_id, "content": content}})
                    ack, echoes = await_ack(state["sender"], request_id, message_deadline)
                    sender_echoes += echoes
                    ack = ack.get("payload", {})
                    if (ack.get("clientMessageId") != client_message_id or not ack.get("messageId")
                            or not ack.get("createdAt")):
                        raise AssertionError("ACK identity or timestamp mismatch")
                    acknowledged += 1
                    for receiver_index, receiver in enumerate(state["receivers"]):
                        event = receive_json(receiver, message_deadline)
                        if event.get("type") != "message.created":
                            raise AssertionError(f"invalid delivery type: {event.get('type')}")
                        observed_id = event.get("payload", {}).get("message", {}).get("clientMessageId", "")
                        if observed_id != client_message_id:
                            prefix, _, observed_sequence = observed_id.rpartition("-")
                            if (prefix == f"bench-{room_index}" and observed_sequence.isdigit()
                                    and int(observed_sequence) <= state["last_sequence"][receiver_index]):
                                duplicates += 1
                            else:
                                out_of_order += 1
                            raise AssertionError(f"unexpected delivery ID: {observed_id}")
                        validate_delivery(event, ack, state["id"], client_message_id, content)
                        state["last_sequence"][receiver_index] = sequence
                        delivered += 1
                    latencies.add((time.perf_counter_ns() - started) / 1_000_000)
                except (AssertionError, OSError, ValueError, TypeError) as error:
                    errors += 1
                    first_error = str(error)
                    break
        finally:
            elapsed = time.monotonic() - started_at
            sampler.stop()

        if not errors:
            trailing_duplicates, trailing_errors = drain_receivers(room_states, args.drain_seconds)
            duplicates += trailing_duplicates
            errors += trailing_errors
        expected_deliveries = acknowledged * args.recipients
        missing = max(0, expected_deliveries - delivered)
        print("tool=python-websocket-chat-benchmark")
        print("workload_model=closed-loop-round-robin-one-message-in-flight")
        print("latency_method=logarithmic_histogram_upper_bound_1pct")
        print(f"duration_requested_seconds={args.duration}")
        print(f"duration_seconds={elapsed:.3f}")
        print(f"rooms={args.rooms}")
        print(f"recipients_per_room={args.recipients}")
        print(f"connections={args.rooms * (args.recipients + 1)}")
        print(f"message_bytes={args.message_bytes}")
        print(f"message_rate_limit={args.message_rate}")
        print(f"auth_mode={args.auth_mode}")
        print(f"sender_url={sender_endpoint.url}")
        print(f"receiver_url={receiver_endpoint.url}")
        print(f"distinct_endpoints={int(receiver_endpoint.address != sender_endpoint.address)}")
        print("instance_identity_verification=operator-must-map-endpoints-to-distinct-instances")
        print(f"tls_verification={'disabled-local-only' if args.insecure else 'enabled'}")
        print(f"messages_attempted={attempts}")
        print(f"messages_acknowledged={acknowledged}")
        print(f"messages_fully_delivered={latencies.count}")
        print(f"deliveries_expected={expected_deliveries}")
        print(f"deliveries_received={delivered}")
        print(f"missing_deliveries={missing}")
        print(f"duplicate_deliveries={duplicates}")
        print(f"out_of_order_deliveries={out_of_order}")
        print(f"protocol_errors={errors}")
        if first_error:
            print(f"first_error={first_error}")
        print(f"sender_echoes_ignored={sender_echoes}")
        print(f"messages_per_second={latencies.count / elapsed:.2f}")
        print(f"acknowledgements_per_second={acknowledged / elapsed:.2f}")
        print(f"deliveries_per_second={delivered / elapsed:.2f}")
        print(f"latency_p50_ms={latencies.percentile(.5):.3f}")
        print(f"latency_p95_ms={latencies.percentile(.95):.3f}")
        print(f"latency_p99_ms={latencies.percentile(.99):.3f}")
        sampler.print_results(elapsed)
        passed = bool(acknowledged) and not (errors or missing or duplicates or out_of_order) and sampler.valid
        print(f"benchmark_result={'passed' if passed else 'failed'}")
        if not passed:
            raise SystemExit(1)
    finally:
        for connection in reversed(all_sockets):
            connection.close()


if __name__ == "__main__":
    main()
