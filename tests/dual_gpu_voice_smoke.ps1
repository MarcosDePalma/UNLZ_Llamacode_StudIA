param(
    [int]$Port = 8897,
    [string]$Exe = ".\build\Debug\LlamaCode.exe",
    [switch]$RequireRtx3090
)

$ErrorActionPreference = "Stop"
$qtRoot = "C:\Qt\6.8.3\msvc2022_64"
$env:QT_QPA_PLATFORM = "offscreen"
$env:QT_PLUGIN_PATH = Join-Path $qtRoot "plugins"
$env:PATH = (Join-Path $qtRoot "bin") + ";" + $env:PATH
$profileDir = Join-Path (Get-Location) (".dual-gpu-voice-smoke-{0}" -f $PID)
$env:LLAMACODE_CONTROL_PORT = "$Port"
$env:LLAMACODE_PROFILES_DIR = $profileDir
New-Item -ItemType Directory -Force $profileDir | Out-Null
$proc = $null

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
    $query = if ([string]::IsNullOrEmpty($Target)) { "?name=$Name" } else { "?target=$Target&name=$Name" }
    (Invoke-Api "/prop$query").value
}

try {
    $smi = Get-Command nvidia-smi -ErrorAction SilentlyContinue
    if (-not $smi) { throw "No se encontró nvidia-smi; esta prueba requiere NVIDIA." }

    $physical = @(& $smi.Source --query-gpu=index,name,memory.total,memory.free,memory.used,pci.bus_id --format=csv,noheader,nounits |
        ForEach-Object {
            $p = $_ -split ','
            if ($p.Count -ge 6) {
                [pscustomobject]@{
                    Index = [int]$p[0].Trim()
                    Name = $p[1].Trim()
                    TotalMb = [double]$p[2].Trim()
                    FreeMb = [double]$p[3].Trim()
                    UsedMb = [double]$p[4].Trim()
                    BusId = $p[5].Trim()
                }
            }
        })
    if ($physical.Count -lt 2) { throw "nvidia-smi reportó menos de dos GPU físicas." }
    if ($RequireRtx3090) {
        foreach ($gpu in $physical | Select-Object -First 2) {
            if ($gpu.Name -notmatch "RTX 3090" -or [math]::Abs($gpu.TotalMb - 24576) -gt 512) {
                throw "La GPU $($gpu.Index) no coincide con una RTX 3090 de 24 GiB: $($gpu.Name), $($gpu.TotalMb) MiB."
            }
        }
    }

    $weakPhysical = $physical | Sort-Object @{Expression = { $_.TotalMb }; Ascending = $true}, @{Expression = { $_.FreeMb }; Ascending = $true}, @{Expression = { $_.Index }; Ascending = $true} | Select-Object -First 1
    $proc = Start-Process -FilePath $Exe -ArgumentList "--agent-daemon --handoff-ui" -WindowStyle Hidden -PassThru
    $ready = $false
    for ($i = 0; $i -lt 40; $i++) {
        try { if ((Invoke-Api "/health").ok) { $ready = $true; break } } catch {}
        Start-Sleep -Milliseconds 250
    }
    if (-not $ready) { throw "El daemon no respondió en el puerto $Port." }

    Invoke-Target "" "runStartupScan" @() | Out-Null
    $hardware = $null
    for ($i = 0; $i -lt 60; $i++) {
        try {
            $hardware = Get-Prop "" "hardwareSummary"
            $hardwareReady = $hardware.summary -and $hardware.summary -notmatch "Detectando" -and [int]$hardware.gpuCount -ge 2
            if ($hardwareReady) { break }
        } catch {}
        Start-Sleep -Milliseconds 250
    }
    if (-not $hardware -or [int]$hardware.gpuCount -lt 2) {
        throw "LlamaCode no publicó dos GPU en hardwareSummary: gpuCount=$($hardware.gpuCount), summary=$($hardware.summary)."
    }
    $plan = (Invoke-Target "" "voiceGpuPlan" @()).result
    if (-not $plan.enabled) { throw "voiceGpuPlan no se habilitó con dos GPU físicas." }
    if ([int]$plan.voiceGpuIndex -ne [int]$weakPhysical.Index) {
        throw "La GPU de voz no coincide: esperaba $($weakPhysical.Index), obtuvo $($plan.voiceGpuIndex)."
    }
    if ([string]::IsNullOrWhiteSpace($plan.voiceGpuMask)) { throw "voiceGpuMask quedó vacío." }
    if ([string]::IsNullOrWhiteSpace($plan.modelTensorSplit) -or @($plan.modelTensorSplit -split ',').Count -lt 2) {
        throw "modelTensorSplit no representa las dos GPU: $($plan.modelTensorSplit)."
    }
    if ([double]$plan.voiceReserveMb -lt 2048) { throw "La reserva de voz es menor a 2048 MiB." }

    Write-Output ("PASS: {0} GPU físicas; voz=GPU {1} ({2}); split={3}; libre física={4:N0} MiB; fingerprint={5}" -f `
        $physical.Count, $plan.voiceGpuIndex, $weakPhysical.Name, $plan.modelTensorSplit,
        $weakPhysical.FreeMb, $hardware.hardwareFingerprint)
}
finally {
    if ($proc -and -not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
    Remove-Item Env:LLAMACODE_CONTROL_PORT -ErrorAction SilentlyContinue
    Remove-Item Env:LLAMACODE_PROFILES_DIR -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $profileDir -Recurse -Force -ErrorAction SilentlyContinue
}
