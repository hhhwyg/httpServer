#!/usr/bin/env python3
"""Verify a disconnected room member recovers a configurable history window."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
INTEGRATION_DIR = Path(__file__).resolve().parents[1] / "integration"
sys.path.insert(0, str(INTEGRATION_DIR))

from ChatBusinessFlowTest import (  # noqa: E402
    SECRET,
    memory_only_environment,
    receive_json,
    register_and_login,
    request_json,
    send_json,
    subscribe,
)
from WebSocketSmokeTest import connect_websocket, wait_for_server  # noqa: E402


def run_flow(port):
    message_count = int(os.environ.get("HTTPSERVER_RECOVERY_MESSAGE_COUNT", "250"))
    assert message_count > 0
    alice_token = register_and_login(port, "m2-recovery-alice")
    bob_token = register_and_login(port, "m2-recovery-bob")
    status, response = request_json(
        port, "POST", "/api/v1/rooms", alice_token, {"name": "M2 recovery"}
    )
    assert status == 201
    room_id = response["data"]["room"]["id"]
    status, _ = request_json(
        port, "POST", f"/api/v1/rooms/{room_id}/members", bob_token
    )
    assert status == 200

    receiver = connect_websocket(port, bob_token)
    sender = connect_websocket(port, alice_token)
    try:
        subscribe(receiver, room_id, "receiver-before-disconnect")
        receiver.close()
        subscribe(sender, room_id, "sender-subscribe")

        acknowledged_ids = []
        for index in range(message_count):
            send_json(
                sender,
                {
                    "version": 1,
                    "type": "message.send",
                    "requestId": f"send-{index}",
                    "payload": {
                        "roomId": room_id,
                        "clientMessageId": f"offline-message-{index}",
                        "content": f"offline message {index}",
                    },
                },
            )
            ack = receive_json(sender)
            assert ack["type"] == "message.ack"
            acknowledged_ids.append(ack["payload"]["messageId"])

        receiver = connect_websocket(port, bob_token)
        subscribe(receiver, room_id, "receiver-after-reconnect")

        after = "0"
        recovered = []
        pages = 0
        while True:
            status, response = request_json(
                port,
                "GET",
                f"/api/v1/rooms/{room_id}/messages?after={after}&limit=100",
                bob_token,
            )
            assert status == 200
            page = response["data"]
            recovered.extend(page["messages"])
            pages += 1
            next_after = page["nextAfter"]
            if next_after is None:
                break
            assert next_after != after
            after = next_after

        recovered_ids = [item["messageId"] for item in recovered]
        expected_pages = (message_count + 99) // 100
        assert pages == expected_pages
        assert recovered_ids == acknowledged_ids
        assert len(set(recovered_ids)) == message_count
        assert all(
            int(recovered_ids[index]) + 1 == int(recovered_ids[index + 1])
            for index in range(len(recovered_ids) - 1)
        )
        print(
            "reconnect_recovery_acceptance "
            f"pages={pages} persisted_message_records={len(recovered_ids)} "
            f"receiver_messages={len(recovered_ids)} "
            f"first_message_id={recovered_ids[0]} last_message_id={recovered_ids[-1]} "
            "duplicates=0 missing=0 out_of_order=0"
        )
    finally:
        sender.close()
        receiver.close()


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: ReconnectRecoveryTest.py SERVER PORT")
    server, port = sys.argv[1], int(sys.argv[2])
    log_file = tempfile.NamedTemporaryFile(prefix="httpserver-m2-recovery-", delete=False)
    log_path = log_file.name
    log_file.close()
    environment = memory_only_environment()
    process = subprocess.Popen(
        [server, "-b", "epoll", "-t", "2", "-p", str(port), "-l", log_path],
        env=environment,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        wait_for_server(port)
        run_flow(port)
    finally:
        process.terminate()
        try:
            process.wait(timeout=4)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        try:
            os.unlink(log_path)
        except FileNotFoundError:
            pass


if __name__ == "__main__":
    main()
