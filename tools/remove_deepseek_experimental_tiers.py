#!/usr/bin/env python3
"""Retira tiers DeepSeek 0-5/0-9 ya descartados; ejecutarlo con la app cerrada."""
from __future__ import annotations

import datetime as dt
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROFILE = ROOT / "profiles" / "launches.json"
REMOVE = {
    "f3d000b7-59da-4035-9114-f326515ba95d",  # VRAM experts 0-5
    "78929286-486e-43a2-a97b-25f251d34254",  # VRAM experts 0-9
    "b493dc59-5e6d-4327-b1d8-3b4e59a89c03",  # 0-5 HE0 safe
    "2ae89282-9bc1-4459-ac57-180a075a65ff",  # 0-5 CUDA stable
}

raw = PROFILE.read_text(encoding="utf-8")
data = json.loads(raw)
items = data if isinstance(data, list) else data.get("items", data.get("profiles"))
removed = [p for p in items if p.get("id") in REMOVE]
if not removed:
    print("No había perfiles experimentales para retirar.")
else:
    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    backup = PROFILE.with_name(PROFILE.name + f".bak.remove-deepseek-tiers.{stamp}")
    backup.write_text(raw, encoding="utf-8", newline="\n")
    items[:] = [p for p in items if p.get("id") not in REMOVE]
    PROFILE.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"Retirados {len(removed)} perfiles; backup: {backup}")
    for p in removed:
        print(f"- {p.get('id')}: {p.get('name')}")
