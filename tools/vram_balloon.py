#!/usr/bin/env python3
"""Globo de VRAM: ocupa VRAM en la GPU para dejar LIBRE solo el presupuesto de un
tier, y asi testear OOM de los perfiles (8/4/2GB...) en una placa grande (3090).

NO simula velocidad — solo fuerza el techo de memoria: si el perfil se pasa del
presupuesto, OOMea igual que en la placa chica real.

Uso:
    python tools/vram_balloon.py 8        # deja ~8 GB libres (simula placa 8GB)
    python tools/vram_balloon.py 8 --gpu 0
    python tools/vram_balloon.py --free   # solo reporta libre/total y sale

Queda corriendo reteniendo la VRAM. Cortar con Ctrl-C libera todo. Mientras corre:
abri LlamaCode → Benchmark → corre el perfil del tier. Sin deps (ctypes→cudart).
"""
import ctypes, glob, os, sys, time

GiB = 1024**3
HEADROOM = 0.4 * GiB  # margen para que el driver/escritorio no quede sin nada

def find_cudart():
    pats = [
        r"%APPDATA%\LlamaCode\LlamaCode\tools\**\cudart64_*.dll",
        r"%ProgramFiles%\NVIDIA GPU Computing Toolkit\CUDA\**\cudart64_*.dll",
    ]
    for p in pats:
        for f in glob.glob(os.path.expandvars(p), recursive=True):
            return f
    return "cudart64_12.dll"  # ultimo intento: PATH

def load():
    dll = find_cudart()
    try:
        cuda = ctypes.WinDLL(dll)
    except OSError as e:
        sys.exit("no pude cargar cudart (%s): %s" % (dll, e))
    cuda.cudaMemGetInfo.argtypes = [ctypes.POINTER(ctypes.c_size_t)] * 2
    cuda.cudaMalloc.argtypes = [ctypes.POINTER(ctypes.c_void_p), ctypes.c_size_t]
    cuda.cudaSetDevice.argtypes = [ctypes.c_int]
    return cuda, dll

def meminfo(cuda):
    free, total = ctypes.c_size_t(), ctypes.c_size_t()
    if cuda.cudaMemGetInfo(ctypes.byref(free), ctypes.byref(total)) != 0:
        sys.exit("cudaMemGetInfo fallo (driver/GPU?)")
    return free.value, total.value

def main():
    args = [a for a in sys.argv[1:]]
    gpu = 0
    if "--gpu" in args:
        i = args.index("--gpu"); gpu = int(args[i + 1]); del args[i:i + 2]
    cuda, dll = load()
    cuda.cudaSetDevice(gpu)
    free, total = meminfo(cuda)
    print("cudart: %s" % dll)
    print("GPU %d: total %.2f GB | libre %.2f GB" % (gpu, total / GiB, free / GiB))
    if "--free" in args or not args:
        return
    target_free = float(args[0]) * GiB
    reserve = free - target_free - HEADROOM
    if reserve <= 0:
        sys.exit("ya hay <= %.1f GB libres; nada que ocupar" % (target_free / GiB))
    print("reservando %.2f GB para dejar ~%.1f GB libres..." % (reserve / GiB, target_free / GiB))
    held = []
    chunk = 512 * 1024 * 1024  # 512MB por alloc (evita fallar por fragmentacion)
    got = 0
    while got < reserve:
        sz = min(chunk, int(reserve - got))
        ptr = ctypes.c_void_p()
        if cuda.cudaMalloc(ctypes.byref(ptr), sz) != 0:
            chunk //= 2
            if chunk < 16 * 1024 * 1024:
                break
            continue
        held.append(ptr); got += sz
    free2, _ = meminfo(cuda)
    print("ocupado %.2f GB | LIBRE AHORA %.2f GB  (%d bloques)" % (got / GiB, free2 / GiB, len(held)))
    print("listo. corre el benchmark del tier. Ctrl-C para liberar.")
    try:
        while True:
            time.sleep(3600)
    except KeyboardInterrupt:
        print("\nliberando.")

if __name__ == "__main__":
    main()
