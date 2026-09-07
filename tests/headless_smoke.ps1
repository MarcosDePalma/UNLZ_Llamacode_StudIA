param(
    [int]$Port = 8877,
    [string]$Exe = ".\build\Debug\LlamaCode.exe",
    [switch]$RunModelLoop,
    [switch]$TestRestartPersistence,
    [switch]$TestSchedulerDaemon,
    [string]$LaunchId = ""
)

$ErrorActionPreference = "Stop"
$qtRoot = "C:\Qt\6.8.3\msvc2022_64"
$env:QT_QPA_PLATFORM = "offscreen"
$env:QT_PLUGIN_PATH = Join-Path $qtRoot "plugins"
$env:PATH = (Join-Path $qtRoot "bin") + ";" + $env:PATH
$profileDir = Join-Path (Get-Location) ".headless-smoke-profiles"
$env:LLAMACODE_CONTROL_PORT = "$Port"
$env:LLAMACODE_PROFILES_DIR = $profileDir
New-Item -ItemType Directory -Force $profileDir | Out-Null
$proc = $null
$schedulerProc = $null

function Invoke-Api([string]$Path, [string]$Method = "Get", $Body = $null) {
    $params = @{ Uri = "http://127.0.0.1:$Port$Path"; Method = $Method }
    if ($null -ne $Body) {
        $params.ContentType = "application/json"
        $params.Body = ($Body | ConvertTo-Json -Depth 20 -Compress)
    }
    Invoke-RestMethod @params
}

function Invoke-Target([string]$Target, [string]$Method, [object[]]$Arguments) {
    $query = if ([string]::IsNullOrEmpty($Target)) { "" } else { "?target=$Target" }
    Invoke-Api "/invoke$query" "Post" @{ method = $Method; args = $Arguments }
}

function Get-Prop([string]$Target, [string]$Name) {
    $query = if ([string]::IsNullOrEmpty($Target)) { "" } else { "?target=$Target&name=$Name" }
    if ([string]::IsNullOrEmpty($Target)) { $query = "?name=$Name" }
    (Invoke-Api "/prop$query").value
}

try {
    # --handoff-ui evita colisiones con la instancia Debug que puede estar
    # abierta durante desarrollo; no habilita QML y conserva el modo daemon.
    $proc = Start-Process -FilePath $Exe -ArgumentList "--agent-daemon --handoff-ui" -WindowStyle Hidden -PassThru
    $ready = $false
    for ($i = 0; $i -lt 30; $i++) {
        try { if ((Invoke-Api "/health").ok) { $ready = $true; break } } catch {}
        Start-Sleep -Milliseconds 250
    }
    if (-not $ready) { throw "El daemon no respondió en el puerto $Port" }

    # Hardware/performance smoke: no abre QML ni requiere modelo. El probe es
    # asíncrono, por eso se invoca y luego se espera a que el resumen deje de
    # estar en estado "Detectando". En equipos sin NVIDIA se valida igualmente
    # el fallback CPU y la forma estable del contrato.
    Invoke-Target "" "runStartupScan" @() | Out-Null
    $hardware = $null
    for ($i = 0; $i -lt 40; $i++) {
        try {
            $hardware = Get-Prop "" "hardwareSummary"
            if ($hardware.summary -and $hardware.summary -notmatch "Detectando") { break }
        } catch {}
        Start-Sleep -Milliseconds 250
    }
    if (-not $hardware -or $hardware.PSObject.Properties.Name -notcontains "hardwareFingerprint") {
        throw "hardwareSummary no publicó hardwareFingerprint"
    }
    $recommendation = (Invoke-Target "" "performanceRecommendation" @("balanced")).result
    if ($recommendation.splitMode -notin @("layer", "tensor")) {
        throw "performanceRecommendation devolvió splitMode inválido"
    }
    $candidates = (Invoke-Target "" "performanceMatrixCandidates" @("decode", $false)).result
    if (@($candidates).Count -lt 6) { throw "performanceMatrixCandidates devolvió menos candidatos que los esperados" }
    $matrixSample = @{
        performanceCandidate = $candidates[0]
        avgTps = 10.0
        qualityScore = 1
        qualityTotal = 1
        failed = $false
        timedOut = $false
    }
    $annotated = (Invoke-Target "" "annotatePerformanceMatrix" @($matrixSample, $candidates[0])).result
    if ($annotated.measurementStatus -ne "measured" -or [string]::IsNullOrWhiteSpace($annotated.hardwareFingerprint)) {
        throw "annotatePerformanceMatrix no produjo una medición headless válida"
    }
    $ranked = (Invoke-Target "" "rankPerformanceMatrix" @(@($annotated), "decode")).result
    if (@($ranked).Count -ne 1 -or $ranked[0].rank -ne 1 -or $ranked[0].performanceScore -le 0) {
        throw "rankPerformanceMatrix no ordenó la muestra headless"
    }
    Write-Output ("Hardware smoke: GPUs={0}, split={1}, p2p={2}, nvlink={3}, fingerprint={4}" -f `
        $hardware.gpuCount, $recommendation.splitMode, $hardware.p2pAvailable, `
        $hardware.nvlinkAvailable, $hardware.hardwareFingerprint)

    $methods = Invoke-Api "/methods?target=taskStore"
    $saveMethod = @($methods.methods | Where-Object {
        $_.name -eq "save" -and @($_.params).Count -eq 2
    })
    if ($saveMethod.Count -eq 0) { throw "taskStore.save/2 no está expuesto" }

    $task = @{
        name = "Headless smoke loop"
        description = "CRUD y persistencia sin modelo"
        loopEnabled = $true
        loopGoal = "el agente confirma la verificación"
        loopMaxIterations = 3
        loopMaxSeconds = 30
    }
    $id = (Invoke-Target "taskStore" "save" @("headless-smoke-loop", $task)).result
    if ([string]::IsNullOrWhiteSpace($id)) { throw "taskStore.save no devolvió id" }
    $saved = (Invoke-Target "taskStore" "get" @($id)).result
    if ($saved.loopMaxSeconds -ne 30) { throw "loopMaxSeconds no persistió correctamente" }

    if ($TestRestartPersistence) {
        Stop-Process -Id $proc.Id -Force
        $proc = Start-Process -FilePath $Exe -ArgumentList "--agent-daemon" -WindowStyle Hidden -PassThru
        $restarted = $false
        for ($i = 0; $i -lt 30; $i++) {
            try { if ((Invoke-Api "/health").ok) { $restarted = $true; break } } catch {}
            Start-Sleep -Milliseconds 250
        }
        if (-not $restarted) { throw "El daemon no volvió después del reinicio" }
        $afterRestart = (Invoke-Target "taskStore" "get" @($id)).result
        if ($afterRestart.id -ne $id -or $afterRestart.loopGoal -ne $task.loopGoal) {
            throw "La Task no sobrevivió al reinicio del daemon"
        }
    }

    if ($TestSchedulerDaemon) {
        $env:LLAMACODE_SCHEDULER_SMOKE = "1"
        $schedulerProc = Start-Process -FilePath $Exe -ArgumentList "--scheduler-daemon" -WindowStyle Hidden -PassThru
        $schedulerRunning = $false
        for ($i = 0; $i -lt 30; $i++) {
            $status = (Invoke-Target "" "schedulerDaemonStatus" @()).result
            if ($status.running) { $schedulerRunning = $true; break }
            Start-Sleep -Milliseconds 500
        }
        if (-not $schedulerRunning) { throw "El scheduler daemon no publicó un heartbeat" }
    }

    $workflow = @{ schemaVersion = 1; entry = "finish"; steps = @{ finish = @{ type = "finish" } } }
    $validation = (Invoke-Target "" "validateWorkflow" @($workflow)).result
    if ($validation -ne "") { throw "validateWorkflow rechazó un workflow válido: $validation" }

    if ($RunModelLoop) {
        if ([string]::IsNullOrWhiteSpace($LaunchId)) { throw "-LaunchId es obligatorio con -RunModelLoop" }
        Invoke-Target "" "startServerAndAgent" @($LaunchId) | Out-Null
        Invoke-Target "" "runTask" @($id) | Out-Null
        do { Start-Sleep -Seconds 2 } while ([bool](Get-Prop "" "taskRunning"))
        $result = (Invoke-Target "taskStore" "get" @($id)).result
        if ($result.lastRunStatus -notin @("ok", "error")) { throw "La ejecución no terminó: $($result.lastRunStatus)" }
        $result | ConvertTo-Json -Depth 20
    } else {
        Write-Output "PASS: daemon, hardware/topology, performance matrix, ControlApi, TaskStore CRUD, persistencia y workflow validation headless"
    }
}
finally {
    if ($schedulerProc -and -not $schedulerProc.HasExited) { Stop-Process -Id $schedulerProc.Id -Force }
    if ($proc -and -not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
    Remove-Item Env:LLAMACODE_SCHEDULER_SMOKE -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $profileDir -Recurse -Force -ErrorAction SilentlyContinue
}
