#!/usr/bin/env python3
"""Crea una variante segura del tier DeepSeek 0-5 para validar HE0.

Conserva el reparto de expertos 0-5 y limita únicamente el presupuesto de
generación y la presión de contexto/batch. El perfil histórico no se modifica.
Ejecutar con LlamaCode cerrado.
"""
from __future__ import annotations

import copy
import datetime as dt
import json
import os
import sys
import uuid

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROFILE = os.path.join(ROOT, "profiles", "launches.json")
BASE_ID = "f3d000b7-59da-4035-9114-f326515ba95d"
NAME = "141_QUALITY - DeepSeek Fusion leloch · VRAM experts 0-5 · HE0 safe"


def items(data):
    return data if isinstance(data, list) else data.get("items", data.get("profiles"))


def set_arg(args: list[str], flag: str, value: str) -> None:
    try:
        i = args.index(flag)
    except ValueError:
        args.extend([flag, value])
        return
    if i + 1 >= len(args):
        args.append(value)
    else:
        args[i + 1] = value


def main() -> int:
    apply = "--apply" in sys.argv
    raw = open(PROFILE, encoding="utf-8").read()
    data = json.loads(raw)
    launches = items(data)
    if any(p.get("name") == NAME for p in launches):
        print(f"skip: {NAME}")
        return 0
    base = next((p for p in launches if p.get("id") == BASE_ID), None)
    if base is None:
        raise SystemExit(f"No existe el perfil base {BASE_ID}")

    clone = copy.deepcopy(base)
    clone["id"] = str(uuid.uuid4())
    clone["name"] = NAME
    clone["alias"] = "deepseek-vram-0-5-he0-safe"
    clone["favorite"] = False
    clone["best"] = False
    clone["lastUsed"] = 0
    args = list(clone.get("extraArgs", []))
    set_arg(args, "--predict", "4096")
    set_arg(args, "--ctx-size", "65536")
    set_arg(args, "--batch-size", "2048")
    set_arg(args, "--ubatch-size", "512")
    clone["extraArgs"] = args
    print(f"{'APPLY' if apply else 'DRY-RUN'}: {NAME} [{clone['id']}]")
    if not apply:
        return 0

    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    backup = PROFILE + f".bak.deepseek-safe-05.{stamp}"
    open(backup, "w", encoding="utf-8", newline="\n").write(raw)
    launches.append(clone)
    open(PROFILE, "w", encoding="utf-8", newline="\n").write(
        json.dumps(data, ensure_ascii=False, indent=2) + "\n"
    )
    print(f"Escrito {PROFILE}; backup {backup}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
