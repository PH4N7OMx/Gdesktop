import argparse
import json
import queue
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path


IDENTITY = {"protocolVersion": 2, "instanceId": "native-test", "generation": 1}
LIMIT = 2 * 1024 * 1024
HOST = None
DRIVER = None


def command(executable, *arguments):
    return ([str(DRIVER)] if DRIVER else []) + [str(executable), *map(str, arguments)]


def encode(frame):
    data = json.dumps(frame, ensure_ascii=False).encode("utf-8")
    return struct.pack(">I", len(data)) + data


class Host:
    def __init__(self):
        self.process = subprocess.Popen(command(HOST), stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.frames = queue.Queue()
        threading.Thread(target=self._read, daemon=True).start()
        try:
            self.hello = self.receive()
            assert self.hello["type"] == "hello", self.hello
            assert self.hello["protocolVersion"] == 2, self.hello
            assert self.hello["sandbox"] == {
                "win32": "windows-appcontainer", "darwin": "macos-app-sandbox"
            }.get(sys.platform, "linux-seccomp"), self.hello
        except BaseException:
            self.close()
            raise

    def _read(self):
        try:
            while True:
                header = self.process.stdout.read(4)
                if len(header) != 4:
                    raise EOFError("Host closed the transport")
                size = struct.unpack(">I", header)[0]
                if not 0 < size <= LIMIT:
                    raise ValueError("Invalid frame length")
                data = self.process.stdout.read(size)
                if len(data) != size:
                    raise EOFError("Truncated frame")
                self.frames.put(json.loads(data))
        except Exception as error:
            self.frames.put(error)

    def receive(self, timeout=5):
        deadline = time.monotonic() + timeout
        while True:
            frame = self.frames.get(timeout=max(0, deadline - time.monotonic()))
            if isinstance(frame, Exception):
                raise frame
            if frame.get("type") != "turnComplete":
                return frame

    def send(self, frame, fragmented=False):
        data = encode({**IDENTITY, **frame})
        if fragmented:
            for byte in data:
                self.process.stdin.write(bytes([byte]))
                self.process.stdin.flush()
        else:
            self.process.stdin.write(data)
            self.process.stdin.flush()

    def initialize(self, code, fragmented=False):
        self.send({"type": "init", "settings": {}, "package": {
            "formatVersion": 2, "manifest": {"id": "test.native", "name": "Native test", "version": "2.0.0",
                "apiVersion": 2, "capabilities": []}, "code": code,
        }}, fragmented)

    def close(self):
        try:
            if self.process.poll() is None:
                self.process.kill()
            self.process.communicate(timeout=5)
        finally:
            for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
                if stream:
                    stream.close()


class HostTests(unittest.TestCase):
    def setUp(self):
        self.host = Host()
        self.addCleanup(self.host.close)

    def test_fragmented_initialization_and_graceful_stop(self):
        self.host.initialize("export default api => { api.log.info('Привет'); };", fragmented=True)
        log = self.host.receive()
        self.assertEqual(log["message"], "Привет")
        self.assertEqual(self.host.receive()["type"], "ready")
        self.host.send({"type": "stop"})
        self.assertEqual(self.host.receive()["type"], "stopped")
        self.assertEqual(self.host.process.wait(timeout=5), 0)

    def test_activation_waits_for_real_api_response(self):
        self.host.initialize("export default async api => { await api.storage.get('startup'); };")
        request = self.host.receive()
        self.assertEqual(request["method"], "storage.get")
        with self.assertRaises(queue.Empty):
            self.host.receive(timeout=0.1)
        self.host.send({"type": "result", "requestId": request["requestId"], "value": None})
        self.assertEqual(self.host.receive()["type"], "ready")

    def test_scheduler_and_actual_send_request_without_telegram(self):
        self.host.initialize("""export default async api => {
            await api.schedule.every('neutral', 60, async () => {
                await api.telegram.sendText({chatId:'-123', text:'Test payload'});
            });
        };""")
        registration = self.host.receive()
        self.assertEqual(registration["params"], {"id": "neutral", "seconds": 60, "first": "after-period"})
        self.host.send({"type": "result", "requestId": registration["requestId"], "value": True})
        self.assertEqual(self.host.receive()["type"], "ready")
        self.host.send({"type": "event", "eventId": 1, "event": "schedule", "scheduleId": "neutral"})
        request = self.host.receive()
        self.assertEqual(request["method"], "telegram.sendText")
        self.assertEqual(request["params"], {"chatId": "-123", "text": "Test payload", "silent": False})
        self.host.send({"type": "result", "requestId": request["requestId"], "value": {"messageId": "1"}})
        self.assertEqual(self.host.receive()["outcome"], "completed")

    def test_infinite_synchronous_execution_is_interrupted(self):
        self.host.initialize("export default () => { while (true) {} };")
        self.assertEqual(self.host.receive()["type"], "fatal")
        self.assertNotEqual(self.host.process.wait(timeout=5), 0)

    def test_infinite_microtask_queue_is_interrupted(self):
        self.host.initialize("export default () => { const next = () => Promise.resolve().then(next); next(); };")
        received = [self.host.receive()]
        if received[0]["type"] == "ready":
            received.append(self.host.receive())
        self.assertEqual(received[-1]["type"], "fatal")
        self.assertNotEqual(self.host.process.wait(timeout=5), 0)

    def test_native_import_is_unavailable(self):
        self.host.initialize("import fs from 'node:fs'; export default () => fs.readFileSync('/etc/passwd');")
        self.assertEqual(self.host.receive()["type"], "fatal")
        self.assertNotEqual(self.host.process.wait(timeout=5), 0)

    def test_stale_generation_is_rejected(self):
        self.host.initialize("export default () => {};")
        self.assertEqual(self.host.receive()["type"], "ready")
        self.host.send({"type": "stop", "generation": 2})
        self.assertEqual(self.host.receive()["type"], "fatal")

    def test_oversized_frame_is_rejected_before_reading_body(self):
        self.host.process.stdin.write(struct.pack(">I", LIMIT + 1))
        self.host.process.stdin.flush()
        self.assertEqual(self.host.receive()["type"], "fatal")
        self.assertNotEqual(self.host.process.wait(timeout=5), 0)

    def test_invalid_utf8_is_rejected(self):
        data = b'{"type":"\xff"}'
        self.host.process.stdin.write(struct.pack(">I", len(data)) + data)
        self.host.process.stdin.flush()
        self.assertEqual(self.host.receive()["type"], "fatal")
        self.assertNotEqual(self.host.process.wait(timeout=5), 0)

    def test_truncated_frame_does_not_look_like_clean_eof(self):
        self.host.process.stdin.write(struct.pack(">I", 100) + b"{")
        self.host.process.stdin.close()
        self.host.process.stdin = None
        self.assertEqual(self.host.receive()["type"], "fatal")
        self.assertNotEqual(self.host.process.wait(timeout=5), 0)


def check_isolation(probe):
    with tempfile.TemporaryDirectory(prefix="jelly-private-") as temporary:
        sensitive = Path(temporary) / "private.txt"
        sensitive.write_text("native fixture; no account data", encoding="utf-8")
        with socket.socket() as server:
            server.bind(("127.0.0.1", 0))
            server.listen(1)
            result = subprocess.run(command(probe, sensitive, server.getsockname()[1]),
                capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, (result.stdout, result.stderr, result.returncode)
        assert json.loads(result.stdout) == {"readDenied": True, "writeDenied": True, "networkDenied": True}
    print("Native sandbox: fixture read/write and loopback connection denied", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("host", type=Path)
    parser.add_argument("--driver", type=Path)
    parser.add_argument("--probe", type=Path, required=True)
    arguments = parser.parse_args()
    HOST = arguments.host.resolve(strict=True)
    DRIVER = arguments.driver.resolve(strict=True) if arguments.driver else None
    if sys.platform == "win32" and not DRIVER:
        parser.error("Windows tests require the isolated test driver")
    check_isolation(arguments.probe.resolve(strict=True))
    unittest.main(argv=[sys.argv[0]], verbosity=2)
