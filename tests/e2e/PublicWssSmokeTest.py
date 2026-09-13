#!/usr/bin/env python3
"""Exercise the public HTTPS/WSS business path with certificate validation."""

import argparse
import base64
import hashlib
import http.client
import json
import os
import socket
import ssl
import struct
import time
from urllib.parse import urlsplit


def request_json(connection, method, path, body=None, token=None):
    encoded = None if body is None else json.dumps(body, separators=(",", ":")).encode()
    headers = {"Accept": "application/json"}
    if encoded is not None:
        headers["Content-Type"] = "application/json"
    if token:
        headers["Authorization"] = f"Bearer {token}"
    connection.request(method, path, body=encoded, headers=headers)
    response = connection.getresponse()
    payload = response.read()
    try:
        decoded = json.loads(payload) if payload else {}
    except json.JSONDecodeError as error:
        raise AssertionError(
            f"{method} {path} returned non-JSON status={response.status}: {payload!r}"
        ) from error
    return response.status, decoded


def recv_exact(connection, size):
    result = b""
    while len(result) < size:
        chunk = connection.recv(size - len(result))
        if not chunk:
            raise AssertionError("WSS connection closed unexpectedly")
        result += chunk
    return result


def recv_until(connection, marker, maximum=16384):
    result = b""
    while marker not in result:
        result += recv_exact(connection, 1)
        if len(result) > maximum:
            raise AssertionError("WSS handshake exceeded header limit")
    return result


def send_frame(connection, opcode, payload):
    mask = os.urandom(4)
    length = len(payload)
    if length <= 125:
        header = bytes((0x80 | opcode, 0x80 | length))
    elif length <= 0xFFFF:
        header = bytes((0x80 | opcode, 0xFE)) + struct.pack("!H", length)
    else:
        raise AssertionError("smoke-test frame is too large")
    masked = bytes(value ^ mask[index % 4] for index, value in enumerate(payload))
    connection.sendall(header + mask + masked)


def recv_frame(connection):
    first, second = recv_exact(connection, 2)
    if first & 0x70:
        raise AssertionError("server set a reserved WebSocket bit")
    if second & 0x80:
        raise AssertionError("server frames must not be masked")
    length = second & 0x7F
    if length == 126:
        length = struct.unpack("!H", recv_exact(connection, 2))[0]
    elif length == 127:
        length = struct.unpack("!Q", recv_exact(connection, 8))[0]
    return first & 0x0F, recv_exact(connection, length)


def connect_wss(parsed, token, origin, context):
    port = parsed.port or 443
    raw = socket.create_connection((parsed.hostname, port), timeout=8)
    connection = context.wrap_socket(raw, server_hostname=parsed.hostname)
    connection.settimeout(8)
    key = base64.b64encode(os.urandom(16)).decode("ascii")
    path = parsed.path or "/ws"
    if parsed.query:
        path += "?" + parsed.query
    host = parsed.hostname if port == 443 else f"{parsed.hostname}:{port}"
    request = (
        f"GET {path} HTTP/1.1\r\n"
        f"Host: {host}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        f"Sec-WebSocket-Protocol: httpserver.v1, bearer.{token}\r\n"
        f"Origin: {origin}\r\n"
        "\r\n"
    )
    connection.sendall(request.encode("ascii"))
    response = recv_until(connection, b"\r\n\r\n")
    status_line = response.split(b"\r\n", 1)[0]
    if b" 101 " not in status_line:
        raise AssertionError(f"WSS upgrade failed: {status_line.decode('ascii', 'replace')}")
    expected = base64.b64encode(hashlib.sha1(
        (key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode("ascii")
    ).digest())
    headers = response.lower()
    if b"sec-websocket-accept: " + expected.lower() not in headers:
        raise AssertionError("WSS upgrade returned an invalid accept key")
    if b"sec-websocket-protocol: httpserver.v1" not in headers:
        raise AssertionError("WSS upgrade did not select the application subprotocol")
    if token.encode() in response:
        raise AssertionError("WSS upgrade reflected the bearer token")
    return connection


def send_json(connection, value):
    send_frame(connection, 0x1, json.dumps(value, separators=(",", ":")).encode())


def recv_json(connection):
    opcode, payload = recv_frame(connection)
    if opcode != 0x1:
        raise AssertionError(f"expected text frame, got opcode={opcode}")
    return json.loads(payload)


def subscribe(connection, room_id, request_id):
    send_json(connection, {"version": 1, "type": "room.subscribe",
                           "requestId": request_id, "payload": {"roomId": room_id}})
    response = recv_json(connection)
    assert response.get("type") == "room.subscribed", response


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("url", help="public wss:// URL, for example wss://chat.example.com/ws")
    parser.add_argument("--origin", help="expected browser Origin; defaults to the HTTPS site origin")
    parser.add_argument("--ca-file", help="additional CA bundle used to verify a private test certificate")
    parser.add_argument("--insecure", action="store_true", help="local self-signed rehearsal only")
    args = parser.parse_args()
    parsed = urlsplit(args.url)
    if parsed.scheme != "wss" or not parsed.hostname:
        parser.error("url must use wss:// and contain a hostname")
    context = ssl.create_default_context(cafile=args.ca_file)
    if args.insecure:
        context.check_hostname = False
        context.verify_mode = ssl.CERT_NONE
    port = parsed.port or 443
    origin = args.origin or f"https://{parsed.hostname}" + (f":{port}" if port != 443 else "")
    suffix = f"{int(time.time())}-{os.getpid()}"
    password = "Public-smoke-password-2026"

    def api_connection():
        return http.client.HTTPSConnection(parsed.hostname, port, context=context, timeout=8)

    tokens = []
    for role in ("sender", "receiver"):
        username = f"public-smoke-{role}-{suffix}"
        connection = api_connection()
        try:
            status, response = request_json(
                connection, "POST", "/register", {"username": username, "password": password}
            )
            assert status == 201 and response.get("ok") is True, response
            status, response = request_json(
                connection, "POST", "/login", {"username": username, "password": password}
            )
            assert status == 200 and isinstance(response.get("token"), str), response
            tokens.append(response["token"])
        finally:
            connection.close()

    connection = api_connection()
    try:
        status, response = request_json(
            connection, "POST", "/api/v1/rooms", {"name": "public-smoke-room"}, tokens[0]
        )
        assert status == 201 and response.get("ok") is True, response
        room_id = response["data"]["room"]["id"]
        status, response = request_json(
            connection, "POST", f"/api/v1/rooms/{room_id}/members", None, tokens[1]
        )
        assert status == 200 and response.get("ok") is True, response
    finally:
        connection.close()

    sender = connect_wss(parsed, tokens[0], origin, context)
    receiver = connect_wss(parsed, tokens[1], origin, context)
    try:
        subscribe(sender, room_id, "public-subscribe-sender")
        subscribe(receiver, room_id, "public-subscribe-receiver")
        client_message_id = f"public-message-{suffix}"
        send_json(sender, {"version": 1, "type": "message.send",
                           "requestId": "public-send", "payload": {
                               "roomId": room_id,
                               "clientMessageId": client_message_id,
                               "content": "public WSS smoke test"}})
        sender_responses = [recv_json(sender), recv_json(sender)]
        acknowledgement = next(
            (item for item in sender_responses if item.get("type") == "message.ack"), None
        )
        sender_delivery = next(
            (item for item in sender_responses if item.get("type") == "message.created"), None
        )
        delivered = recv_json(receiver)
        assert acknowledgement is not None, sender_responses
        assert sender_delivery is not None, sender_responses
        message = delivered.get("payload", {}).get("message", {})
        sender_message = sender_delivery.get("payload", {}).get("message", {})
        assert delivered.get("type") == "message.created", delivered
        assert message.get("clientMessageId") == client_message_id, delivered
        assert message.get("messageId") == acknowledgement["payload"]["messageId"], delivered
        assert sender_message.get("messageId") == message.get("messageId"), sender_delivery
        send_frame(sender, 0x9, b"health")
        opcode, payload = recv_frame(sender)
        assert opcode == 0xA and payload == b"health"
        tls_mode = "insecure-local" if args.insecure else "verified"
        print(f"public_wss_acceptance room={room_id} message={message['messageId']} delivery=1 tls={tls_mode}")
    finally:
        try:
            send_frame(receiver, 0x8, struct.pack("!H", 1000))
            send_frame(sender, 0x8, struct.pack("!H", 1000))
        finally:
            receiver.close()
            sender.close()


if __name__ == "__main__":
    main()
