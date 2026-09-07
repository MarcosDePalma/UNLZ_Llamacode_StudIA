# Barrido A/B de HARNESS: corre el mismo benchmark con dos (o mas) perfiles de
# AGENTE sobre el MISMO launch profile, y compara los resultados agrupados por
# perfil de agente. Es el cierre del ciclo del harness modular: personalizar sin
# medir es adivinar.
#
# Requiere un daemon headless corriendo (ver docs/HEADLESS.md):
#   $env:LLAMACODE_CONTROL_PORT = "8877"
#   Start-Process .\build\Release\LlamaCode.exe -ArgumentList "--agent-daemon"
#
# Uso:
#   powershell -File tools\harness_ab.ps1 -LaunchProfileId <id> `
#       -AgentProfileIds agent-intermedio,agent-minimal `
#       [-CustomBenchmarkId <id>] [-Mode coding] [-Passes 3] [-TimeoutSec 900] `
#       [-Port 8877] [-Out harness-ab.json]
#
# El script NO elige por vos: imprime el informe (medianas de calidad, tiempo,
# tasa de exito y complejidad por perfil, mas los deltas entre pares) y lo deja
# en JSON. Un harness solo es mejor si no baja calidad ni exito.
#
# ASCII puro: Windows PowerShell 5.1 lee un .ps1 sin BOM como ANSI y un caracter
# UTF-8 dentro de un string puede terminarlo (ver CLAUDE.md).

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]   $LaunchProfileId,
    [Parameter(Mandatory = $true)][string[]] $AgentProfileIds,
    [string] $CustomBenchmarkId = "",
    [string] $Mode = "coding",
    [int]    $Passes = 5,
    [int]    $TimeoutSec = 0,
    [int]    $Port = 8877,
    [int]    $PollSeconds = 10,
    [int]    $MaxWaitMinutes = 240,
    [string] $Out = "harness-ab.json",
    [int]    $OrderSeed = 4242
)

$ErrorActionPreference = "Stop"
$base = "http://127.0.0.1:$Port"

# Invocado con -File (CI, cmd, otro script), PowerShell NO separa por comas: la
# lista llega como UN string. Sin esto, '-AgentProfileIds a,b' se interpretaba
# como un unico perfil llamado "a,b" y el script cortaba diciendo que no existe.
$AgentProfileIds = @($AgentProfileIds | ForEach-Object { $_ -split ',' } |
                     ForEach-Object { $_.Trim() } | Where-Object { $_.Length -gt 0 })
if ($AgentProfileIds.Count -lt 1) { throw "Falta -AgentProfileIds." }
if ($Passes -lt 1) { throw "-Passes debe ser mayor o igual a 1." }
if ($Passes -lt 5) {
    Write-Host "AVISO: menos de 5 pasadas; el resultado sirve como smoke/A-B rapido, no como comparacion estable." -ForegroundColor Yellow
}

function Inv([string]$target, [string]$method, $arguments) {
    $body = @{ method = $method; args = $arguments } | ConvertTo-Json -Depth 12 -Compress
    return Invoke-RestMethod "$base/invoke?target=$target" -Method Post `
        -ContentType "application/json" -Body $body
}
function Prop([string]$target, [string]$name) {
    return (Invoke-RestMethod "$base/prop?target=$target&name=$name").value
}

# El daemon tiene que estar arriba: si no, fallar temprano y claro en vez de
# arrastrar un error de conexion adentro del bucle de polling.
try { Invoke-RestMethod "$base/health" | Out-Null }
catch { throw "No hay daemon headless en $base. Arrancalo con --agent-daemon (docs/HEADLESS.md)." }

# Los perfiles de agente tienen que existir: un id mal escrito correria el
# benchmark con el nivel por defecto y la comparacion seria una mentira.
foreach ($ap in $AgentProfileIds) {
    $spec = Inv "profileManager" "agentProfileSpec" @($ap)
    if (-not $spec.ok) { throw "Perfil de agente inexistente: $ap" }
    $summary = Inv "profileManager" "harnessSpecSummary" @($ap, "")
    $s = $summary.result
    Write-Host ("[{0}] {1} tools, ~{2} tok de schemas" -f $ap, $s.toolCount, $s.approxTokens)
    foreach ($w in $s.warnings) { Write-Host ("  aviso: {0}" -f $w) -ForegroundColor Yellow }
}

function Wait-Benchmark {
    $deadline = (Get-Date).AddMinutes($MaxWaitMinutes)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds $PollSeconds
        if (-not (Prop "" "benchmarkRunning")) { return $true }
        $progress = Prop "" "benchmarkProgress"
        if ($progress) { Write-Host ("  {0}" -f $progress) }
    }
    return $false
}

function Get-Shuffled([string[]]$Items, [int]$Seed) {
    $shuffled = [System.Collections.ArrayList]@($Items)
    $random = [System.Random]::new($Seed)
    for ($i = $shuffled.Count - 1; $i -gt 0; $i--) {
        $j = $random.Next($i + 1)
        $tmp = $shuffled[$i]
        $shuffled[$i] = $shuffled[$j]
        $shuffled[$j] = $tmp
    }
    return @($shuffled)
}

# Instante de arranque: la comparacion final se acota a las corridas de ESTE
# barrido. Sin esto el informe suma el historial del usuario y un perfil con 30
# corridas viejas "gana" contra uno recien creado con una.
$sinceEpochMs = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
$ordersByPass = @()

# Intercalar perfiles por pasada evita que un perfil quede siempre primero (cache
# fria) o siempre ultimo (temperatura/throttling). Cada llamada es una sola pasada
# para que todos acumulen exactamente la misma cantidad de muestras.
for ($pass = 1; $pass -le $Passes; $pass++) {
    $order = @(Get-Shuffled -Items $AgentProfileIds -Seed ($OrderSeed + $pass - 1))
    $ordersByPass += ,$order
    Write-Host ("=== Pasada {0}/{1}: {2}" -f $pass, $Passes, ($order -join ", ")) -ForegroundColor Cyan
    foreach ($ap in $order) {
        if ($CustomBenchmarkId) {
            $started = Inv "" "startCustomBenchmark" @(@($LaunchProfileId), $CustomBenchmarkId, 1,
                                                       "agent", $TimeoutSec, $ap)
        } else {
            $started = Inv "" "startBenchmark" @(@($LaunchProfileId), $Mode, 1,
                                             "agent", $TimeoutSec, $ap)
        }
        if ($started.ok -ne $true) {
            throw "No se pudo iniciar el benchmark para el perfil '$ap'."
        }
        if (-not (Wait-Benchmark)) { throw "Timeout esperando el benchmark del perfil '$ap'." }
    }
}

$report = (Inv "" "compareHarnessBenchmarks" @($AgentProfileIds, "", $sinceEpochMs)).result
$report | Add-Member -Force -NotePropertyName passesRequested -NotePropertyValue $Passes
$report | Add-Member -Force -NotePropertyName orderSeed -NotePropertyValue $OrderSeed
$report | Add-Member -Force -NotePropertyName profileOrderByPass -NotePropertyValue @($ordersByPass)

# El informe debe ser balanceado de verdad: un perfil sin todas sus muestras no
# puede compararse con los demas aunque el API lo marque como parcialmente ok.
foreach ($ap in $AgentProfileIds) {
    $row = @($report.profiles | Where-Object { $_.profileId -eq $ap }) | Select-Object -First 1
    if ($null -eq $row) { throw "El informe no contiene el perfil '$ap'." }
    if ([int]$row.runs -ne $Passes) {
        throw ("El perfil '{0}' tiene {1} corridas; se esperaban {2}. Comparacion abortada por desbalance." -f $ap, $row.runs, $Passes)
    }
}
$report | ConvertTo-Json -Depth 12 | Set-Content -Path $Out -Encoding UTF8

Write-Host ""
Write-Host "=== Resultado por perfil de agente" -ForegroundColor Cyan
# Las medianas de calidad/tiempo se calculan SOLO sobre corridas exitosas (una
# corrida fallida no debe contaminar la mediana). Con pocas pasadas eso deja un
# perfil sin ninguna muestra y "0,0%" se lee como "peor", cuando en realidad es
# "sin dato": hay que decirlo, no imprimir un cero.
foreach ($p in $report.profiles) {
    if ($p.successfulRuns -gt 0) {
        $toolF1 = if ($p.PSObject.Properties.Name -contains 'medianToolF1Pct') { $p.medianToolF1Pct } else { -1 }
        Write-Host ("{0,-24} calidad {1,6:N1}%  exito {2,6:N1}%  tiempo {3,7:N1}s  tools F1 {4,6:N1}%  archivos {5,4:N1}  runs {6}" -f `
            $p.profileName, $p.medianQualityPct, $p.successRatePct, $p.medianElapsedSec,
            $toolF1, $p.medianFilesChanged, $p.runs)
    } else {
        Write-Host ("{0,-24} calidad     s/d  exito {1,6:N1}%  tiempo     s/d  (0 de {2} corridas pasaron los criterios)" -f `
            $p.profileName, $p.successRatePct, $p.runs) -ForegroundColor Yellow
    }
}
Write-Host ""
Write-Host "=== Deltas (candidato vs baseline)" -ForegroundColor Cyan
# Formato con signo explicito: "{n,+6:N1}" NO es valido en .NET (el alineado es
# un entero, no admite '+') y tiraba FormatException justo antes del resumen.
function Signed($v) { return ("{0:+0.0;-0.0;0.0}" -f [double]$v) }
$noSamples = @($report.profiles | Where-Object { $_.successfulRuns -le 0 } |
                ForEach-Object { $_.profileId })
foreach ($c in $report.comparisons) {
    Write-Host ("{0} -> {1}: calidad {2,7} pp  exito {3,7} pp  tiempo {4,7}%  archivos {5,6}" -f `
        $c.baselineProfileId, $c.candidateProfileId, (Signed $c.qualityDeltaPctPoints),
        (Signed $c.successRateDeltaPctPoints), (Signed $c.comparisonTimeChangePct),
        (Signed $c.filesChangedDelta))
    $toolF1Delta = if ($c.PSObject.Properties.Name -contains 'toolF1DeltaPctPoints') {
        Signed $c.toolF1DeltaPctPoints
    } else { 's/d' }
    $toolSuccessDelta = if ($c.PSObject.Properties.Name -contains 'toolSuccessRateDeltaPctPoints') {
        Signed $c.toolSuccessRateDeltaPctPoints
    } else { 's/d' }
    $toolRedundantDelta = if ($c.PSObject.Properties.Name -contains 'toolRedundantCallsDelta') {
        Signed $c.toolRedundantCallsDelta
    } else { 's/d' }
    Write-Host ("   tools: F1 {0,7} pp  exito {1,7} pp  redundancias {2,7}" -f `
        $toolF1Delta, $toolSuccessDelta, $toolRedundantDelta)
    if ($noSamples -contains $c.baselineProfileId -or $noSamples -contains $c.candidateProfileId) {
        Write-Host ("   ^ delta NO interpretable: uno de los dos no tiene corridas exitosas. " +
                    "Suma pasadas (-Passes) o revisa por que fallo.") -ForegroundColor Yellow
    }
}
Write-Host ""
if ($report.balanced -ne $true) {
    Write-Host ("AVISO: la comparacion esta DESBALANCEADA (corridas por perfil: " +
                (($report.profiles | ForEach-Object { $_.runs }) -join " vs ") +
                "). Con muestras tan distintas los deltas no son comparables.") -ForegroundColor Red
}
Write-Host "Informe completo: $Out"
Write-Host "Un harness solo es mejor si NO baja calidad ni tasa de exito." -ForegroundColor Yellow
