[CmdletBinding()]
param(
    [string]$Base = 'http://127.0.0.1:8877',
    [int]$TimeoutSec = 1800,
    [int]$Passes = 1,
    [string[]]$Harnesses = @('agent-chat', 'agent-intermedio', 'agent-maximo'),
    [switch]$IncludeBrowserHarness
)

$ErrorActionPreference = 'Stop'

$he0 = '059b6f00-7b4b-48ff-a3a2-2a71f900c1c0'
$he20 = '267bb33d-4510-417b-ae3c-6dbbfb2cb08d'
$bcb = '05c28394-11d0-41fe-a55f-b3cb69db9c15'

# Keep this list explicit: a matrix run must be reviewable and reproducible even
# if the catalog later gains unrelated system profiles.
$profiles = @(
    # DeepSeek V4 Flash IQ3_S, 24/48 GB, no DSpark on Windows.
    'sys-bench-ultraq-b4096-u1024-nospec',
    'sys-bench-ultraq-b8192-u2048-nospec',
    'sys-bench-ultraq-b8192-u2048-kv8-nospec',
    'sys-bench-ultraq-b8192-u2048-kv-k8v4',
    'sys-bench-ultraq-64k-nospec',
    'sys-bench-ultraq-64k-kv8-nospec',
    'sys-bench-ultraq-reasoning-low',
    'sys-bench-ultraq-reasoning-medium',
    'sys-bench-ultraq-reasoning-high',
    'sys-ultraq-dsv4-0731-iq3s-48gb',
    'sys-48-dsv4-nospec',
    'sys-bench-ultraq-48gb-64k-nospec',
    'sys-bench-ultraq-48gb-64k-kv8',
    'sys-bench-ultraq-48gb-reasoning-off',
    'sys-bench-ultraq-48gb-reasoning-medium',
    'sys-bench-ultraq-48gb-reasoning-high',

    # DeepSeek V4 Antirez Q2/Q4 imatrix.
    'sys-48-antirez-dsv4-q2q4-0731-16k',
    'sys-48-antirez-dsv4-q2q4-0731-32k-b4096',
    'sys-48-antirez-dsv4-q2q4-0731-32k-b8192',
    'sys-48-antirez-dsv4-q2q4-64k',
    'sys-48-antirez-dsv4-q2q4-131k',
    'sys-48-antirez-dsv4-q2q4-kv8',
    'sys-48-antirez-dsv4-q2q4-kvf16',
    'sys-48-antirez-dsv4-q2q4-64k-kv8',
    'sys-48-antirez-dsv4-q2q4-prefill',
    'sys-48-antirez-dsv4-q2q4-32k-reasoning-off',
    'sys-48-antirez-dsv4-q2q4-32k-reasoning-low',
    'sys-48-antirez-dsv4-q2q4-32k-reasoning-high',
    'sys-48-antirez-dsv4-q2q4-131k-kv8',

    # SOL: Dynamic V3 DSH medium; TERRA: Browser Agent.
    '8797a8cf-fea9-46cb-934a-0d62f3ee8ca7',
    'abc1df7a-2af1-4957-9d12-dbe2d01988aa',
    '7d54c7f2-47dd-43df-a608-f67e4d4b027d',
    '2c25280b-819e-411c-92fc-c5127cb3b900',

    # LUNA: ThinkingCap; METEOR: BigBang repaired and controls.
    'a03e65f5-2f2c-4d45-b67b-4b1270fa2a6c',
    'sys-48-thinkingcap-131k',
    'sys-48-thinkingcap-196k',
    'sys-bench-48-tc-mtp',
    'sys-bench-48-tc-mtp-131k',
    'sys-bench-48-tc-kv4',
    'sys-bench-48-tc-b4096',
    'cbff7c85-2116-4b42-b1b9-485dd33384cc',
    'sys-bench-48-bigbang-mtp',
    'sys-bench-48-bigbang-long',
    'sys-bench-48-bigbang-post',
    'sys-repair-48-bigbang-mtp',
    'sys-bench-48-bigbang-mtp-ngram',
    'sys-repair-48-bigbang-base',
    'sys-repair-48-bigbang-mtp-balance'
)

if ($IncludeBrowserHarness -and $Harnesses -notcontains 'agent-browser') {
    $Harnesses += 'agent-browser'
}

function Invoke-Control([string]$Method, [object[]]$Arguments) {
    $body = @{ method = $Method; args = $Arguments } | ConvertTo-Json -Depth 20
    Invoke-RestMethod "$Base/invoke?target=" -Method Post -ContentType 'application/json' -Body $body
}

function Get-Prop([string]$Name) {
    (Invoke-RestMethod "$Base/prop?target=&name=$Name").value
}

if ((Get-Prop 'benchmarkRunning') -eq $true) {
    throw 'Ya hay un benchmark en ejecución; no se pisa una campaña activa.'
}

foreach ($harness in $Harnesses) {
    Write-Host "Iniciando escalera para $($profiles.Count) perfiles con $harness"
    Invoke-Control 'startThreeStageBenchmark' @($profiles, $he0, $he20, $bcb,
        $Passes, 'agent', $TimeoutSec, $harness) | Out-Host

    do {
        Start-Sleep -Seconds 10
        $running = Get-Prop 'benchmarkRunning'
        $status = Get-Prop 'benchmarkStatus'
        Write-Host "[$harness] $status"
    } while ($running -eq $true)

    $final = Get-Prop 'benchmarkStatus'
    if ($final -eq 'Cancelado.') {
        throw "La campaña fue cancelada durante el harness $harness."
    }
}

Write-Host "Matriz completa finalizada. Revisar benchmark-runs y exportar TPS/tiempos antes del ranking."
