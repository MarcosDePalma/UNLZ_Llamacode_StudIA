#!/usr/bin/env python3
"""Correcciones de perfiles tras medir VRAM real en el medidor del app.

Mediciones (RTX 3090):
  4GB Q4   5.9GB > 4   -> bajar ctx + offload parcial + batch chico
  4GB Q5   5.8GB > 4   -> idem
  2GB      4.3GB > 2   -> idem (mas agresivo)
  0GB CPU  crashea     -> el gguf UD-Q4_K_M NO tiene MTP; sacar --spec-type draft-mtp

Los offload/batch son PUNTOS DE PARTIDA: re-medir en el medidor y ajustar ngl.
CORRER CON APP CERRADO. Idempotente-ish (vuelve a setear los mismos valores).
Uso:  python tools/patch_tier_fixes.py            (dry)
      python tools/patch_tier_fixes.py --apply
"""
import json, sys, datetime

P = lambda f: "profiles/" + f

# alias -> nuevos valores. ctx/ngl/batch van a runtime; ctx tambien a extraArgs (gana).
FIXES = {
    "vram-4-qwen4b":    dict(ctx=8192, ngl=18, batch=256),
    "vram-4-qwen4b-q5": dict(ctx=8192, ngl=16, batch=256),
    "vram-2-qwen2b":    dict(ctx=3072, ngl=8,  batch=64),
    # CPU puro: sin MTP + ocultar GPU (CUDA_VISIBLE_DEVICES=-1) -> 0 VRAM real
    "vram-0-cpu-35a3b": dict(drop_mtp=True, env={"CUDA_VISIBLE_DEVICES": "-1"}),
}

def set_extra_ctx(ea, ctx):
    if "--ctx-size" in ea:
        i = ea.index("--ctx-size"); ea[i+1] = str(ctx)
    else:
        ea += ["--ctx-size", str(ctx)]

def drop_pair(ea, flag):
    while flag in ea:
        i = ea.index(flag); del ea[i:i+2]

def main():
    dry = "--apply" not in sys.argv
    L = json.load(open(P("launches.json"), encoding="utf-8"))
    Mraw = open(P("models.json"), encoding="utf-8").read(); M = json.loads(Mraw)
    Rraw = open(P("runtimes.json"), encoding="utf-8").read(); R = json.loads(Rraw)
    Lraw = open(P("launches.json"), encoding="utf-8").read()
    rt = {r["id"]: r for r in R}; mp = {m["id"]: m for m in M}

    changed = []
    for x in L:
        f = FIXES.get(x.get("alias"))
        if not f:
            continue
        ea = x["extraArgs"]
        if f.get("env"):
            x.setdefault("envOverrides", {}).update(f["env"])
        if f.get("drop_mtp"):
            drop_pair(ea, "--spec-type"); drop_pair(ea, "--spec-draft-n-max")
            m = mp.get(x["modelProfileId"])
            if m: m["specType"] = ""
            changed.append((x["alias"], "sin MTP + env %s" % (f.get("env") or "")))
            continue
        r = rt[x["runtimePresetId"]]
        r["ctx"] = f["ctx"]; r["gpuLayers"] = f["ngl"]
        r["batch"] = f["batch"]; r["ubatch"] = f["batch"]
        set_extra_ctx(ea, f["ctx"])
        changed.append((x["alias"], "ctx=%d ngl=%d batch=%d" % (f["ctx"], f["ngl"], f["batch"])))

    print(("DRY-RUN " if dry else "") + "cambios: %d" % len(changed))
    for a, d in changed:
        print("  %-20s %s" % (a, d))
    if dry:
        print("\nRe-run con --apply."); return
    ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    for f, raw, data in (("models.json", Mraw, M), ("runtimes.json", Rraw, R), ("launches.json", Lraw, L)):
        open(P(f) + ".bak.tierfix." + ts, "w", encoding="utf-8").write(raw)
        json.dump(data, open(P(f), "w", encoding="utf-8"), ensure_ascii=False, indent=2)
    print("escrito. backups *.bak.tierfix." + ts)

if __name__ == "__main__":
    main()
