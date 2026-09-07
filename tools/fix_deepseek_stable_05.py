#!/usr/bin/env python3
"""Corrige la variante 0-5 estable: Q4 KV exige Flash Attention."""
from __future__ import annotations
import datetime as dt
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROFILE = os.path.join(ROOT, "profiles", "launches.json")
PROFILE_ID = "2ae89282-9bc1-4459-ac57-180a075a65ff"

raw = open(PROFILE, encoding="utf-8").read()
data = json.loads(raw)
launches = data if isinstance(data, list) else data.get("items", data.get("profiles"))
profile = next(p for p in launches if p.get("id") == PROFILE_ID)
args = list(profile.get("extraArgs", []))
i = args.index("--flash-attn")
args[i + 1] = "on"
profile["extraArgs"] = args
stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
backup = PROFILE + f".bak.deepseek-stable-05-fix.{stamp}"
open(backup, "w", encoding="utf-8", newline="\n").write(raw)
open(PROFILE, "w", encoding="utf-8", newline="\n").write(json.dumps(data, ensure_ascii=False, indent=2) + "\n")
print(f"Corregido {profile['name']} [{PROFILE_ID}]; backup {backup}")
