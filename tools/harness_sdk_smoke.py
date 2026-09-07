"""Cross-runtime smoke for the checked-in Node/Python worker SDKs."""

from __future__ import annotations

import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
PROTOCOL = "llamacode-worker-v1"


def frame(body: dict) -> bytes:
    raw = json.dumps({"protocol": PROTOCOL, **body}, separators=(",", ":")).encode()
    return struct.pack(">I", len(raw)) + raw


def read_frame(stream) -> dict:
    header = stream.read(4)
    if len(header) != 4:
        raise RuntimeError("worker closed before sending a frame")
    size = struct.unpack(">I", header)[0]
    raw = stream.read(size)
    if len(raw) != size:
        raise RuntimeError("worker sent a truncated frame")
    body = json.loads(raw.decode())
    if body.get("protocol") != PROTOCOL:
        raise RuntimeError(f"wrong protocol: {body}")
    return body


def run(label: str, command: list[str], value: str) -> None:
    nonce = uuid.uuid4().hex
    env = os.environ.copy()
    env["LLAMACODE_WORKER_NONCE"] = nonce
    proc = subprocess.Popen(command, cwd=ROOT, stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
    try:
        assert proc.stdin and proc.stdout
        proc.stdin.write(frame({"type": "hello", "nonce": nonce, "network": False,
                                "capabilities": {"generation": 1, "grants": {}}}))
        proc.stdin.flush()
        hello = read_frame(proc.stdout)
        if hello.get("type") != "hello_ack" or hello.get("nonce") != nonce:
            raise RuntimeError(f"{label} handshake failed: {hello}")
        proc.stdin.write(frame({"type": "call", "callId": "smoke-1",
                                "payload": {"operation": "echo", "value": value}}))
        proc.stdin.flush()
        result = read_frame(proc.stdout)
        payload = result.get("payload", {})
        if result.get("type") != "result" or payload.get("value") != value:
            raise RuntimeError(f"{label} result failed: {result}")
        print(f"{label.upper()}_SDK_OK")
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=3)


def main() -> int:
    run("node", ["node", "sdk/node/examples/echo-worker.mjs"], "node-smoke")
    run("python", [sys.executable, "sdk/python/examples/echo_worker.py"], "python-smoke")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
