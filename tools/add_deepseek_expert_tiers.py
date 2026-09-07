#!/usr/bin/env python3
"""Agrega variantes DeepSeek con más expertos MoE en GPU0.

La variante existente ``VRAM balance`` es el tier 0-1. Este script agrega los
tiers 0-5 y 0-9 clonando su configuración completa y cambiando únicamente el
override de expertos. Ejecutar con LlamaCode cerrado para evitar que el cierre
de la app sobrescriba profiles/*.json.

Uso:
  python tools/add_deepseek_expert_tiers.py          # dry-run
  python tools/add_deepseek_expert_tiers.py --apply
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
BASE_ID = "6b3bf7bd-0889-491a-9b6d-b12128478a5f"
TIERS = (5, 9)


def items(data):
    return data if isinstance(data, list) else data.get("items", data.get("profiles"))


def expert_override(max_gpu0_block: int) -> str:
    gpu0 = rf"blk\.({'|'.join(str(i) for i in range(max_gpu0_block + 1))})\.ffn_(gate|up|down)_exps\.weight=CUDA0"
    gpu1 = r"blk\.(37|38|39|40|41|42)\.ffn_(gate|up|down)_exps\.weight=CUDA1"
    cpu = r"blk\.[0-9]+\.ffn_(gate|up|down)_exps\.weight=CPU"
    return f"{gpu0},{gpu1},{cpu}"


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
    with open(PROFILE, encoding="utf-8") as fh:
        raw = fh.read()
    data = json.loads(raw)
    launches = items(data)
    base = next((p for p in launches if p.get("id") == BASE_ID), None)
    if base is None:
        raise SystemExit(f"No existe el perfil base {BASE_ID}")

    existing = {p.get("name") for p in launches}
    additions = []
    for tier in TIERS:
        name = f"141_QUALITY - DeepSeek Fusion leloch · VRAM experts 0-{tier}"
        if name in existing:
            print(f"skip: {name}")
            continue
        clone = copy.deepcopy(base)
        clone["id"] = str(uuid.uuid4())
        clone["name"] = name
        clone["alias"] = f"deepseek-vram-0-{tier}"
        clone["favorite"] = False
        clone["best"] = False
        clone["lastUsed"] = 0
        args = list(clone.get("extraArgs", []))
        set_arg(args, "-ot", expert_override(tier))
        clone["extraArgs"] = args
        additions.append(clone)

    if not additions:
        print("No hay perfiles nuevos para agregar.")
        return 0
    for clone in additions:
        launches.append(clone)
        print(f"{'APPLY' if apply else 'DRY-RUN'}: {clone['name']} [{clone['id']}]")

    if not apply:
        return 0
    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    backup = PROFILE + f".bak.deepseek-tiers.{stamp}"
    with open(backup, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(raw)
    with open(PROFILE, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(data, fh, ensure_ascii=False, indent=2)
        fh.write("\n")
    print(f"Escrito {PROFILE}; backup {backup}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
