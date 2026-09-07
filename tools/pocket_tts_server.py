#!/usr/bin/env python3
"""Local resident adapter for Kyutai Pocket TTS.

The Qt application starts this process with a managed Python environment.  The
model and voice conditioning are loaded once, then /v1/audio/speech streams a
PCM16 WAV response compatible with LlamaCode's TTS client.
"""

from __future__ import annotations

import argparse
import json
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import struct
import sys
from typing import Any


def configure_cache(cache_dir: str | None) -> None:
    if not cache_dir:
        return
    cache = str(Path(cache_dir).expanduser())
    Path(cache).mkdir(parents=True, exist_ok=True)
    os.environ.setdefault("HF_HOME", cache)
    os.environ.setdefault("HUGGINGFACE_HUB_CACHE", str(Path(cache) / "hub"))


def load_model(args: argparse.Namespace) -> tuple[Any, Any, int]:
    configure_cache(args.cache_dir)
    from pocket_tts import TTSModel, get_default_voice_for_language

    model = TTSModel.load_model(
        language=args.language,
        config=args.config or "default",
        quantize=args.quantize,
    )
    voice = args.voice or get_default_voice_for_language(args.language, args.config or "default")
    voice_path = Path(voice).expanduser()
    if voice_path.is_file():
        if voice_path.suffix.lower() == ".safetensors":
            voice_state = model.get_state_for_audio_prompt(voice_path)
        else:
            voice_state = model.get_state_for_audio_prompt(voice_path, truncate=True)
    else:
        voice_state = model.get_state_for_audio_prompt(voice)
    return model, voice_state, int(model.sample_rate)


def wav_stream_header(sample_rate: int, channels: int = 1) -> bytes:
    # Unknown RIFF/data sizes are intentional: the HTTP response is chunked and
    # the Qt client consumes the data chunk while generation is still running.
    byte_rate = sample_rate * channels * 2
    block_align = channels * 2
    return struct.pack(
        "<4sI4s4sIHHIIHH4sI",
        b"RIFF", 0xFFFFFFFF, b"WAVE", b"fmt ", 16, 1, channels,
        sample_rate, byte_rate, block_align, 16, b"data", 0xFFFFFFFF,
    )


def pcm16_from_tensor(chunk: Any) -> bytes:
    import torch

    samples = (
        chunk.detach()
        .to(device="cpu", dtype=torch.float32)
        .flatten()
        .clamp(-1.0, 1.0)
        .mul(32767.0)
        .to(dtype=torch.int16)
    )
    return samples.numpy().tobytes()


class PocketHandler(BaseHTTPRequestHandler):
    server_version = "LlamaCode-PocketTTS/1"

    def log_message(self, fmt: str, *values: Any) -> None:
        print("[pocket-tts] " + (fmt % values), file=sys.stderr, flush=True)

    @property
    def state(self) -> dict[str, Any]:
        return self.server.pocket_state  # type: ignore[attr-defined]

    def send_json(self, status: int, payload: dict[str, Any]) -> None:
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self) -> None:
        if self.path.rstrip("/") == "/health":
            self.send_json(200, {"ok": True, "engine": "pocket-tts"})
            return
        self.send_json(404, {"error": "not found"})

    def do_POST(self) -> None:
        if self.path.rstrip("/") != "/v1/audio/speech":
            self.send_json(404, {"error": "not found"})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            request = json.loads(self.rfile.read(length).decode("utf-8"))
            text = str(request.get("input", "")).strip()
            if not text:
                self.send_json(400, {"error": {"message": "input is empty"}})
                return
            model = self.state["model"]
            voice_state = self.state["voice_state"]
            sample_rate = self.state["sample_rate"]
            self.send_response(200)
            self.send_header("Content-Type", "audio/wav")
            self.send_header("Transfer-Encoding", "chunked")
            self.send_header("Connection", "close")
            self.end_headers()
            self.write_chunk(wav_stream_header(sample_rate))
            for chunk in model.generate_audio_stream(voice_state, text):
                pcm = pcm16_from_tensor(chunk)
                if pcm:
                    self.write_chunk(pcm)
            self.wfile.write(b"0\r\n\r\n")
            self.wfile.flush()
        except Exception as exc:  # the client will treat a broken stream as a TTS error
            print(f"[pocket-tts] synthesis failed: {exc}", file=sys.stderr, flush=True)
            self.close_connection = True

    def write_chunk(self, data: bytes) -> None:
        self.wfile.write(f"{len(data):X}\r\n".encode("ascii"))
        self.wfile.write(data)
        self.wfile.write(b"\r\n")
        self.wfile.flush()


class PocketServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="LlamaCode Pocket TTS local server")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8200)
    parser.add_argument("--language", default="spanish")
    parser.add_argument("--voice", default="")
    parser.add_argument("--config", default="")
    parser.add_argument("--cache-dir", default="")
    parser.add_argument("--quantize", action="store_true")
    parser.add_argument("--prepare", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        model, voice_state, sample_rate = load_model(args)
    except Exception as exc:
        print(f"[pocket-tts] unable to load model/voice: {exc}", file=sys.stderr, flush=True)
        return 2
    if args.prepare:
        print("Pocket TTS model and voice ready", flush=True)
        return 0

    server = PocketServer((args.host, args.port), PocketHandler)
    server.pocket_state = {
        "model": model,
        "voice_state": voice_state,
        "sample_rate": sample_rate,
    }
    print(f"Pocket TTS listening on http://{args.host}:{args.port}", flush=True)
    try:
        server.serve_forever(poll_interval=0.2)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
