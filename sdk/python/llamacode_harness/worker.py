"""LlamaCode supervised worker SDK. Stdout is reserved for protocol frames."""

from __future__ import annotations

import asyncio
import inspect
import json
import os
import queue
import struct
import sys
import threading
import uuid
from dataclasses import dataclass
from typing import Any, BinaryIO, Callable, Mapping

PROTOCOL = "llamacode-worker-v1"
DEFAULT_MAX_FRAME_BYTES = 1024 * 1024


class FrameError(ValueError):
    pass


class CapabilityError(RuntimeError):
    def __init__(self, code: str, message: str, capability: str = "") -> None:
        super().__init__(message)
        self.code = code
        self.capability = capability


def encode_frame(body: Mapping[str, Any], max_frame_bytes: int = DEFAULT_MAX_FRAME_BYTES) -> bytes:
    if not isinstance(body, Mapping) or body.get("protocol") != PROTOCOL:
        raise FrameError("unsupported worker protocol")
    if not isinstance(body.get("type"), str) or not body["type"]:
        raise FrameError("worker frame has no type")
    raw = json.dumps(body, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    if not raw or len(raw) > max_frame_bytes:
        raise FrameError("worker frame exceeds the configured limit")
    return struct.pack(">I", len(raw)) + raw


def decode_frame(stream: BinaryIO, max_frame_bytes: int = DEFAULT_MAX_FRAME_BYTES) -> dict[str, Any] | None:
    header = stream.read(4)
    if not header:
        return None
    if len(header) != 4:
        raise FrameError("truncated worker frame header")
    size = struct.unpack(">I", header)[0]
    if size == 0 or size > max_frame_bytes:
        raise FrameError("invalid worker frame size")
    raw = stream.read(size)
    if len(raw) != size:
        raise FrameError("truncated worker frame body")
    try:
        body = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise FrameError(f"invalid worker JSON: {exc}") from exc
    if not isinstance(body, dict) or body.get("protocol") != PROTOCOL:
        raise FrameError("unsupported worker protocol")
    if not isinstance(body.get("type"), str) or not body["type"]:
        raise FrameError("worker frame has no type")
    return body


class CapabilityHandle:
    def __init__(self, runtime: "WorkerRuntime", name: str, handle: str, generation: int) -> None:
        self._runtime = runtime
        self.name = name
        self._handle = handle
        self._generation = generation

    def call(self, operation: str, payload: Mapping[str, Any] | None = None) -> dict[str, Any]:
        return self._runtime.request_capability(self, operation, dict(payload or {}))


class CapabilityBroker:
    def __init__(self, runtime: "WorkerRuntime", snapshot: Mapping[str, Any] | None = None) -> None:
        self._runtime = runtime
        self._snapshot = dict(snapshot or {})
        self._grants = self._snapshot.get("grants", {}) or {}
        self.generation = int(self._snapshot.get("generation", 1))

    def names(self) -> list[str]:
        return sorted(name for name, grant in self._grants.items() if self._valid(grant))

    def _valid(self, grant: Any) -> bool:
        return bool(isinstance(grant, dict) and grant.get("granted") and grant.get("handle")
                    and int(grant.get("generation", 0)) == self.generation)

    def has(self, name: str) -> bool:
        return self._valid(self._grants.get(name))

    def require(self, name: str) -> CapabilityHandle:
        grant = self._grants.get(name)
        if not self._valid(grant):
            reason = grant.get("reason", "denied_by_policy") if isinstance(grant, dict) else "denied_by_policy"
            raise CapabilityError("capability_denied", f"{name}: {reason}", name)
        return CapabilityHandle(self._runtime, name, str(grant["handle"]), self.generation)


@dataclass
class CallContext:
    call_id: str
    capabilities: CapabilityBroker
    cancel_event: threading.Event

    @property
    def cancelled(self) -> bool:
        return self.cancel_event.is_set()


class WorkerRuntime:
    def __init__(self, handlers: Mapping[str, Callable[..., Any]] | None = None,
                 max_frame_bytes: int = DEFAULT_MAX_FRAME_BYTES,
                 input_stream: BinaryIO | None = None, output_stream: BinaryIO | None = None) -> None:
        self.handlers = dict(handlers or {})
        self.max_frame_bytes = max_frame_bytes
        self.input = input_stream or sys.stdin.buffer
        self.output = output_stream or sys.stdout.buffer
        self.authenticated = False
        self.nonce = ""
        self.capabilities = CapabilityBroker(self, {})
        self._frames: queue.Queue[Any] = queue.Queue()
        self._pending: dict[str, tuple[threading.Event, dict[str, Any] | None, Exception | None]] = {}
        self._pending_lock = threading.Lock()
        self._write_lock = threading.Lock()
        self._reader_done = threading.Event()
        self._disconnect_error: Exception | None = None
        self._active_calls: dict[str, threading.Event] = {}
        self._active_lock = threading.Lock()

    def _send(self, body: Mapping[str, Any]) -> None:
        frame = encode_frame({"protocol": PROTOCOL, **body}, self.max_frame_bytes)
        with self._write_lock:
            self.output.write(frame)
            self.output.flush()

    def request_capability(self, handle: CapabilityHandle, operation: str,
                           payload: Mapping[str, Any]) -> dict[str, Any]:
        if (not self.authenticated or not self.capabilities.has(handle.name)
                or handle._generation != self.capabilities.generation):
            raise CapabilityError("capability_revoked", "capability handle is no longer valid", handle.name)
        request_id = f"cap-{uuid.uuid4().hex}"
        event = threading.Event()
        with self._pending_lock:
            if self._reader_done.is_set():
                raise CapabilityError("worker_disconnected", "worker host disconnected")
            self._pending[request_id] = (event, None, None)
        try:
            self._send({"type": "capability_call", "requestId": request_id,
                        "capability": handle.name, "handle": handle._handle,
                        "operation": operation, "payload": dict(payload)})
            event.wait()
            with self._pending_lock:
                _, response, error = self._pending.pop(request_id)
            if error:
                raise error
            return response or {}
        finally:
            with self._pending_lock:
                self._pending.pop(request_id, None)

    def _reader(self) -> None:
        try:
            while True:
                frame = decode_frame(self.input, self.max_frame_bytes)
                if frame is None:
                    self._disconnect_error = CapabilityError(
                        "worker_disconnected", "worker host disconnected")
                    self._frames.put(None)
                    return
                if frame.get("type") == "capability_result":
                    self._resolve_capability(frame)
                else:
                    self._frames.put(frame)
        except Exception as exc:  # the main loop turns it into a useful stderr error
            self._frames.put(exc)
        finally:
            self._reader_done.set()

    def _reject_pending_capabilities(self, error: Exception) -> None:
        with self._pending_lock:
            pending = list(self._pending.items())
            for request_id, (event, _, _) in pending:
                # Keep the original request id: request_capability pops it after
                # wake-up, while the event guarantees the caller cannot wait
                # forever after EOF or a framing error.
                self._pending[request_id] = (event, None, error)
        for _, (event, _, _) in pending:
            event.set()

    def _resolve_capability(self, frame: Mapping[str, Any]) -> None:
        request_id = str(frame.get("requestId", ""))
        with self._pending_lock:
            item = self._pending.get(request_id)
            if not item:
                return
            event, _, _ = item
            if frame.get("ok"):
                self._pending[request_id] = (event, dict(frame.get("payload") or {}), None)
            else:
                error = frame.get("error") or {}
                self._pending[request_id] = (event, None, CapabilityError(
                    str(error.get("code", "capability_error")),
                    str(error.get("message", "capability call failed"))))
            event.set()

    def _invoke(self, handler: Callable[..., Any], payload: dict[str, Any], context: CallContext) -> Any:
        result = handler(payload, context)
        if inspect.isawaitable(result):
            return asyncio.run(result)
        return result

    def _handle_call(self, frame: Mapping[str, Any]) -> None:
        call_id = str(frame.get("callId", ""))
        payload = dict(frame.get("payload") or {})
        operation = str(payload.get("operation", "handle"))
        handler = self.handlers.get(operation) or self.handlers.get("handle")
        cancel_event = threading.Event()
        with self._active_lock:
            if call_id in self._active_calls:
                self._send({"type": "result", "callId": call_id, "payload": {
                    "error": {"code": "duplicate_call_id", "message": "worker call id is already in use"}}})
                return
            self._active_calls[call_id] = cancel_event
        try:
            if not call_id or not handler:
                raise RuntimeError(f"unknown worker operation: {operation}")
            context = CallContext(call_id, self.capabilities, cancel_event)
            result = self._invoke(handler, payload, context)
            if not isinstance(result, dict):
                result = {"value": result}
            self._send({"type": "result", "callId": call_id, "payload": result})
        except Exception as exc:
            self._send({"type": "result", "callId": call_id, "payload": {
                "error": {"code": "cancelled" if cancel_event.is_set() else getattr(exc, "code", "worker_error"),
                           "message": str(exc)}}})
        finally:
            with self._active_lock:
                self._active_calls.pop(call_id, None)

    def run(self) -> None:
        reader = threading.Thread(target=self._reader, name="llamacode-worker-reader", daemon=True)
        reader.start()
        while True:
            frame = self._frames.get()
            if frame is None:
                self._reject_pending_capabilities(
                    self._disconnect_error or CapabilityError(
                        "worker_disconnected", "worker host disconnected"))
                return
            if isinstance(frame, Exception):
                self._reject_pending_capabilities(frame)
                raise frame
            kind = frame.get("type")
            if kind == "hello":
                if self.authenticated:
                    raise FrameError("worker authenticated twice")
                expected = os.environ.get("LLAMACODE_WORKER_NONCE", "")
                if expected and frame.get("nonce") != expected:
                    raise FrameError("worker nonce authentication failed")
                self.nonce = str(frame.get("nonce", ""))
                self.capabilities = CapabilityBroker(self, frame.get("capabilities") or {})
                self.authenticated = True
                self._send({"type": "hello_ack", "nonce": self.nonce,
                            "sdk": "llamacode-harness-worker", "sdkVersion": "0.1"})
            elif not self.authenticated:
                raise FrameError("worker sent data before authentication")
            elif kind == "cancel":
                # Cancellation is cooperative. A handler can inspect its
                # context; a future handler may choose to run in its own lane.
                with self._active_lock:
                    event = self._active_calls.get(str(frame.get("callId", "")))
                    if event:
                        event.set()
                continue
            elif kind == "call":
                threading.Thread(target=self._handle_call, args=(frame,),
                                 name="llamacode-worker-call", daemon=True).start()
            elif kind in {"result", "error"}:
                raise FrameError(f"unexpected worker frame: {kind}")


def run_worker(handlers: Mapping[str, Callable[..., Any]], **kwargs: Any) -> None:
    WorkerRuntime(handlers, **kwargs).run()
