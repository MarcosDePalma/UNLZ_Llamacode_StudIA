#!/usr/bin/env python3
"""Agrega variantes experimentales DeepSeek con expertos 0-2 y 0-3 en GPU0."""
from __future__ import annotations

import copy
import datetime as dt
import json
import os
import uuid

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROFILE = os.path.join(ROOT, "profiles", "launches.json")
BASE_ID = "6b3bf7bd-0889-491a-9b6d-b12128478a5f"
TIERS = (2, 3)


def expert_override(tier: int) -> str:
    gpu0 = rf"blk\.({'|'.join(str(i) for i in range(tier + 1))})\.ffn_(gate|up|down)_exps\.weight=CUDA0"
    gpu1 = r"blk\.(37|38|39|40|41|42)\.ffn_(gate|up|down)_exps\.weight=CUDA1"
    cpu = r"blk\.[0-9]+\.ffn_(gate|up|down)_exps\.weight=CPU"
    return f"{gpu0},{gpu1},{cpu}"


def set_arg(args: list[str], flag: str, value: str) -> None:
    try:
        i = args.index(flag)
    except ValueError:
        args.extend([flag, value])
        return
    args[i + 1] = value


raw = open(PROFILE, encoding="utf-8").read()
data = json.loads(raw)
items = data if isinstance(data, list) else data.get("items", data.get("profiles"))
base = next(p for p in items if p.get("id") == BASE_ID)
existing = {p.get("name") for p in items}
added = []
for tier in TIERS:
    name = f"141_QUALITY - DeepSeek Fusion leloch · VRAM experts 0-{tier}"
    if name in existing:
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
    items.append(clone)
    added.append(clone)

if not added:
    print("No hay variantes nuevas.")
else:
    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    backup = PROFILE + f".bak.deepseek-02-03.{stamp}"
    open(backup, "w", encoding="utf-8", newline="\n").write(raw)
    open(PROFILE, "w", encoding="utf-8", newline="\n").write(json.dumps(data, ensure_ascii=False, indent=2) + "\n")
    for p in added:
        print(f"Agregado {p['name']} [{p['id']}]")
    print(f"Backup: {backup}")
