[CmdletBinding()]
param(
    [datetime]$Since = (Get-Date).AddDays(-1),
    [string]$Root = (Join-Path $env:LOCALAPPDATA 'LlamaCode\LlamaCode\benchmark-runs'),
    [string]$Out = 'benchmark-full-matrix-results'
)

$ErrorActionPreference = 'Stop'

function Get-Stage([string]$Name) {
    if ($Name -match '^HumanEval \(1') { return 'HE0' }
    if ($Name -match '^HumanEval \(20') { return 'HE20' }
    if ($Name -match '^BigCodeBench-Hard') { return 'BCB' }
    return ''
}

function Get-Tier([string]$Id, [string]$Name) {
    if ($Id -in @('8797a8cf-fea9-46cb-934a-0d62f3ee8ca7', 'abc1df7a-2af1-4957-9d12-dbe2d01988aa')) { return 'SOL' }
    if ($Id -in @('7d54c7f2-47dd-43df-a608-f67e4d4b027d', '2c25280b-819e-411c-92fc-c5127cb3b900')) { return 'TERRA' }
    if ($Id -in @('a03e65f5-2f2c-4d45-b67b-4b1270fa2a6c', 'sys-48-thinkingcap-131k', 'sys-48-thinkingcap-196k') -or $Name -match 'ThinkingCap') { return 'LUNA' }
    if ($Id -in @('cbff7c85-2116-4b42-b1b9-485dd33384cc') -or $Id -match 'bigbang') { return 'METEOR' }
    if ($Id -match 'antirez') { return 'DeepSeek Antirez' }
    if ($Id -match 'dsv4|ultraq') { return 'DeepSeek IQ3_S' }
    return 'Otros'
}

if (-not (Test-Path -LiteralPath $Root)) {
    throw "No existe el directorio de corridas: $Root"
}

$rows = @()
Get-ChildItem -LiteralPath $Root -Filter '*.json' -Recurse -File |
    Where-Object { $_.Name -notin @('metadata.json', 'comparison.json') } |
    ForEach-Object {
        try { $r = Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json }
        catch { return }
        if ($r -is [array]) { return }
        if (-not $r.timestamp) { return }
        $stamp = @($r.timestamp) | Select-Object -First 1
        $when = [DateTimeOffset]::FromUnixTimeMilliseconds([int64]$stamp).LocalDateTime
        if ($when -lt $Since) { return }
        $stage = Get-Stage $r.benchmarkName
        if (-not $stage) { return }
        $score = [double]$r.qualityScore
        $total = [double]$r.qualityTotal
        $rows += [pscustomobject]@{
            stage = $stage
            tier = Get-Tier $r.profileId $r.profileName
            profileId = $r.profileId
            profileName = $r.profileName
            harness = $r.agentProfileId
            harnessName = $r.agentProfileName
            quality = if ($total -gt 0) { "$([int]$score)/$([int]$total)" } else { 's/d' }
            qualityScore = [int]$score
            qualityTotal = [int]$total
            tps = [math]::Round([double]$r.avgTps, 2)
            ttftMs = [math]::Round([double]$r.avgTtftMs, 1)
            totalTimeSec = [math]::Round([double]$r.totalTime, 1)
            elapsedSec = [math]::Round([double]$r.elapsedSec, 1)
            reasoningTokens = [int]$r.reasoningTokens
            failed = [bool]$r.failed
            invalid = [bool]$r.invalid
            failureStage = [string]$r.failureStage
            timestamp = $when.ToString('o')
            source = $_.FullName
        }
    }

$rows = @($rows | Sort-Object timestamp, tier, profileName, harness, stage)
$bcb = @($rows | Where-Object stage -eq 'BCB' | Group-Object profileId, harness | ForEach-Object {
    $_.Group | Sort-Object timestamp -Descending | Select-Object -First 1
})
$summary = [pscustomobject]@{
    generatedAt = (Get-Date).ToString('o')
    since = $Since.ToString('o')
    rowCount = $rows.Count
    bcbCount = $bcb.Count
    rows = $rows
    finalBcb = $bcb
}

$jsonPath = [IO.Path]::GetFullPath("$Out.json")
$csvPath = [IO.Path]::GetFullPath("$Out.csv")
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $jsonPath -Encoding UTF8
$rows | Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding UTF8
Write-Host "Exportados $($rows.Count) resultados ($($bcb.Count) BCB finales)."
Write-Host $jsonPath
Write-Host $csvPath
