#!/usr/bin/env python3
"""Instala el perfil MAX-Q de ThinkingCap-Qwen3.6-27B.

El script es determinista e idempotente. Corre en dry-run salvo que se pase
--apply y debe ejecutarse con LlamaCode cerrado, porque la app persiste profiles/
al salir.

Uso:
    python tools/add_thinkingcap_profiles.py
    python tools/add_thinkingcap_profiles.py --apply
"""

import copy
import datetime
import json
import os
import sys
import uuid


CATALOG_NS = uuid.UUID("a1b2c3d4-e5f6-4a5b-8c7d-0e1f2a3b4c5d")
PROFILE_NS = uuid.UUID("ad4094c4-3ebf-47b3-bb25-d343df25e578")
ROOT = "D:/Models/llamacpp"
MODEL_REL = "ThinkingCap-Qwen3.6-27B-GGUF/ThinkingCap-Qwen3.6-27B-Q4_K_M.gguf"
MMPROJ_REL = "ThinkingCap-Qwen3.6-27B-GGUF/mmproj-ThinkingCap-Qwen3.6-27B-f16.gguf"
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROFILE_DIR = os.path.join(REPO, "profiles")

# Reutiliza el backend/harness del benchmark KAT ya validado (llama.cpp CUDA).
BASE_LAUNCH_ID = "bd6f72c4-f849-40ec-b3d6-b930f93921dd"
MODEL_NAME = "MAX-Q · ThinkingCap Qwen3.6 27B Q4_K_M + vision"
RUNTIME_NAME = "MAX-Q · ThinkingCap 27B · 131k Q4KV"
LAUNCH_NAME = "MAX-Q · ThinkingCap Qwen3.6-27B 131k MTP4"


def stable_id(kind):
    return str(uuid.uuid5(PROFILE_NS, "thinkingcap-qwen36-27b:" + kind))


def catalog_id(relative_path):
    return str(uuid.uuid5(CATALOG_NS, ROOT + "/" + relative_path))


def load(name):
    path = os.path.join(PROFILE_DIR, name)
    with open(path, encoding="utf-8") as handle:
        raw = handle.read()
    return path, raw, json.loads(raw)


def ensure_unique(items, entry):
    matches = [item for item in items if item.get("id") == entry["id"]]
    if matches:
        matches[0].clear()
        matches[0].update(entry)
        return "updated"
    items.append(entry)
    return "added"


def main():
    apply = "--apply" in sys.argv
    model_path, model_raw, models = load("models.json")
    runtime_path, runtime_raw, runtimes = load("runtimes.json")
    launch_path, launch_raw, launches = load("launches.json")

    base = next(item for item in launches if item["id"] == BASE_LAUNCH_ID)
    model = {
        "draftModelId": "",
        "id": stable_id("model"),
        "mmprojId": catalog_id(MMPROJ_REL),
        "modelId": catalog_id(MODEL_REL),
        "name": MODEL_NAME,
        "specDraftNMax": 4,
        "specDraftNgl": "",
        "specDraftTypeK": "",
        "specDraftTypeV": "",
        "specType": "draft-mtp",
    }
    runtime = {
        "batch": 512,
        "cacheType": "q4_0",
        "contBatching": True,
        "ctx": 131000,
        "flashAttention": True,
        "gpuLayers": -1,
        "id": stable_id("runtime"),
        "mlock": True,
        "mmap": False,
        "name": RUNTIME_NAME,
        "parallelSlots": 1,
        "threads": 8,
        "ubatch": 64,
    }
    launch = copy.deepcopy(base)
    launch.update({
        "alias": "MAX-Q",
        "favorite": True,
        "id": stable_id("launch"),
        "modelProfileId": model["id"],
        "name": LAUNCH_NAME,
        "runtimePresetId": runtime["id"],
        "extraArgs": [
            "--alias", "thinkingcap-qwen36-27b-q4km-mtp4",
            "--cache-type-k", "q4_0",
            "--cache-type-v", "q4_0",
            "--temp", "0.60",
            "--top-p", "0.95",
            "--top-k", "20",
            "--min-p", "0.0",
            "--repeat-penalty", "1.0",
            "--presence-penalty", "0.0",
            "--no-context-shift",
            "--metrics",
            "--no-warmup",
            "--cache-ram", "32768",
            "--cache-reuse", "512",
            "--jinja",
            "--threads-batch", "8",
            "--predict", "4096",
            "--parallel", "1",
            "--flash-attn", "on",
            "--ctx-size", "131000",
            "--reasoning", "off",
        ],
    })

    actions = [
        ("models.json", ensure_unique(models, model)),
        ("runtimes.json", ensure_unique(runtimes, runtime)),
        ("launches.json", ensure_unique(launches, launch)),
    ]
    print(("APPLY" if apply else "DRY-RUN") + " ThinkingCap profiles")
    for name, action in actions:
        print("  %s: %s" % (name, action))
    for relative_path in (MODEL_REL, MMPROJ_REL):
        absolute = os.path.join(ROOT.replace("/", os.sep), *relative_path.split("/"))
        print("  %s: %s" % ("OK" if os.path.exists(absolute) else "FALTA", absolute))
    if not apply:
        print("Re-run con --apply para escribir.")
        return

    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    for path, raw, data in (
        (model_path, model_raw, models),
        (runtime_path, runtime_raw, runtimes),
        (launch_path, launch_raw, launches),
    ):
        with open(path + ".bak.thinkingcap." + stamp, "w", encoding="utf-8") as backup:
            backup.write(raw)
        with open(path, "w", encoding="utf-8") as handle:
            json.dump(data, handle, ensure_ascii=False, indent=2)
            handle.write("\n")


if __name__ == "__main__":
    main()
