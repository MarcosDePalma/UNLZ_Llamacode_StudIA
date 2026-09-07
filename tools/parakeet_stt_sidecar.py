#!/usr/bin/env python3
"""Parakeet rolling STT sidecar for LlamaCode.

The Qt application talks to this process through NDJSON v1:

  LlamaCode -> {type: config|audio|end|cancel}
  sidecar   -> {type: ready|partial|final|error}

This adapter deliberately keeps the neural runtime outside the Qt binary.  It
uses NeMo's batch decoder over the growing utterance, so it provides rolling
partials without pretending that Parakeet TDT is a stateful streaming session.
That makes it useful as an experimental quality/latency adapter while a true
streaming runtime is evaluated separately.
"""

from __future__ import annotations

import argparse
import base64
import json
import sys
import time
from typing import Any


def emit(message: dict[str, Any]) -> None:
    sys.stdout.write(json.dumps(message, ensure_ascii=False, separators=(",", ":")) + "\n")
    sys.stdout.flush()


def hypothesis_text(value: Any) -> str:
    if isinstance(value, str):
        return value.strip()
    text = getattr(value, "text", None)
    if text is not None:
        return str(text).strip()
    return str(value).strip()


class RollingParakeet:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.sample_rate = args.sample_rate
        self.language = args.language
        self.model_name = args.model
        self.model = None
        self.numpy = None
        self.audio = bytearray()
        self.last_decode = 0.0
        self.last_text = ""

    def load(self) -> None:
        try:
            import numpy as np  # type: ignore
            import nemo.collections.asr as nemo_asr  # type: ignore
        except Exception as exc:  # pragma: no cover - depends on user runtime
            raise RuntimeError(
                "Faltan dependencias del sidecar: instalá numpy y nemo_toolkit[asr]"
            ) from exc

        self.numpy = np
        if self.args.model_path:
            self.model = nemo_asr.models.ASRModel.restore_from(self.args.model_path)
        else:
            self.model = nemo_asr.models.ASRModel.from_pretrained(
                model_name=self.model_name
            )
        if self.args.device:
            self.model = self.model.to(self.args.device)
        self.model.eval()

    def decode(self, final: bool) -> str:
        if not self.audio:
            return self.last_text
        if self.model is None:
            self.load()
        if self.numpy is None or self.model is None:
            return self.last_text

        # PCM16 mono little-endian, exactly what VoiceController captures.
        samples = self.numpy.frombuffer(bytes(self.audio), dtype=self.numpy.int16)
        if samples.size < max(1, int(self.sample_rate * self.args.min_audio_ms / 1000)):
            return self.last_text
        waveform = samples.astype(self.numpy.float32) / 32768.0
        result = self.model.transcribe([waveform], batch_size=1, verbose=False)
        if isinstance(result, tuple):
            result = result[0]
        if not result:
            return self.last_text
        text = hypothesis_text(result[0])
        changed = bool(text and text != self.last_text)
        if changed:
            self.last_text = text
            # El controlador emite el único `final` después del último decode;
            # así no hay dos finales si el último parcial ya contenía el mismo
            # prefijo completo.
            if not final:
                emit({"type": "partial", "text": text})
        return self.last_text

    def add_audio(self, payload: str) -> None:
        try:
            self.audio.extend(base64.b64decode(payload, validate=True))
        except Exception as exc:
            raise RuntimeError(f"audio base64 inválido: {exc}") from exc
        max_bytes = int(self.sample_rate * self.args.max_audio_ms / 1000) * 2
        if len(self.audio) > max_bytes:
            del self.audio[: len(self.audio) - max_bytes]
        now = time.monotonic()
        if now - self.last_decode >= self.args.partial_interval_ms / 1000:
            self.last_decode = now
            self.decode(final=False)

    def reset(self) -> None:
        self.audio.clear()
        self.last_decode = 0.0
        self.last_text = ""


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", default="nvidia/parakeet-tdt-0.6b-v3")
    parser.add_argument("--model-path", default="")
    parser.add_argument("--device", default="", help="cpu, cuda o cuda:0")
    parser.add_argument("--sample-rate", type=int, default=16000)
    parser.add_argument("--language", default="es")
    parser.add_argument("--partial-interval-ms", type=int, default=350)
    parser.add_argument("--min-audio-ms", type=int, default=250)
    parser.add_argument("--max-audio-ms", type=int, default=60000)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.partial_interval_ms < 50 or args.min_audio_ms < 50 or args.max_audio_ms < 1000:
        emit({"type": "error", "error": "intervalos de audio inválidos"})
        return 2

    sidecar = RollingParakeet(args)
    configured = False
    emit({"type": "ready", "protocol": "ndjson-v1"})
    for raw in sys.stdin:
        try:
            message = json.loads(raw)
            kind = str(message.get("type", "")).strip().lower()
            if kind == "config":
                sidecar.sample_rate = int(message.get("sample_rate", args.sample_rate))
                sidecar.language = str(message.get("language", args.language))
                configured = True
                continue
            if kind == "audio":
                if not configured:
                    raise RuntimeError("se recibió audio antes de config")
                sidecar.add_audio(str(message.get("pcm16_base64", "")))
                continue
            if kind == "end":
                sidecar.decode(final=True)
                emit({"type": "final", "text": sidecar.last_text})
                sidecar.reset()
                configured = False
                continue
            if kind == "cancel":
                sidecar.reset()
                configured = False
                continue
        except Exception as exc:  # pragma: no cover - runtime/dependency errors
            emit({"type": "error", "error": str(exc)})
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
