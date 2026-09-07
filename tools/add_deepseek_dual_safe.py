"""Create DeepSeek 0-2/0-3 copies with a safer dual-GPU split."""

from __future__ import annotations

import json
import uuid
from copy import deepcopy
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LAUNCHES = ROOT / "profiles" / "launches.json"
RUNTIMES = ROOT / "profiles" / "runtimes.json"

SOURCES = {
    "392ea030-059e-4f69-86c6-81d3fa31acbc": ("157", "0-2"),
    "6d4b528f-f26d-4500-99cf-c25a36dd6f54": ("158", "0-3"),
}


def main() -> None:
    data = json.loads(LAUNCHES.read_text(encoding="utf-8"))
    runtimes = json.loads(RUNTIMES.read_text(encoding="utf-8"))
    low_gpu_runtime_id = "f2d6ab5c-6db6-45a3-9c73-39d53a83cc01"
    if not any(item.get("id") == low_gpu_runtime_id for item in runtimes):
        runtimes.append({
            "batch": 128,
            "cacheType": "q4_0",
            "ctx": 32768,
            "contBatching": False,
            "flashAttention": True,
            "gpuLayers": 20,
            "id": low_gpu_runtime_id,
            "mlock": False,
            "mmap": True,
            "name": "DeepSeek dual GPU · 20 layers · 32k",
            "parallelSlots": 1,
            "threads": 8,
            "ubatch": 32,
        })
    output = []
    for source_id, (number, tier) in SOURCES.items():
        source = next(item for item in data if item.get("id") == source_id)
        clone = deepcopy(source)
        clone["id"] = str(uuid.uuid4())
        clone["name"] = f"141_QUALITY - DeepSeek Fusion · expertos {tier} · dual GPU safe 65k"
        clone["alias"] = f"QUALITY-DeepSeek-Fusion-experts-{tier}-dual-gpu-safe-65k"
        clone["agentProfileId"] = "agent-maximo"
        clone["runtimePresetId"] = "abb643a3-c8b9-442a-a8fe-28abb0381978"
        clone["best"] = False
        clone["lastUsed"] = 0
        args = list(clone.get("extraArgs", []))
        if "--tensor-split" in args:
            args[args.index("--tensor-split") + 1] = "1,1"
        if "--predict" in args:
            args[args.index("--predict") + 1] = "4096"
        clone["extraArgs"] = args
        data.append(clone)
        output.append((number, clone["id"], clone["name"]))

        compact = deepcopy(clone)
        compact["id"] = str(uuid.uuid4())
        compact["name"] = f"141_QUALITY - DeepSeek Fusion · expertos {tier} · dual GPU safe 32k"
        compact["alias"] = f"QUALITY-DeepSeek-Fusion-experts-{tier}-dual-gpu-safe-32k"
        compact["runtimePresetId"] = "d5f2b7d6-3b67-4d90-9c3a-9b0e4d6a1f20"
        data.append(compact)
        output.append((f"{number}-32k", compact["id"], compact["name"]))

        tilted = deepcopy(compact)
        tilted["id"] = str(uuid.uuid4())
        tilted["name"] = f"141_QUALITY - DeepSeek Fusion · expertos {tier} · dual GPU tilted 32k"
        tilted["alias"] = f"QUALITY-DeepSeek-Fusion-experts-{tier}-dual-gpu-tilted-32k"
        args = list(tilted.get("extraArgs", []))
        if "--tensor-split" in args:
            args[args.index("--tensor-split") + 1] = "1.1,0.9"
        tilted["extraArgs"] = args
        data.append(tilted)
        output.append((f"{number}-tilted", tilted["id"], tilted["name"]))

        low_gpu = deepcopy(compact)
        low_gpu["id"] = str(uuid.uuid4())
        low_gpu["name"] = f"141_QUALITY - DeepSeek Fusion · expertos {tier} · dual GPU 20 layers"
        low_gpu["alias"] = f"QUALITY-DeepSeek-Fusion-experts-{tier}-dual-gpu-20layers"
        low_gpu["runtimePresetId"] = low_gpu_runtime_id
        data.append(low_gpu)
        output.append((f"{number}-20layers", low_gpu["id"], low_gpu["name"]))

    backup = LAUNCHES.with_suffix(".json.bak.deepseek-dual-safe")
    backup.write_text(LAUNCHES.read_text(encoding="utf-8"), encoding="utf-8")
    LAUNCHES.write_text(json.dumps(data, ensure_ascii=False, indent=4) + "\n", encoding="utf-8")
    RUNTIMES.write_text(json.dumps(runtimes, ensure_ascii=False, indent=4) + "\n", encoding="utf-8")
    for row in output:
        print(" | ".join(row))


if __name__ == "__main__":
    main()
