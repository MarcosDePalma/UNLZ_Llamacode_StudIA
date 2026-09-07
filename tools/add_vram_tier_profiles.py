#!/usr/bin/env python3
"""Crea perfiles de launch por tier de VRAM (24/16/12/8/4/2/0 GB).

Para cada tier arma 3 entradas enlazadas:
  - models.json   : un model-profile que apunta al gguf del catalogo
  - runtimes.json : un runtime preset (ctx / gpuLayers / cacheType / flash)
  - launches.json : el launch que ata modelo+runtime+backend/harness base

El modelId del catalogo es DETERMINISTA: uuid5(NS, absolute_path), igual que
GGUFScanner.cpp. Asi no hace falta releer la DB para enlazar.

CORRER CON EL APP CERRADO (save-on-exit pisa profiles/*.json). Idempotente:
salta tiers cuyo launch ya existe (por nombre). Escribe .bak antes de guardar.

Uso:   python tools/add_vram_tier_profiles.py            (dry-run)
       python tools/add_vram_tier_profiles.py --apply
"""
import json, os, sys, uuid, datetime

NS = uuid.UUID("a1b2c3d4-e5f6-4a5b-8c7d-0e1f2a3b4c5d")  # == GGUFScanner.cpp kCatalogNs
ROOT = "D:/Models/llamacpp"                               # model_roots.json path (slashes)
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
P = lambda f: os.path.join(REPO, "profiles", f)

BASE_LAUNCH_ID = "5c3d9bda-8810-4331-a770-1c981461fe17"   # perfil 1 (Qwen-27b, probado)

def cid(rel):  # catalog id por ruta relativa al root
    return str(uuid.uuid5(NS, "%s/%s" % (ROOT, rel)))

# rel: ruta del gguf bajo el root | ctx | gpuLayers (-1=all GPU, 0=CPU) | cacheType
# | cpu_moe: None | int (--n-cpu-moe N) | "all" (--cpu-moe) | mtp: usa spec draft-mtp
TIERS = [
    dict(tier="24GB", alias="vram-24-qwen27b",
         rel="Qwen3.6-27B-MTP-Q4_K_M-GGUF/Qwen3.6-27B-MTP-Q4_K_M.gguf",
         ctx=262000, ngl=-1, kv="q4_0", cpu_moe=None, mtp=True,
         note="Qwen3.6-27B denso Q4 (sweet spot, todo en GPU)"),
    dict(tier="16GB", alias="vram-16-qwen35a3b",
         rel="Qwen3.6-35B-A3B-MTP-IQ4_XS-GGUF/Qwen3.6-35B-A3B-MTP-IQ4_XS.gguf",
         ctx=32768, ngl=-1, kv="q4_0", cpu_moe=18, mtp=True,
         note="Qwen3.6-35B-A3B MoE Q4, offload parcial de expertos a RAM"),
    dict(tier="12GB-MoE", alias="vram-12-qwen35a3b",
         rel="Qwen3.6-35B-A3B-MTP-IQ4_XS-GGUF/Qwen3.6-35B-A3B-MTP-IQ4_XS.gguf",
         ctx=16384, ngl=-1, kv="q4_0", cpu_moe=30, mtp=True,
         note="Qwen3.6-35B-A3B MoE Q4 en 12GB: offload fuerte de expertos a RAM"),
    dict(tier="12GB", alias="vram-12-qwen9b",
         rel="Qwen3.5-9B/Qwen3.5-9B-Q4_K_M.gguf",
         ctx=65536, ngl=-1, kv="q8_0", cpu_moe=None, mtp=False,
         note="Qwen3.5-9B denso Q4, todo en GPU, KV q8 amplio"),
    dict(tier="8GB", alias="vram-8-qwen9b",
         rel="Qwen3.5-9B/Qwen3.5-9B-Q4_K_M.gguf",
         ctx=16384, ngl=-1, kv="q4_0", cpu_moe=None, mtp=False,
         note="Qwen3.5-9B Q4, ctx recortado + KV q4 para entrar en 8GB"),
    dict(tier="4GB", alias="vram-4-qwen4b",
         rel="Qwen3.5-4B/Qwen3.5-4B-Q4_K_M.gguf",
         ctx=16384, ngl=-1, kv="q4_0", cpu_moe=None, mtp=True,
         note="Qwen3.5-4B Q4 (descargado), todo en GPU"),
    dict(tier="4GB-Q5", alias="vram-4-qwen4b-q5",
         rel="Qwen3.5-4B/Qwen3.5-4B-Q5_K_M.gguf",
         ctx=12288, ngl=-1, kv="q4_0", cpu_moe=None, mtp=True,
         note="Qwen3.5-4B Q5_K_M (mejor calidad, ctx algo menor)"),
    dict(tier="2GB", alias="vram-2-qwen2b",
         rel="Qwen3.5-2B/Qwen3.5-2B-Q4_K_M.gguf",
         ctx=8192, ngl=-1, kv="q4_0", cpu_moe=None, mtp=True,
         note="Qwen3.5-2B Q4 (descargado), ctx 8k"),
    dict(tier="0GB", alias="vram-0-cpu-35a3b",
         rel="Qwen3.6-35B-A3B-GGUF/Qwen3.6-35B-A3B-UD-Q4_K_M.gguf",
         ctx=32768, ngl=0, kv="q8_0", cpu_moe=None, mtp=True,
         note="CPU puro: Qwen3.6-35B-A3B Q4 (3B activos, RAM)"),
]

def base_extra_args(t):
    a = ["--alias", t["alias"],
         "--cache-type-v", t["kv"],
         "--temp", "0.60", "--top-p", "0.95", "--top-k", "20", "--min-p", "0.0",
         "--no-context-shift", "--metrics", "--no-warmup",
         "--jinja", "--reasoning", "off",
         "--ctx-size", str(t["ctx"]),
         "--parallel", "1"]
    if t["mtp"]:
        a += ["--spec-type", "draft-mtp", "--spec-draft-n-max", "3"]
    if t["cpu_moe"] == "all":
        a += ["--cpu-moe"]
    elif isinstance(t["cpu_moe"], int):
        a += ["--n-cpu-moe", str(t["cpu_moe"])]
    return a

def load(f):
    raw = open(P(f), encoding="utf-8").read()
    return raw, json.loads(raw)

def items_of(d):
    return d if isinstance(d, list) else d.get("items", d.get("profiles"))

def main():
    dry = "--apply" not in sys.argv
    mraw, models = load("models.json")
    rraw, runtimes = load("runtimes.json")
    lraw, launches = load("launches.json")
    M, R, L = items_of(models), items_of(runtimes), items_of(launches)
    base = next(x for x in L if x["id"] == BASE_LAUNCH_ID)
    existing = {x.get("name") for x in L}

    plan = []
    for t in TIERS:
        name = "VRAM %s · %s" % (t["tier"], t["alias"])
        if name in existing:
            print("skip (existe):", name); continue
        model_id = cid(t["rel"])
        mp_id, rt_id, lc_id = (str(uuid.uuid4()) for _ in range(3))
        M.append({"draftModelId": "", "id": mp_id, "mmprojId": "",
                  "modelId": model_id, "name": "VRAM %s Model" % t["tier"],
                  "specDraftNMax": 0, "specDraftNgl": "", "specDraftTypeK": "",
                  "specDraftTypeV": "", "specType": "draft-mtp" if t["mtp"] else ""})
        R.append({"batch": 512, "cacheType": t["kv"], "contBatching": True,
                  "ctx": t["ctx"], "flashAttention": True, "gpuLayers": t["ngl"],
                  "id": rt_id, "mlock": False, "mmap": True,
                  "name": "VRAM %s rt" % t["tier"], "parallelSlots": 1,
                  "threads": -1, "ubatch": 512})
        lc = json.loads(json.dumps(base))
        lc.update(id=lc_id, name=name, alias=t["alias"], favorite=True,
                  modelProfileId=mp_id, runtimePresetId=rt_id,
                  extraArgs=base_extra_args(t))
        L.append(lc)
        plan.append((name, t["note"], model_id, t["rel"]))

    print(("DRY-RUN " if dry else "") + "tiers nuevos: %d" % len(plan))
    for n, note, mid, rel in plan:
        ok = os.path.exists(os.path.join(ROOT.replace("/", os.sep), *rel.split("/")))
        print("  %-26s %s  [gguf %s]" % (n, note, "OK" if ok else "FALTA"))
    if dry:
        print("\nRe-run con --apply para escribir."); return

    ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    for f, raw, data in (("models.json", mraw, models), ("runtimes.json", rraw, runtimes),
                         ("launches.json", lraw, launches)):
        open(P(f) + ".bak.vramtiers." + ts, "w", encoding="utf-8").write(raw)
        with open(P(f), "w", encoding="utf-8") as fh:
            json.dump(data, fh, ensure_ascii=False, indent=2)
    print("escrito. backups *.bak.vramtiers.%s" % ts)

if __name__ == "__main__":
    main()
