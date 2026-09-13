#!/usr/bin/env python3
"""Shared transport and bounded statistics for the benchmark clients."""

import base64
import hashlib
import hmac
import http.client
import json
import math
import os
import socket
import ssl
import time
from dataclasses import dataclass
from urllib.parse import urlsplit


class LatencyHistogram:
    """Fixed memory, with percentile upper bounds within 1% above 1 us."""

    def __init__(self):
        self.counts = [0] * 2048
        self.count = 0
        self.maximum = 0.0

    def add(self, milliseconds):
        index = 0 if milliseconds <= 0.001 else math.ceil(
            math.log(milliseconds / 0.001) / math.log(1.01)
        )
        self.counts[min(index, len(self.counts) - 1)] += 1
        self.count += 1
        self.maximum = max(self.maximum, milliseconds)

    def percentile(self, fraction):
        if not self.count:
            return 0.0
        rank = math.ceil(self.count * fraction)
        cumulative = 0
        for index, count in enumerate(self.counts):
            cumulative += count
            if cumulative >= rank:
                if index == len(self.counts) - 1:
                    return self.maximum
                return min(self.maximum, 0.001 * 1.01 ** index)
        raise AssertionError("invalid histogram")


def make_token(secret, username, lifetime=3600):
    def encode(value):
        return base64.urlsafe_b64encode(value).rstrip(b"=").decode("ascii")

    now = int(time.time())
    header = encode(b'{"alg":"HS256","typ":"JWT"}')
    payload = encode(json.dumps({"sub": username, "iss": "httpServer",
        "jti": encode(os.urandom(16)), "iat": now, "exp": now + lifetime},
        separators=(",", ":")).encode())
    signature = encode(hmac.new(secret.encode(), f"{header}.{payload}".encode(),
                                hashlib.sha256).digest())
    return f"{header}.{payload}.{signature}"


@dataclass(frozen=True)
class Endpoint:
    url: str
    host: str
    port: int
    path: str
    authority: str
    secure: bool

    @classmethod
    def parse(cls, url):
        if any(ord(character) < 33 or ord(character) > 126 for character in url):
            raise ValueError("URL must be ASCII without whitespace or control characters")
        parsed = urlsplit(url)
        if parsed.scheme not in {"ws", "wss", "http", "https"} or not parsed.hostname:
            raise ValueError("URL must use ws://, wss://, http:// or https:// with a host")
        if parsed.username is not None or parsed.password is not None or parsed.fragment:
            raise ValueError("URL must not contain credentials or a fragment")
        secure = parsed.scheme in {"wss", "https"}
        port = parsed.port if parsed.port is not None else (443 if secure else 80)
        if port <= 0:
            raise ValueError("URL port must be between 1 and 65535")
        path = parsed.path or "/ws"
        if parsed.query:
            path += "?" + parsed.query
        authority = f"[{parsed.hostname}]" if ":" in parsed.hostname else parsed.hostname
        if port != (443 if secure else 80):
            authority += f":{port}"
        return cls(url, parsed.hostname, port, path, authority, secure)

    @property
    def origin(self):
        return f"{'https' if self.secure else 'http'}://{self.authority}"

    @property
    def address(self):
        return self.host.lower(), self.port


def tls_context(ca_file=None, insecure=False):
    context = ssl.create_default_context(cafile=ca_file)
    if insecure:
        context.check_hostname = False
        context.verify_mode = ssl.CERT_NONE
    return context


def request_json(endpoint, token, method, path, body=None, context=None, timeout=8):
    if endpoint.secure:
        connection = http.client.HTTPSConnection(endpoint.host, endpoint.port,
                                                 timeout=timeout, context=context)
    else:
        connection = http.client.HTTPConnection(endpoint.host, endpoint.port, timeout=timeout)
    headers = {"Accept": "application/json"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    encoded = None
    if body is not None:
        encoded = json.dumps(body, separators=(",", ":")).encode()
        headers["Content-Type"] = "application/json"
    try:
        connection.request(method, path, body=encoded, headers=headers)
        response = connection.getresponse()
        payload = response.read()
        try:
            decoded = json.loads(payload) if payload else {}
        except json.JSONDecodeError as error:
            raise AssertionError(f"{method} {path}: non-JSON status={response.status}") from error
        return response.status, decoded
    finally:
        connection.close()


def handshake_request(endpoint, auth_token, origin=None):
    origin = origin or endpoint.origin
    if any(ord(character) < 33 or ord(character) > 126 for character in origin):
        raise ValueError("Origin must not contain whitespace or control characters")
    key = base64.b64encode(os.urandom(16)).decode("ascii")
    request = (
        f"GET {endpoint.path} HTTP/1.1\r\nHost: {endpoint.authority}\r\n"
        "Upgrade: websocket\r\nConnection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n"
        f"Sec-WebSocket-Protocol: httpserver.v1, bearer.{auth_token}\r\n"
        f"Origin: {origin}\r\n\r\n"
    )
    return request.encode("ascii"), key


def validate_handshake(response, key, auth_token):
    lines = response.decode("iso-8859-1").split("\r\n")
    if len(lines[0].split()) < 2 or lines[0].split()[1] != "101":
        raise AssertionError(f"WebSocket upgrade failed: {lines[0]}")
    headers = {}
    for line in lines[1:]:
        if line:
            name, value = line.split(":", 1)
            headers[name.lower()] = value.strip()
    expected = base64.b64encode(hashlib.sha1(
        (key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
    if headers.get("sec-websocket-accept") != expected:
        raise AssertionError("WebSocket upgrade returned an invalid accept key")
    if headers.get("sec-websocket-protocol") != "httpserver.v1":
        raise AssertionError("WebSocket upgrade did not select httpserver.v1")
    if auth_token.encode() in response:
        raise AssertionError("WebSocket upgrade reflected the bearer token")


def connect_websocket(endpoint, auth_token, context=None, origin=None, timeout=8):
    connection = socket.create_connection((endpoint.host, endpoint.port), timeout=timeout)
    try:
        if endpoint.secure:
            connection = context.wrap_socket(connection, server_hostname=endpoint.host)
        request, key = handshake_request(endpoint, auth_token, origin)
        connection.sendall(request)
        response = bytearray()
        # Reading only the headers preserves a coalesced first WebSocket frame.
        while not response.endswith(b"\r\n\r\n"):
            chunk = connection.recv(1)
            if not chunk or len(response) >= 16384:
                raise AssertionError("WebSocket upgrade response closed or exceeded header limit")
            response.extend(chunk)
        validate_handshake(bytes(response), key, auth_token)
        return connection
    except BaseException:
        connection.close()
        raise


def receive_frame(connection, deadline):
    def exact(size):
        result = bytearray()
        while len(result) < size:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("WebSocket frame deadline exceeded")
            connection.settimeout(remaining)
            chunk = connection.recv(size - len(result))
            if not chunk:
                raise AssertionError("connection closed during WebSocket frame")
            result.extend(chunk)
        return bytes(result)

    first, second = exact(2)
    if first & 0x70 or second & 0x80 or not first & 0x80:
        raise AssertionError("unexpected reserved bits, mask or fragmented server frame")
    length = second & 0x7F
    if length == 126:
        length = int.from_bytes(exact(2), "big")
    elif length == 127:
        length = int.from_bytes(exact(8), "big")
    if length > 65536 or (first & 0x08 and length > 125):
        raise AssertionError("server frame exceeds benchmark protocol limit")
    return first & 0x0F, exact(length)


def add_transport_arguments(parser):
    parser.add_argument("--ca-file", help="CA bundle for private certificates; verification is on by default")
    parser.add_argument("--insecure", action="store_true", help="disable TLS validation for local rehearsal")
    parser.add_argument("--origin", help="WebSocket Origin; defaults to the endpoint HTTP(S) origin")
    parser.add_argument("--timeout", type=float, default=8, help="maximum seconds per message or handshake")
