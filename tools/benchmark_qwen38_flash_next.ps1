# Benchmark de Qwen3.8-Flash-Next (arquitectura Qwen4: MoE + Ngram/PLE).
#
# Lo que mide, y por que: en esta arquitectura los tensores Ngram/PLE son una
# lookup table de acceso aleatorio, no compute. La pregunta de tuning no es
# "cuantas capas a GPU" sino "donde viven los Ngram": VRAM, RAM, o SSD via mmap.
# El sweep compara esas tres colocaciones con el mismo modelo y prompts.
#
# ASCII puro a proposito (ver CLAUDE.md: Windows PowerShell 5.1 lee .ps1 sin BOM
# como ANSI y un caracter UTF-8 dentro de un string puede cortarlo).
param(
    [string]$Server = "",
    [string]$Model  = "",
    [int]$Port      = 18097,
    [int]$Passes    = 2,
    [int]$CtxSize   = 32768,
    # Capas cuyos expertos MoE quedan en CPU (de 48). En 2x24GB, por debajo de
    # ~32 el modelo NO entra: con 26 el loader pide 36 GB en una placa -> OOM.
    # 32 y 34 hacen OOM en 2x24 GB (piden ~28 GB en una sola placa).
    [int[]]$NCpuMoeSweep = @(36, 40),
    # Sin bajar el esfuerzo de razonamiento el modelo NUNCA emite content: se
    # come el presupuesto entero pensando (xhigh es el default) y devuelve
    # finish_reason=length con content vacio.
    # OJO: el chat template SOLO acepta xhigh/medium/low. La doc de unsloth
    # lista "none" como opcion, pero el template hace raise_exception y el
    # server devuelve 500 ("Unexpected reasoning effort none").
    # Aun con "low" el modelo se come 2048 tokens en tareas triviales, por eso
    # el presupuesto por defecto es alto.
    [ValidateSet("low","medium","xhigh")]
    [string]$ReasoningEffort = "low",
    [int]$MaxTokens = 4096,
    # Incluye un brazo con -ot de los Ngram. Medido: en CUDA CORROMPE la salida
    # y NO ahorra VRAM. Off por defecto; encenderlo sirve para re-verificar si
    # una build futura del PR lo arregla.
    [switch]$IncludeNgramOffload,
    # Regex de tensores Ngram/PLE. Vacio = autodetectar desde el GGUF.
    [string]$NgramRegex = "",
    [ValidateSet("thinking","instruct","both")]
    [string]$Mode = "both",
    [string]$Output = ""
)

$ErrorActionPreference = "Stop"

if (-not $Server) {
    # Ninja es single-config: los binarios quedan en buildin, no en buildin\Release.
    $Server = Join-Path $env:APPDATA "LlamaCode\LlamaCode\tools\llama.cpp-qwen38-next\build\bin\llama-server.exe"
}
if (-not $Model) {
    $Model = "D:\Models\llamacpp\Qwen3.8-Flash-Next-UD-Q4_K_XL\UD-Q4_K_XL\Qwen3.8-Flash-Next-UD-Q4_K_XL-00001-of-00004.gguf"
}
if (-not (Test-Path $Server)) { throw "No existe llama-server: $Server (compilar el PR 27742 primero)" }
if (-not (Test-Path $Model))  { throw "No existe el modelo: $Model" }

# Sampling oficial. Son DOS perfiles distintos, no uno con variantes.
$sampling = @{
    thinking = @{ temp="1.0"; topP="0.95"; topK="20"; minP="0.0"; presence="0.0" }
    instruct = @{ temp="0.7"; topP="0.80"; topK="20"; minP="0.0"; presence="1.5" }
}

$tasks = @(
    @{ id="modular";      expected='(?im)^FINAL:\s*1\s*$';       prompt="Compute 2^100 mod 125. Explain briefly and end with exactly FINAL: <integer>." },
    @{ id="mississippi";  expected='(?im)^FINAL:\s*34650\s*$';   prompt="How many distinct permutations does MISSISSIPPI have? Explain briefly and end with exactly FINAL: <integer>." },
    @{ id="telescoping";  expected='(?im)^FINAL:\s*100/101\s*$'; prompt="Compute sum from k=1 to 100 of 1/(k(k+1)). Explain briefly and end with exactly FINAL: <reduced fraction>." },
    @{ id="python-output";expected='(?im)^FINAL:\s*\[1,4,9\]\s*$';prompt="What does Python print for [x*x for x in range(1,4)]? Explain briefly and end with exactly FINAL: followed by compact JSON only." }
)

# Nombres reales de tensor de la arquitectura qwen4exp, tomados de llama-arch.cpp
# en el PR 27742. Solo las LOOKUP TABLES: blk.N.ple_key / ple_value y
# per_layer_token_embd. Deliberadamente NO incluye ple_norm_*, ple_conv1d ni
# per_layer_proj_norm: esos son compute, mandarlos a CPU es perdida neta.
$script:NgramTensorRegex = "(ple_key|ple_value|per_layer_token_embd)"

function Get-NgramRegex($modelPath) {
    # Hay que mirar TODOS los shards, no solo el que se pasa: en este modelo el
    # shard 1 es puro metadata (0 tensores) y los Ngram estan enteros en el 2.
    # Mirando uno solo, la verificacion decia "no hay tensores" y no verificaba
    # nada.
    $dump = Join-Path (Split-Path $Server) "llama-gguf.exe"
    if (-not (Test-Path $dump)) { return "" }
    $hits = 0
    foreach ($sh in (Get-ChildItem -Path (Split-Path $modelPath) -Filter "*.gguf")) {
        $names = & $dump $sh.FullName r n 2>$null
        $hits += ($names | Select-String -Pattern $script:NgramTensorRegex).Count
    }
    if ($hits -lt 1) {
        Write-Warning "Ningun shard tiene tensores que matcheen $script:NgramTensorRegex."
        return ""
    }
    # Write-Host, no Write-Output: esta funcion DEVUELVE el regex, y un
    # Write-Output se concatena al retorno. Con eso $NgramRegex quedaba como
    # "Tensores... 12 (ple_key|...)" y despues no matcheaba nada.
    Write-Host ("Tensores Ngram/PLE encontrados: " + $hits)
    return $script:NgramTensorRegex
}

function Wait-Healthy([int]$targetPort, [int]$maxSeconds = 600) {
    # Este modelo tarda en cargar: 111 GB con mmap desde SSD no levanta en 60s.
    $deadline = (Get-Date).AddSeconds($maxSeconds)
    while ((Get-Date) -lt $deadline) {
        try {
            $h = Invoke-RestMethod "http://127.0.0.1:$targetPort/health" -TimeoutSec 3
            if ($h.status -eq "ok") { return }
        } catch {}
        Start-Sleep -Seconds 3
    }
    throw "llama-server no quedo healthy en el puerto $targetPort tras $maxSeconds s"
}

function Get-VramMB {
    try {
        $q = & nvidia-smi --query-gpu=memory.used --format=csv,noheader,nounits 2>$null
        if (-not $q) { return 0 }
        return (($q | ForEach-Object { [int]$_.Trim() }) | Measure-Object -Sum).Sum
    } catch { return 0 }
}

function Test-NgramLayout($modelPath) {
    # El punto que hace o rompe el offload: si los tensores Ngram/PLE estan
    # interleaved con el resto adentro de un shard, mmap los trae igual y no se
    # ahorra nada. Solo sirve si viven en shards propios.
    $dir = Split-Path $modelPath
    $shards = Get-ChildItem -Path $dir -Filter "*.gguf" | Sort-Object Name
    $dump = Join-Path (Split-Path $Server) "llama-gguf.exe"
    if (-not (Test-Path $dump)) {
        Write-Warning "Sin llama-gguf.exe no se puede verificar el layout del quant."
        return $null
    }
    $report = @()
    foreach ($sh in $shards) {
        $names = & $dump $sh.FullName r n 2>$null
        $ng = ($names | Select-String -Pattern $NgramRegex).Count
        $tot = ($names | Select-String -Pattern 'tensor\[').Count
        $report += [pscustomobject]@{
            shard = $sh.Name; ngram_tensors = $ng; total_tensors = $tot
            pure_ngram = ($ng -gt 0 -and $tot -gt 0 -and $ng -eq $tot)
        }
    }
    $mixed = $report | Where-Object { $_.ngram_tensors -gt 0 -and -not $_.pure_ngram }
    if ($mixed) {
        Write-Warning ("Layout interleaved: " + $mixed.Count + " shard(s) mezclan Ngram con backbone.")
        Write-Warning "El offload a SSD no va a ahorrar memoria hasta repackear el GGUF."
    }
    return $report
}

function Invoke-Bench($label, $ncmoe, $withNgramOffload, $modeName) {
    # KV cache f16 a proposito: --cache-type-k/v q8_0 CRASHEA el PR 27742 en esta
    # arquitectura (GGML_ASSERT(inp->self_k_rot == nullptr) en qwen4exp.cpp:544).
    # Tampoco se pasa --tensor-split ni --fit: -ot desactiva --fit ("tensor_buft_
    # overrides already set by user, abort") y --tensor-split tambien.
    $s = $sampling[$modeName]
    $serverArgs = @(
        "-m", $Model, "--host", "127.0.0.1", "--port", "$Port",
        "--ctx-size", "$CtxSize", "--parallel", "1",
        "--n-gpu-layers", "999",
        "--flash-attn", "on", "--jinja", "--metrics", "--no-warmup",
        # Las comillas van escapadas con \" a proposito: Start-Process arma la
        # command line nativa y sin eso llega '{reasoning_effort:low}', que
        # llama-server rechaza con "syntax error while parsing object key".
        "--chat-template-kwargs", ('{\"reasoning_effort\":\"' + $ReasoningEffort + '\"}'),
        "--temp", $s.temp, "--top-p", $s.topP, "--top-k", $s.topK,
        "--min-p", $s.minP, "--presence-penalty", $s.presence
    )
    # Medido contra el binario real del PR 27742 (ver docs/qwen38-flash-next.md):
    #  - "--cpu-moe" (las 48 capas en CPU) ROMPE la salida: "121 122 123" ->
    #    "1211 1211 1212". Con --n-cpu-moe 36 la misma prueba da "130 131 132".
    #  - -ot de los Ngram a CPU corrompe la salida Y no ahorra VRAM (28936 vs
    #    28904 MiB sobre 26.85 GB de tensores). En CUDA no hace lo que promete.
    #  - --mmap/--no-mmap/--mlock estan deprecados -> -lm/--load-mode.
    $serverArgs += @("--n-cpu-moe", "$ncmoe", "--load-mode", "mmap")
    if ($withNgramOffload) {
        $serverArgs += @("--override-tensor", ($NgramRegex + "=CPU"))
    }

    $log = Join-Path ([IO.Path]::GetTempPath()) ("llamacode-q38fn-" + $label + ".log")
    $proc = Start-Process -FilePath $Server -ArgumentList $serverArgs -PassThru `
                          -RedirectStandardError $log -RedirectStandardOutput ($log + ".out") -WindowStyle Hidden
    $rows = @()
    try {
        Wait-Healthy $Port
        # Foto de memoria despues de cargar y antes de generar: aca se ve si los
        # Ngram quedaron residentes o si el SO los esta paginando desde el SSD.
        Start-Sleep -Seconds 5
        $proc.Refresh()
        $wsMB   = [math]::Round($proc.WorkingSet64 / 1MB, 0)
        $vramMB = Get-VramMB
        # Write-Host y no Write-Output: dentro de una funcion que devuelve filas,
        # Write-Output las mezcla en el pipeline de retorno y el resumen explota
        # con "No se encuentra la propiedad tok_s".
        Write-Host ("  memoria: working set " + $wsMB + " MB, VRAM " + $vramMB + " MB")
        foreach ($t in $tasks) {
            for ($p = 1; $p -le $Passes; $p++) {
                $body = @{
                    messages = @(@{ role="user"; content=$t.prompt })
                    max_tokens = $MaxTokens
                    stream = $false
                } | ConvertTo-Json -Depth 6
                $sw = [Diagnostics.Stopwatch]::StartNew()
                $r = Invoke-RestMethod "http://127.0.0.1:$Port/v1/chat/completions" -Method Post `
                        -ContentType "application/json" -Body $body -TimeoutSec 1800
                $sw.Stop()
                $text = $r.choices[0].message.content
                $genTok = $r.usage.completion_tokens
                $rows += [pscustomobject]@{
                    ncmoe      = $ncmoe
                    ngram_ot   = [bool]$withNgramOffload
                    mode       = $modeName
                    task       = $t.id
                    pass       = $p
                    seconds    = [math]::Round($sw.Elapsed.TotalSeconds, 2)
                    gen_tokens = $genTok
                    tok_s      = if ($sw.Elapsed.TotalSeconds -gt 0) { [math]::Round($genTok / $sw.Elapsed.TotalSeconds, 2) } else { 0 }
                    correct    = [bool]([regex]::IsMatch($text, $t.expected))
                    ws_mb      = $wsMB
                    vram_mb    = $vramMB
                }
            }
        }
    } finally {
        if ($proc -and -not $proc.HasExited) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
        Start-Sleep -Seconds 3
    }
    return $rows
}

if (-not $NgramRegex) {
    $NgramRegex = Get-NgramRegex $Model
    if (-not $NgramRegex) {
        Write-Warning "No se pudo verificar contra el GGUF; usando los nombres de llama-arch.cpp."
        $NgramRegex = $script:NgramTensorRegex
    }
}
Write-Output ("Ngram/PLE regex: " + $NgramRegex)
$layout = Test-NgramLayout $Model
if ($layout) { $layout | Format-Table -AutoSize }

$modes = if ($Mode -eq "both") { @("thinking","instruct") } else { @($Mode) }
$configs = @()
foreach ($n in $NCpuMoeSweep) { $configs += ,@($n, $false) }
if ($IncludeNgramOffload) {
    # El mayor del sweep = el que mas VRAM deja libre; usar el primero podia
    # caer justo en la config que hace OOM y perder el brazo entero.
    $configs += ,@(($NCpuMoeSweep | Measure-Object -Maximum).Maximum, $true)
}

$all = @()
foreach ($m in $modes) {
    foreach ($cfg in $configs) {
        $n  = $cfg[0]
        $ot = $cfg[1]
        Write-Output ("== " + $m + " / n-cpu-moe=" + $n + " / ngram-ot=" + $ot + " ==")
        try {
            $all += Invoke-Bench ($m + "-n" + $n + $(if ($ot) { "-ot" } else { "" })) $n $ot $m
        } catch {
            # Una config que no entra en VRAM no invalida las demas: se anota y
            # el sweep sigue. Perder 3 brazos buenos por 1 malo seria absurdo
            # cuando cada carga son minutos.
            Write-Warning ("config n-cpu-moe=" + $n + " ngram-ot=" + $ot + " FALLO: " + $_.Exception.Message)
        }
    }
}

$all | Format-Table -AutoSize
Write-Output ""
Write-Output "== Resumen =="
$all | Group-Object ncmoe, ngram_ot, mode | ForEach-Object {
    $g = $_.Group
    [pscustomobject]@{
        ncmoe     = $g[0].ncmoe
        ngram_ot  = $g[0].ngram_ot
        mode      = $g[0].mode
        avg_tok_s = [math]::Round(($g | Measure-Object tok_s -Average).Average, 2)
        accuracy  = [math]::Round((($g | Where-Object correct).Count / $g.Count) * 100, 1)
        ws_mb     = $g[0].ws_mb
        vram_mb   = $g[0].vram_mb
    }
} | Sort-Object mode, ncmoe | Format-Table -AutoSize

if ($Output) {
    $all | ConvertTo-Json -Depth 5 | Set-Content -Path $Output -Encoding UTF8
    Write-Output ("Resultados en " + $Output)
}
