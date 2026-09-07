#!/usr/bin/env python3
"""Mide VRAM real de cada perfil de sistema lanzando llama-server contra los
modelos locales (D:/Models/llamacpp) y leyendo nvidia-smi. No descarga nada.
Throwaway QA. Corre con la app cerrada."""
import json, subprocess, time, os, urllib.request, sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = r"D:/llama-mtp-b9274/build-ninja/bin/llama-server.exe"
PORT = 8098
ML = "D:/Models/llamacpp"

# id de perfil -> (modelo local, mmproj local o "")
LOCAL = {
    "sys-vram-24":     (f"{ML}/Qwen3.6-27B-GGUF/Qwen3.6-27B-Q4_K_M.gguf", ""),
    "sys-vram-16":     (f"{ML}/Qwen3.6-35B-A3B-MTP-IQ4_XS-GGUF/Qwen3.6-35B-A3B-MTP-IQ4_XS.gguf", ""),
    "sys-vram-12-moe": (f"{ML}/Qwen3.6-35B-A3B-MTP-IQ4_XS-GGUF/Qwen3.6-35B-A3B-MTP-IQ4_XS.gguf", ""),
    "sys-vram-12":     (f"{ML}/Qwen3.5-9B/Qwen3.5-9B-Q4_K_M.gguf", ""),
    "sys-vram-8":      (f"{ML}/Qwen3.5-9B/Qwen3.5-9B-Q4_K_M.gguf", ""),
    "sys-vram-4":      (f"{ML}/Qwen3.5-4B/Qwen3.5-4B-Q4_K_M.gguf", ""),
    "sys-vram-4-q5":   (f"{ML}/Qwen3.5-4B/Qwen3.5-4B-Q5_K_M.gguf", ""),
    "sys-vram-2":      (f"{ML}/Qwen3.5-2B/Qwen3.5-2B-Q4_K_M.gguf", ""),
    "sys-vram-0":      (f"{ML}/Qwen3.6-35B-A3B-GGUF/Qwen3.6-35B-A3B-UD-Q4_K_M.gguf", ""),
    "sys-maxq":        (f"{ML}/Qwen3.6-27B-XS-GGUF/Qwen3.6-27B-IQ4_XS.gguf",
                        f"{ML}/mmproj-Qwen3.6-27B-BF16.gguf"),
    "sys-fastgemma":   (f"{ML}/Anbeeld-gemma-4-31B-it-DFlash-GGUF/gemma4-31b-it-dflash-Q5_K_M.gguf", ""),
}

def vram_used():
    out = subprocess.check_output(
        ["nvidia-smi","--query-gpu=memory.used","--format=csv,noheader,nounits"],
        text=True)
    return int(out.strip().splitlines()[0])

def runtime_args(rt):
    a = []
    ngl = rt.get("gpuLayers", -1)
    a += ["--n-gpu-layers", str(999 if ngl < 0 else ngl)]
    a += ["--batch-size", str(rt.get("batch",512)), "--ubatch-size", str(rt.get("ubatch",512))]
    if rt.get("threads",0) > 0: a += ["--threads", str(rt["threads"])]
    if rt.get("flashAttn"): a += ["--flash-attn","on"]
    a += ["--cache-type-k", rt.get("kv","q8_0")]
    return a

def main():
    bundle = json.load(open(os.path.join(REPO,"assets","system_profiles.json"), encoding="utf-8"))
    base = vram_used()
    print(f"baseline VRAM: {base} MB\n")
    print(f"{'perfil':<16}{'budget':<8}{'VRAM MB':<10}{'estado'}")
    for o in bundle:
        pid = o["id"]; tier = o["tier"]
        mp = LOCAL.get(pid)
        if not mp or not os.path.exists(mp[0]):
            print(f"{tier:<16}{'?':<8}{'-':<10}FALTA modelo local"); continue
        args = [BIN,"--host","127.0.0.1","--port",str(PORT),"--model",mp[0]]
        if mp[1]: args += ["--mmproj", mp[1]]
        args += runtime_args(o["runtime"])
        args += o.get("extraArgs",[])
        if o.get("mtp",{}).get("enabled"): args += o["mtp"]["args"]
        env = dict(os.environ)
        for k,v in (o.get("env") or {}).items(): env[k] = v
        log = open(f"C:/Users/cristian/AppData/Local/Temp/meter_{pid}.log","w",encoding="utf-8")
        proc = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT, env=env)
        ok = None; t0 = time.time()
        while time.time()-t0 < 200:
            try:
                urllib.request.urlopen(f"http://127.0.0.1:{PORT}/health", timeout=2)
                ok = True; break
            except Exception: pass
            if proc.poll() is not None: ok = False; break
            time.sleep(2)
        used = "-"
        if ok:
            time.sleep(3); used = vram_used() - base
        minv = o.get("minVramGb",0)
        verdict = "OK" if (ok and (used=="-" or used <= minv*1024+300 or minv==0)) else ("OOM/err" if ok is False else "carga…")
        if ok and used != "-": verdict = "OK" if used <= minv*1024 + 512 else f"SOBRE budget ({minv}GB)"
        print(f"{tier:<16}{str(minv)+'GB':<8}{str(used):<10}{verdict}")
        proc.terminate()
        try: proc.wait(timeout=15)
        except Exception: proc.kill()
        time.sleep(3)
    print("\nlisto.")

if __name__ == "__main__":
    main()
