#!/usr/bin/env python3
"""Run directly; set HTTPSERVER_BENCH_TEST_SERVER for real-server regression."""

import base64
import hashlib
import http.server
import json
import os
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

from benchmark_common import Endpoint, LatencyHistogram, connect_websocket, receive_frame, request_json, tls_context
from run_chat_benchmark import ResourceSampler, drain_receivers, read_process_totals, validate_delivery

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


class BenchmarkUtilityTest(unittest.TestCase):
    def test_url_retains_remote_host_tls_path_query_and_ipv6(self):
        endpoint = Endpoint.parse("wss://example.test/custom/ws?room=1")
        self.assertEqual((endpoint.host, endpoint.port, endpoint.path, endpoint.secure),
                         ("example.test", 443, "/custom/ws?room=1", True))
        self.assertEqual(Endpoint.parse("ws://[::1]:9090/ws").authority, "[::1]:9090")
        for invalid in ("ws://user:pass@example.test/ws", "ftp://example.test", "ws://a/ws\r\nX:1", "ws://a:0/ws"):
            with self.assertRaises(ValueError):
                Endpoint.parse(invalid)

    def test_histogram_memory_is_bounded_and_quantiles_are_upper_bounds(self):
        histogram = LatencyHistogram()
        for _ in range(100):
            for value in range(1, 1001):
                histogram.add(value)
        self.assertEqual(histogram.count, 100000)
        self.assertEqual(len(histogram.counts), 2048)
        for fraction in (.5, .95, .99):
            self.assertGreaterEqual(histogram.percentile(fraction), fraction * 1000)
            self.assertLessEqual(histogram.percentile(fraction), fraction * 1010)

    def test_delivery_rejects_same_client_id_with_wrong_server_id_or_content(self):
        ack = {"messageId": "42", "createdAt": "2026-09-12T00:00:00Z"}
        message = {**ack, "roomId": "7", "clientMessageId": "bench-0-0", "content": "hello"}
        event = {"type": "message.created", "payload": {"message": message}}
        validate_delivery(event, ack, "7", "bench-0-0", "hello")
        for key in ("messageId", "roomId", "createdAt", "content"):
            with self.subTest(key=key), self.assertRaises(AssertionError):
                invalid = {"type": "message.created", "payload": {"message": {**message, key: "wrong"}}}
                validate_delivery(invalid, ack, "7", "bench-0-0", "hello")

    def test_final_duplicate_is_detected(self):
        client, server = socket.socketpair()
        try:
            payload = b'{"type":"message.created","payload":{"message":{"clientMessageId":"bench-0-0"}}}'
            server.sendall(bytes((0x81, len(payload))) + payload)
            self.assertEqual(drain_receivers([{"receivers": [client], "last_sequence": [0]}], .03), (1, 0))
        finally:
            client.close()
            server.close()

    def test_oversized_frame_is_rejected_before_payload_allocation(self):
        client, server = socket.socketpair()
        try:
            server.sendall(b"\x81\x7f" + (2 ** 63).to_bytes(8, "big"))
            with self.assertRaisesRegex(AssertionError, "exceeds benchmark protocol limit"):
                receive_frame(client, time.monotonic() + .1)
        finally:
            client.close()
            server.close()

    def test_slow_frame_uses_one_absolute_deadline(self):
        client, server = socket.socketpair()
        def trickle():
            try:
                server.sendall(b"\x81\x04")
                for _ in range(4):
                    time.sleep(.04)
                    server.sendall(b"x")
            except OSError:
                pass
        thread = threading.Thread(target=trickle)
        thread.start()
        started = time.monotonic()
        try:
            with self.assertRaises(TimeoutError):
                receive_frame(client, started + .09)
            self.assertLess(time.monotonic() - started, .5)
        finally:
            client.close()
            thread.join(timeout=1)
            server.close()

    def test_resource_snapshots_and_invalid_pid(self):
        self.assertEqual(read_process_totals([os.getpid()])["processes"], 1)
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "samples.jsonl"
            sampler = ResourceSampler([os.getpid()], output, .01)
            sampler.start()
            time.sleep(.04)
            sampler.stop()
            rows = [json.loads(line) for line in output.read_text().splitlines()]
            self.assertTrue(sampler.valid)
            self.assertGreaterEqual(len(rows), 3)
            self.assertEqual((rows[0]["phase"], rows[-1]["phase"]), ("start", "end"))
            self.assertTrue(all(row["client"]["rss_kib"] > 0 for row in rows))
        self.assertFalse(ResourceSampler([2147483647]).valid)

    def test_cross_instance_requires_distinct_endpoint_before_network(self):
        result = subprocess.run([sys.executable, str(HERE / "run_chat_benchmark.py"),
            "--url", "ws://127.0.0.1:9/ws", "--receiver-url", "ws://127.0.0.1:9/other",
            "--require-cross-instance", "--duration", "1"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("distinct sender and receiver endpoints", result.stderr)

    def test_ping_connection_failure_is_nonzero(self):
        with socket.socket() as unused:
            unused.bind(("127.0.0.1", 0))
            port = unused.getsockname()[1]
            environment = {**os.environ, "HTTPSERVER_BENCH_JWT_SECRET": "benchmark-unit-secret"}
            result = subprocess.run([sys.executable, str(HERE / "run_websocket_benchmark.py"),
                "--url", f"ws://127.0.0.1:{port}/ws", "--duration", ".1", "--connections", "2"],
                capture_output=True, text=True, env=environment, timeout=5)
        self.assertEqual(result.returncode, 1)
        self.assertIn("successful_handshakes=0", result.stdout)
        self.assertIn("benchmark_result=failed", result.stdout)


class TransportTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="httpserver-benchmark-tls-")
        cls.certificate = str(Path(cls.directory.name) / "certificate.pem")
        key = str(Path(cls.directory.name) / "key.pem")
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
            "-subj", "/CN=localhost", "-addext", "subjectAltName=IP:127.0.0.2,DNS:localhost",
            "-keyout", key, "-out", cls.certificate], check=True, capture_output=True)
        cls.observed = []
        observed = cls.observed

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                observed.append((self.path, dict(self.headers)))
                if self.headers.get("Upgrade") == "websocket":
                    accept = base64.b64encode(hashlib.sha1((self.headers["Sec-WebSocket-Key"] +
                        "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
                    self.send_response(101)
                    self.send_header("Upgrade", "websocket")
                    self.send_header("Connection", "Upgrade")
                    self.send_header("Sec-WebSocket-Accept", accept)
                    self.send_header("Sec-WebSocket-Protocol", "httpserver.v1")
                    self.end_headers()
                    self.wfile.write(b"\x81\x02{}")
                    self.wfile.flush()
                    self.connection.settimeout(.5)
                    try:
                        self.connection.recv(7)
                    except OSError:
                        pass
                else:
                    payload = b'{"ok":true}'
                    self.send_response(200)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Content-Length", str(len(payload)))
                    self.end_headers()
                    self.wfile.write(payload)

            def log_message(self, *args):
                pass

        cls.server = http.server.ThreadingHTTPServer(("127.0.0.2", 0), Handler)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(cls.certificate, key)
        cls.server.socket = context.wrap_socket(cls.server.socket, server_side=True)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=2)
        cls.directory.cleanup()

    def test_remote_address_https_and_wss_verify_certificate_and_keep_first_frame(self):
        endpoint = Endpoint.parse(f"wss://127.0.0.2:{self.server.server_port}/custom/ws?room=1")
        context = tls_context(self.certificate)
        self.assertEqual(request_json(endpoint, "", "GET", "/api/check", context=context),
                         (200, {"ok": True}))
        with connect_websocket(endpoint, "test-token", context) as connection:
            self.assertEqual(connection.recv(4), b"\x81\x02{}")
        path, headers = self.observed[-1]
        self.assertEqual(path, "/custom/ws?room=1")
        self.assertEqual(headers["Host"], endpoint.authority)
        self.assertEqual(headers["Sec-WebSocket-Protocol"], "httpserver.v1, bearer.test-token")
        self.assertEqual(headers["Origin"], endpoint.origin)

    def test_untrusted_certificate_is_rejected(self):
        endpoint = Endpoint.parse(f"wss://127.0.0.2:{self.server.server_port}/ws")
        with self.assertRaises(ssl.SSLCertVerificationError):
            connect_websocket(endpoint, "test-token", tls_context())

    def test_async_ping_uses_tls_and_fails_on_invalid_pong(self):
        endpoint = f"wss://127.0.0.2:{self.server.server_port}/ws"
        result = subprocess.run([sys.executable, str(HERE / "run_websocket_benchmark.py"),
            "--url", endpoint, "--duration", ".1", "--connections", "1",
            "--ca-file", self.certificate], capture_output=True, text=True, timeout=5,
            env={**os.environ, "HTTPSERVER_BENCH_JWT_SECRET": "benchmark-unit-secret"})
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("successful_handshakes=1", result.stdout)
        self.assertIn("first_error=invalid pong", result.stdout)
        self.assertIn("benchmark_result=failed", result.stdout)


@unittest.skipUnless(os.environ.get("HTTPSERVER_BENCH_TEST_SERVER"), "set HTTPSERVER_BENCH_TEST_SERVER")
class RealServerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="httpserver-benchmark-real-")
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            cls.port = listener.getsockname()[1]
        cls.secret = "benchmark-isolated-regression-secret-2026"
        environment = {key: value for key, value in os.environ.items() if not key.startswith("HTTPSERVER_")}
        environment.update({"HTTPSERVER_JWT_SECRET": cls.secret, "HTTPSERVER_AUTH_STORE": "memory",
                            "HTTPSERVER_MESSAGE_MAX_ATTEMPTS": "1000000"})
        cls.process = subprocess.Popen([os.environ["HTTPSERVER_BENCH_TEST_SERVER"], "-b", "epoll",
            "-t", "2", "-p", str(cls.port), "-l", str(Path(cls.directory.name) / "server.log")],
            env=environment, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(80):
            try:
                with socket.create_connection(("127.0.0.1", cls.port), timeout=.2):
                    return
            except OSError:
                time.sleep(.1)
        raise AssertionError("isolated benchmark server did not become ready")

    @classmethod
    def tearDownClass(cls):
        cls.process.terminate()
        try:
            cls.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            cls.process.kill()
            cls.process.wait(timeout=5)
        cls.directory.cleanup()

    def run_client(self, script, arguments):
        result = subprocess.run([sys.executable, str(HERE / script), "--url",
            f"ws://localhost:{self.port}/ws", "--duration", ".3", *arguments],
            env={**os.environ, "HTTPSERVER_BENCH_JWT_SECRET": self.secret},
            text=True, capture_output=True, timeout=20, cwd=ROOT)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("benchmark_result=passed", result.stdout)
        return result.stdout

    def test_chat_fanout_resource_and_rate(self):
        output = self.run_client("run_chat_benchmark.py", ["--auth-mode", "fixture", "--rooms", "2",
            "--recipients", "3", "--message-rate", "20", "--server-pids", str(self.process.pid),
            "--resource-output", str(Path(self.directory.name) / "real.resources.jsonl"),
            "--sample-interval", ".05"])
        self.assertIn("connections=8", output)
        self.assertIn("messages_acknowledged=6", output)
        self.assertIn("deliveries_received=18", output)
        self.assertIn("server_resource_sampling=valid", output)

    def test_ping_multiple_connections(self):
        output = self.run_client("run_websocket_benchmark.py", ["--connections", "4"])
        self.assertIn("successful_handshakes=4", output)
        self.assertIn("errors=0", output)

    def test_chat_api_auth_can_be_repeated(self):
        for _ in range(2):
            output = self.run_client("run_chat_benchmark.py", ["--auth-mode", "api",
                "--recipients", "1", "--message-rate", "10"])
            self.assertIn("messages_acknowledged=3", output)

    def test_wrapper_preserves_failed_stderr_and_exit_status(self):
        with tempfile.TemporaryDirectory() as directory:
            environment = {key: value for key, value in os.environ.items()
                           if not key.startswith("HTTPSERVER_BENCH_")}
            environment.update({"HTTPSERVER_BENCH_URL": f"ws://localhost:{self.port}/not-websocket",
                "HTTPSERVER_BENCH_DURATION_SECONDS": ".1", "HTTPSERVER_BENCH_WORKLOAD": "ping",
                "HTTPSERVER_BENCH_CONNECTIONS": "1", "HTTPSERVER_BENCH_JWT_SECRET": self.secret,
                "HTTPSERVER_BENCH_RESULTS_DIR": directory})
            result = subprocess.run(["bash", str(HERE / "run_websocket_benchmark.sh"),
                "deploy/benchmark/scenario-single-instance.env"], env=environment, cwd=ROOT,
                text=True, capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            artifacts = list(Path(directory).glob("*.txt"))
            self.assertEqual(len(artifacts), 1)
            self.assertIn("benchmark_result=failed", artifacts[0].read_text())

    def test_soak_wrapper_honors_all_overrides_and_preserves_evidence(self):
        environment = {key: value for key, value in os.environ.items() if not key.startswith("HTTPSERVER_BENCH_")}
        environment.update({"HTTPSERVER_BENCH_URL": f"ws://localhost:{self.port}/ws",
            "HTTPSERVER_BENCH_DURATION_SECONDS": ".3", "HTTPSERVER_BENCH_ROOM_FANOUT": "2",
            "HTTPSERVER_BENCH_ROOMS": "2", "HTTPSERVER_BENCH_MESSAGE_BYTES": "32",
            "HTTPSERVER_BENCH_MESSAGE_RATE": "20", "HTTPSERVER_BENCH_AUTH_MODE": "fixture",
            "HTTPSERVER_BENCH_JWT_SECRET": self.secret, "HTTPSERVER_ALLOW_LONG_SOAK": "1",
            "HTTPSERVER_BENCH_RESULTS_DIR": self.directory.name})
        result = subprocess.run(["bash", str(HERE / "run_soak_test.sh"),
            "deploy/benchmark/scenario-soak-24h.env"], env=environment, cwd=ROOT,
            text=True, capture_output=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("auth_mode=fixture", result.stdout)
        self.assertIn("message_bytes=32", result.stdout)
        self.assertIn("message_rate_limit=20.0", result.stdout)
        self.assertIn("connections=6", result.stdout)
        self.assertIn("deliveries_received=12", result.stdout)
        self.assertTrue(list(Path(self.directory.name).glob("websocket-*.resources.jsonl")))


if __name__ == "__main__":
    unittest.main()
