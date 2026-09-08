#!/usr/bin/env python3
"""Cover owner/member/non-member/expired-token authorization decisions."""

import os
from pathlib import Path
import json
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "integration"))

from WebSocketSmokeTest import (  # noqa: E402
    SECRET, b64url, connect_websocket, create_room, isolated_websocket_environment,
    make_token, recv_frame, request_json, send_masked_frame, wait_for_server,
)


def expired_token(username):
    import base64
    import hashlib
    import hmac
    import json

    now = int(time.time())
    header = b64url(json.dumps({"alg": "HS256", "typ": "JWT"}, separators=(",", ":")).encode())
    payload = b64url(json.dumps({
        "sub": username, "iss": "httpServer", "jti": b64url(b"m8-expired-token-jti"),
        "iat": now - 120, "exp": now - 60,
    }, separators=(",", ":")).encode())
    signing_input = f"{header}.{payload}".encode("ascii")
    signature = b64url(hmac.new(SECRET, signing_input, hashlib.sha256).digest())
    return f"{header}.{payload}.{signature}"


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: AuthorizationMatrixTest.py SERVER PORT")
    server, port = sys.argv[1], int(sys.argv[2])
    with tempfile.NamedTemporaryFile(prefix="httpserver-m8-matrix-", delete=False) as log:
        log_path = log.name
    environment = isolated_websocket_environment()
    process = subprocess.Popen(
        [server, "-b", "epoll", "-t", "1", "-p", str(port), "-l", log_path],
        env=environment, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    try:
        wait_for_server(port)
        owner, member, outsider = make_token("m8-owner"), make_token("m8-member"), make_token("m8-outsider")
        room_id = create_room(port, owner)
        status, _ = request_json(port, outsider, "GET", f"/api/v1/rooms/{room_id}/members")
        assert status == 403
        status, body = request_json(port, member, "POST", f"/api/v1/rooms/{room_id}/members")
        assert status == 200 and body["data"]["joined"] is True
        status, body = request_json(port, member, "GET", f"/api/v1/rooms/{room_id}/members")
        assert status == 200 and set(body["data"]["members"]) == {"m8-owner", "m8-member"}
        with connect_websocket(port, member) as socket_member:
            send_masked_frame(socket_member, 0x1, json.dumps({
                "version": 1, "type": "room.subscribe", "requestId": "member-subscribe",
                "payload": {"roomId": room_id},
            }, separators=(",", ":")).encode())
            _, payload = recv_frame(socket_member)
            member_response = json.loads(payload)
            assert member_response["type"] == "room.subscribed", member_response
        with connect_websocket(port, outsider) as socket_outsider:
            send_masked_frame(socket_outsider, 0x1, json.dumps({
                "version": 1, "type": "room.subscribe", "requestId": "outsider-subscribe",
                "payload": {"roomId": room_id},
            }, separators=(",", ":")).encode())
            _, payload = recv_frame(socket_outsider)
            outsider_response = json.loads(payload)
            assert outsider_response["error"]["code"] == "room_membership_required", outsider_response
        status, _ = request_json(port, expired_token("m8-owner"), "GET", "/api/v1/rooms")
        assert status == 401
        print("authorization_matrix_acceptance owner=200 member=200 nonmember=403 ws_member=ok ws_nonmember=denied expired=401")
    finally:
        process.terminate()
        try:
            process.wait(timeout=4)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        os.unlink(log_path)


if __name__ == "__main__":
    main()
