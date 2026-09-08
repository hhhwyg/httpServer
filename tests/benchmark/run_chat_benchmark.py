#!/usr/bin/env python3
"""Single-room WebSocket business benchmark with ACK and delivery checks."""
import argparse
import json
import math
import os
import sys
import time
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "integration"))
from WebSocketSmokeTest import (  # noqa: E402
    SECRET, connect_websocket, make_token, recv_frame,
    request_json, send_masked_frame, wait_for_server,
)


def percentile(values, fraction):
    if not values:
        return 0.0
    values.sort()
    return values[min(len(values) - 1, math.ceil(len(values) * fraction) - 1)]


def subscribe(sock, room_id, request_id):
    send_masked_frame(sock, 0x1, json.dumps({"version": 1, "type": "room.subscribe",
        "requestId": request_id, "payload": {"roomId": room_id}}, separators=(",", ":")).encode())
    _, payload = recv_frame(sock)
    reply = json.loads(payload)
    assert reply["type"] == "room.subscribed", reply


def create_room(port, token):
    status, response = request_json(
        port, token, "POST", "/api/v1/rooms", {"name": "benchmark-room"}
    )
    assert status == 201 and response["ok"], response
    return response["data"]["room"]["id"]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--duration", type=float, required=True)
    parser.add_argument("--jwt-secret", required=True)
    parser.add_argument("--recipients", type=int, default=1)
    args = parser.parse_args()
    if args.duration <= 0:
        parser.error("duration must be positive")
    if args.recipients <= 0:
        parser.error("recipients must be positive")
    if args.jwt_secret.encode() != SECRET:
        parser.error("this benchmark currently requires the WebSocket fixture secret")
    wait_for_server(args.port)
    sender_token = make_token("bench-sender")
    receiver_tokens = [make_token(f"bench-receiver-{index}")
                       for index in range(args.recipients)]
    room_id = create_room(args.port, sender_token)
    for receiver_token in receiver_tokens:
        status, response = request_json(
            args.port, receiver_token, "POST", f"/api/v1/rooms/{room_id}/members"
        )
        assert status == 200 and response["ok"], response

    latencies, sent, delivered, errors = [], 0, 0, 0
    sender = connect_websocket(args.port, sender_token)
    receivers = [connect_websocket(args.port, token) for token in receiver_tokens]
    try:
        subscribe(sender, room_id, "sender-subscribe")
        for index, receiver in enumerate(receivers):
            subscribe(receiver, room_id, f"receiver-{index}-subscribe")
        # Subscription work is serialized through the bounded business
        # executor; let both owning EventLoops observe the completed attach.
        time.sleep(0.05)
        deadline = time.monotonic() + args.duration
        while time.monotonic() < deadline:
            message_id = f"bench-{sent}"
            request_id = f"request-{sent}"
            started = time.perf_counter_ns()
            send_masked_frame(sender, 0x1, json.dumps({"version": 1, "type": "message.send",
                "requestId": request_id, "payload": {"roomId": room_id,
                "clientMessageId": message_id, "content": "benchmark"}}, separators=(",", ":")).encode())
            _, ack_payload = recv_frame(sender)
            ack = json.loads(ack_payload)
            message_valid = ack.get("type") == "message.ack" and ack.get("requestId") == request_id
            if not message_valid:
                errors += 1
                continue
            for receiver in receivers:
                _, delivered_payload = recv_frame(receiver)
                event = json.loads(delivered_payload)
                message_valid = message_valid and (
                    event.get("type") == "message.created"
                    and event.get("payload", {}).get("message", {}).get("clientMessageId")
                    == message_id
                )
            if not message_valid:
                errors += 1
                continue
            sent += 1
            delivered += len(receivers)
            latencies.append((time.perf_counter_ns() - started) / 1_000_000)
    finally:
        for receiver in receivers:
            receiver.close()
        sender.close()

    print("tool=python-websocket-chat-benchmark")
    print(f"duration_seconds={args.duration:.3f}")
    print(f"recipients={args.recipients}")
    print(f"messages_sent={sent}")
    print(f"messages_delivered={delivered}")
    print(f"errors={errors}")
    print(f"messages_per_second={sent / args.duration:.2f}")
    print(f"latency_p50_ms={percentile(latencies, .5):.3f}")
    print(f"latency_p95_ms={percentile(latencies, .95):.3f}")
    print(f"latency_p99_ms={percentile(latencies, .99):.3f}")


if __name__ == "__main__":
    main()
