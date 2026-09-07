#!/usr/bin/env python3
"""Crea una variante 0-5 con una ruta CUDA más conservadora para HE0."""
from __future__ import annotations

import copy
import datetime as dt
import json
import os
import sys
import uuid

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROFILE = os.path.join(ROOT, "profiles", "launches.json")
BASE_ID = "b493dc59-5e6d-4327-b1d8-3b4e59a89c03"
NAME = "141_QUALITY - DeepSeek Fusion leloch · VRAM experts 0-5 · CUDA stable"


def main() -> int:
    apply = "--apply" in sys.argv
    raw = open(PROFILE, encoding="utf-8").read()
    data = json.loads(raw)
    launches = data if isinstance(data, list) else data.get("items", data.get("profiles"))
    if any(p.get("name") == NAME for p in launches):
        print(f"skip: {NAME}")
        return 0
    base = next((p for p in launches if p.get("id") == BASE_ID), None)
    if base is None:
        raise SystemExit(f"No existe el perfil base {BASE_ID}")
    clone = copy.deepcopy(base)
    clone["id"] = str(uuid.uuid4())
    clone["name"] = NAME
    clone["alias"] = "deepseek-vram-0-5-cuda-stable"
    clone["favorite"] = False
    clone["best"] = False
    clone["lastUsed"] = 0
    args = list(clone.get("extraArgs", []))
    for flag in ("--flash-attn", "--mmap"):
        if flag in args:
            i = args.index(flag)
            args[i + 1] = "off" if flag == "--flash-attn" else "off"
    if "--flash-attn" not in args:
        args.extend(["--flash-attn", "off"])
    if "--no-mmap" not in args:
        args.append("--no-mmap")
    clone["extraArgs"] = args
    print(f"{'APPLY' if apply else 'DRY-RUN'}: {NAME} [{clone['id']}]")
    if not apply:
        return 0
    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    backup = PROFILE + f".bak.deepseek-stable-05.{stamp}"
    open(backup, "w", encoding="utf-8", newline="\n").write(raw)
    launches.append(clone)
    open(PROFILE, "w", encoding="utf-8", newline="\n").write(json.dumps(data, ensure_ascii=False, indent=2) + "\n")
    print(f"Escrito {PROFILE}; backup {backup}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
