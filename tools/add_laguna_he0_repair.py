"""Create isolated Laguna variants with the agent profile used by valid runs.

The historical Laguna launch entries had an empty agentProfileId.  This script
keeps those entries unchanged and creates a CPU-safe 32k copy with the
maximal tool-capable agent explicitly attached.
"""

from __future__ import annotations

import json
import uuid
from copy import deepcopy
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LAUNCHES = ROOT / "profiles" / "launches.json"
RUNTIMES = ROOT / "profiles" / "runtimes.json"
SOURCE_ID = "318368e6-3fb7-4ef8-a76a-23030c544c49"
HISTORIC_SOURCE_ID = "8d0dd2e0-c6c6-41ef-81d6-893c20d2f621"


def main() -> None:
    data = json.loads(LAUNCHES.read_text(encoding="utf-8"))
    runtimes = json.loads(RUNTIMES.read_text(encoding="utf-8"))
    source = next(item for item in data if item.get("id") == SOURCE_ID)
    clone = deepcopy(source)
    clone["id"] = "bd16d671-9958-403f-92b7-c7327b67b5bc"
    clone["name"] = "152_BALANCE - Laguna S 2.1 · CPU-safe 32k · agent-maximo"
    clone["alias"] = "BALANCE-Laguna-S-2.1-A8B-Q2-cpu-safe-32k-agent-maximo"
    clone["agentProfileId"] = "agent-maximo"
    clone["best"] = False
    clone["lastUsed"] = 0
    if not any(item.get("id") == clone["id"] for item in data):
        data.append(clone)

    historic = next(item for item in data if item.get("id") == HISTORIC_SOURCE_ID)
    fit_on = deepcopy(historic)
    fit_on["id"] = str(uuid.uuid4())
    fit_on["name"] = "153_BALANCE - Laguna S 2.1 · fit on 100k · agent-maximo"
    fit_on["alias"] = "BALANCE-Laguna-S-2.1-A8B-Q2-fit-on-100k-agent-maximo"
    fit_on["agentProfileId"] = "agent-maximo"
    fit_on["best"] = False
    fit_on["lastUsed"] = 0
    if not any(item.get("name") == fit_on["name"] for item in data):
        data.append(fit_on)

    cpu_only = deepcopy(source)
    cpu_only["id"] = str(uuid.uuid4())
    cpu_only["name"] = "154_BALANCE - Laguna S 2.1 · CPU-only HE0 · agent-maximo"
    cpu_only["alias"] = "BALANCE-Laguna-S-2.1-A8B-Q2-cpu-only-he0-agent-maximo"
    cpu_only["agentProfileId"] = "agent-maximo"
    cpu_only["runtimePresetId"] = "863fb56c-76e0-4c99-9c2e-dee56bee42a0"
    cpu_only["best"] = False
    cpu_only["lastUsed"] = 0
    if not any(item.get("name") == cpu_only["name"] for item in data):
        data.append(cpu_only)

    cpu_fast = deepcopy(cpu_only)
    cpu_fast["id"] = str(uuid.uuid4())
    cpu_fast["name"] = "155_BALANCE - Laguna S 2.1 · CPU-only HE/BCB · predict 512"
    cpu_fast["alias"] = "BALANCE-Laguna-S-2.1-A8B-Q2-cpu-only-he-bcb-predict-512"
    args = list(cpu_fast.get("extraArgs", []))
    if "--predict" in args:
        args[args.index("--predict") + 1] = "512"
    cpu_fast["extraArgs"] = args
    cpu_fast["best"] = False
    cpu_fast["lastUsed"] = 0
    if not any(item.get("name") == cpu_fast["name"] for item in data):
        data.append(cpu_fast)

    gpu_runtime_id = "d5f2b7d6-3b67-4d90-9c3a-9b0e4d6a1f20"
    if not any(item.get("id") == gpu_runtime_id for item in runtimes):
        runtimes.append({
            "batch": 128,
            "cacheType": "q4_0",
            "ctx": 32768,
            "contBatching": False,
            "flashAttention": True,
            "gpuLayers": 999,
            "id": gpu_runtime_id,
            "mlock": False,
            "mmap": True,
            "name": "Laguna S 2.1 · dual GPU safe · 32k",
            "parallelSlots": 1,
            "threads": 8,
            "ubatch": 32,
        })

    gpu_balanced = deepcopy(source)
    gpu_balanced["id"] = str(uuid.uuid4())
    gpu_balanced["name"] = "156_BALANCE - Laguna S 2.1 · dual GPU safe · 32k"
    gpu_balanced["alias"] = "BALANCE-Laguna-S-2.1-A8B-Q2-dual-gpu-safe-32k"
    gpu_balanced["agentProfileId"] = "agent-maximo"
    gpu_balanced["runtimePresetId"] = gpu_runtime_id
    gpu_balanced["best"] = False
    gpu_balanced["lastUsed"] = 0
    gpu_args = list(gpu_balanced.get("extraArgs", []))
    if "--tensor-split" in gpu_args:
        gpu_args[gpu_args.index("--tensor-split") + 1] = "1,1"
    if "--predict" in gpu_args:
        gpu_args[gpu_args.index("--predict") + 1] = "512"
    gpu_balanced["extraArgs"] = gpu_args
    if not any(item.get("name") == gpu_balanced["name"] for item in data):
        data.append(gpu_balanced)

    backup = LAUNCHES.with_suffix(".json.bak.laguna-he0-repair")
    backup.write_text(LAUNCHES.read_text(encoding="utf-8"), encoding="utf-8")
    LAUNCHES.write_text(json.dumps(data, ensure_ascii=False, indent=4) + "\n", encoding="utf-8")
    RUNTIMES.write_text(json.dumps(runtimes, ensure_ascii=False, indent=4) + "\n", encoding="utf-8")
    print(clone["id"])
    print(fit_on["id"])
    print(cpu_only["id"])
    print(cpu_fast["id"])
    print(gpu_balanced["id"])


if __name__ == "__main__":
    main()
