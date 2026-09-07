param(
    [int]$Port = 18766,
    [string]$Root = $PSScriptRoot
)

$ErrorActionPreference = 'Stop'
$resolvedRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
$listener = [Net.HttpListener]::new()
$listener.Prefixes.Add("http://127.0.0.1:$Port/")

function Get-ContentType([string]$Path) {
    switch ([IO.Path]::GetExtension($Path).ToLowerInvariant()) {
        '.html' { 'text/html; charset=utf-8'; break }
        '.css' { 'text/css; charset=utf-8'; break }
        '.js' { 'text/javascript; charset=utf-8'; break }
        '.json' { 'application/json; charset=utf-8'; break }
        default { 'application/octet-stream' }
    }
}

$apiCache = @()
$apiCacheAt = [DateTime]::MinValue
$snapshotCache = $null
$snapshotCacheAt = [DateTime]::MinValue
$benchmarkRoot = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'LlamaCode\LlamaCode\benchmark-runs'

function Get-BenchmarkStage([object]$Result) {
    $name = [string]$Result.benchmarkName
    if ([string]::IsNullOrWhiteSpace($name)) { $name = [string]$Result.runLabel }
    if ($name -match 'BigCodeBench') { return 'BCB' }
    if ($name -match 'HumanEval.*20') { return 'HE20' }
    if ($name -match 'HumanEval') { return 'HE0' }
    return $null
}

function Get-BenchmarkSnapshot {
    $now = Get-Date
    if (($now - $snapshotCacheAt).TotalSeconds -lt 4 -and $null -ne $snapshotCache) {
        return $snapshotCache
    }

    # ControlApi can be temporarily starved while the benchmark executes a
    # synchronous acceptance command. Keep the dashboard useful in that window
    # by reading the already-persisted per-stage JSON files directly.
    $latest = @{}
    if (Test-Path -LiteralPath $benchmarkRoot -PathType Container) {
        foreach ($file in Get-ChildItem -LiteralPath $benchmarkRoot -Recurse -Filter '*.json' -File -ErrorAction SilentlyContinue) {
            if ($file.Name -in @('metadata.json', 'comparison.json', '.resume.json')) { continue }
            try { $raw = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json } catch { continue }
            $stage = Get-BenchmarkStage $raw
            $profileId = [string]$raw.profileId
            if ([string]::IsNullOrWhiteSpace($stage) -or [string]::IsNullOrWhiteSpace($profileId)) { continue }

            $identity = @(
                $profileId,
                [string]$raw.profileConfigFingerprint,
                [string]$raw.agentProfileId,
                [string]$raw.harnessSpecHash
            ) -join '|'
            $key = "$identity|$stage"
            $timestamp = 0.0
            try { $timestamp = [double]$raw.timestamp } catch { }
            if ($latest.ContainsKey($key) -and [double]$latest[$key].timestamp -gt $timestamp) { continue }

            # Keep the payload small: the full task transcripts are useful on
            # disk, but the dashboard only needs the stage/result metadata.
            $row = [ordered]@{}
            foreach ($property in @(
                'id','profileId','profileName','profileConfigFingerprint','benchmarkName','runLabel',
                'mode','target','agentProfileId','agentProfileName','agentVariant','harnessEngineId',
                'harnessEngineVersion','harnessSpecHash','thinkingEnabled','agentTemperature','agentSeed',
                'qualityScore','qualityTotal','firstAttemptScore','firstAttemptTotal','finalScore','finalTotal',
                'repairAttempts','passedAfterRepair','transportAfterEvaluation','avgTps','avgTtftMs',
                'elapsedSec','totalTime','generationSec','nonGenerationSec','firstToolCallSec','firstWriteSec',
                'firstEvaluableSec','timeToFirstAttempt','setupSec','measurementPhase','failed','invalid',
                'timedOut','failureKind','failureStage','failureMessage','failureDetail','runDir','workspace',
                'timestamp'
            )) {
                if ($raw.PSObject.Properties.Name -contains $property) { $row[$property] = $raw.$property }
            }
            $latest[$key] = [pscustomobject]$row
        }
    }
    $results = @($latest.Values | Sort-Object @{Expression={ [double]$_.timestamp }})

    $groups = @{}
    foreach ($result in $results) {
        $id = [string]$result.profileId
        if (-not $groups.ContainsKey($id)) {
            $groups[$id] = @{ name = [string]$result.profileName; stages = @{} }
        }
        $groups[$id].name = [string]$result.profileName
        $groups[$id].stages[(Get-BenchmarkStage $result)] = $result
    }
    $coverage = foreach ($entry in $groups.GetEnumerator()) {
        $states = [ordered]@{}
        foreach ($stage in @('HE0','HE20','BCB')) {
            $result = $entry.Value.stages[$stage]
            $states[$stage] = if ($null -eq $result) { 'pending' }
                elseif ([double]$result.qualityTotal -gt 0 -and -not [bool]$result.failed -and -not [bool]$result.invalid) { 'valid' }
                else { 'failed' }
        }
        $next = @('HE0','HE20','BCB') | Where-Object { $states[$_] -ne 'valid' } | Select-Object -First 1
        [ordered]@{
            profileId = $entry.Key
            profileName = $entry.Value.name
            coverageState = if (@($states.Values | Where-Object { $_ -eq 'valid' }).Count -eq 3) { 'complete' } else { 'incomplete' }
            nextStage = $next
            benchmarkEligible = $true
            retired = $false
            stageStates = $states
            healthCodes = @()
        }
    }

    $resume = $null
    $pending = @()
    $runLabel = ''
    $agentProfileId = ''
    $totalProfiles = 0
    $resumeFile = Join-Path $benchmarkRoot '.resume.json'
    if (Test-Path -LiteralPath $resumeFile -PathType Leaf) {
        try { $resume = Get-Content -LiteralPath $resumeFile -Raw | ConvertFrom-Json } catch { $resume = $null }
    }
    if ($null -ne $resume) {
        $pending = @($resume.pending)
        $runLabel = [string]$resume.runLabel
        $agentProfileId = [string]$resume.agentProfileId
        $metadataPath = if ($resume.runDir) { Join-Path ([string]$resume.runDir) 'metadata.json' } else { '' }
        if ($metadataPath -and (Test-Path -LiteralPath $metadataPath -PathType Leaf)) {
            try { $meta = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json; $totalProfiles = @($meta.profiles).Count } catch { }
        }
    }
    if ($totalProfiles -le 0) { $totalProfiles = @($groups.Keys).Count }
    $completedProfiles = [Math]::Max(0, $totalProfiles - $pending.Count)
    $progress = if ($totalProfiles -gt 0) { [int](100 * $completedProfiles / $totalProfiles) } else { 0 }
    $running = $null -ne $resume
    $status = if ($running) {
        "Benchmark en curso · $agentProfileId · $runLabel · resultados guardados hasta ahora"
    } else { 'Resultados guardados en disco; ControlApi no disponible.' }

    $snapshotCache = [ordered]@{
        source = 'filesystem'
        generatedAt = $now.ToUniversalTime().ToString('o')
        running = $running
        progress = $progress
        status = $status
        pendingCount = $pending.Count
        results = $results
        coverage = @($coverage)
    }
    $snapshotCacheAt = $now
    return $snapshotCache
}

function Get-ListeningLocalPorts {
    $ports = @()
    $llamaProcessIds = @(Get-Process -Name 'LlamaCode' -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id)
    try {
        $connections = @(Get-NetTCPConnection -State Listen -ErrorAction Stop |
            Where-Object { $_.LocalAddress -in @('127.0.0.1', '0.0.0.0', '::1', '::') })
        if ($llamaProcessIds.Count -gt 0) {
            $connections = @($connections | Where-Object { $llamaProcessIds -contains $_.OwningProcess })
        }
        $ports = @($connections |
            Select-Object -ExpandProperty LocalPort -Unique)
    } catch {
        # Get-NetTCPConnection may be unavailable on older Windows images.
        $ports = @(netstat -ano -p tcp 2>$null | ForEach-Object {
            if ($_ -match 'LISTENING\s+\d+$' -and $_ -match '(?:127\.0\.0\.1|0\.0\.0\.0|\[::\]|::):(?<port>\d+)') {
                [int]$Matches.port
            }
        } | Sort-Object -Unique)
    }
    # Keep the documented/default ports as a cheap fallback when the daemon
    # is hosted by a wrapper process instead of LlamaCode.exe.
    return @($ports + 8765 + 8877 + 18774 | Where-Object { $_ -and $_ -ne $Port } | Sort-Object -Unique)
}

function Get-BenchmarkApiCandidates {
    $now = Get-Date
    if (($now - $apiCacheAt).TotalSeconds -lt 2) { return @($apiCache) }

    # La validación semántica (/health + /methods) la hace el navegador en
    # paralelo. Acá sólo enumeramos candidatos baratos; no bloqueamos el
    # servidor estático esperando servicios no relacionados.
    $found = @(Get-ListeningLocalPorts | ForEach-Object {
        [pscustomobject]@{ url = "http://127.0.0.1:$_"; port = [int]$_ }
    })
    $apiCache = @($found | Sort-Object port)
    $apiCacheAt = $now
    return @($apiCache)
}

try {
    $listener.Start()
    Write-Host "Benchmark dashboard: http://127.0.0.1:$Port/benchmark-dashboard.html"
    while ($listener.IsListening) {
        $context = $listener.GetContext()
        try {
            $relative = [Uri]::UnescapeDataString($context.Request.Url.AbsolutePath.TrimStart('/'))
            if ([string]::IsNullOrWhiteSpace($relative)) { $relative = 'benchmark-dashboard.html' }
            if ($relative -eq 'favicon.ico') {
                $context.Response.StatusCode = 204
                $context.Response.Close()
                continue
            }
            if ($relative -eq 'benchmark-api.json') {
                $payload = [ordered]@{
                    ok = $true
                    generatedAt = (Get-Date).ToUniversalTime().ToString('o')
                    apis = @(Get-BenchmarkApiCandidates)
                }
                $bytes = [Text.Encoding]::UTF8.GetBytes(($payload | ConvertTo-Json -Depth 4 -Compress))
                $context.Response.ContentType = 'application/json; charset=utf-8'
                $context.Response.ContentLength64 = $bytes.Length
                $context.Response.Headers['Cache-Control'] = 'no-store'
                $context.Response.OutputStream.Write($bytes, 0, $bytes.Length)
                $context.Response.Close()
                continue
            }
            if ($relative -eq 'benchmark-snapshot.json') {
                $payload = Get-BenchmarkSnapshot | ConvertTo-Json -Depth 30 -Compress
                $bytes = [Text.Encoding]::UTF8.GetBytes($payload)
                $context.Response.ContentType = 'application/json; charset=utf-8'
                $context.Response.ContentLength64 = $bytes.Length
                $context.Response.Headers['Cache-Control'] = 'no-store'
                $context.Response.OutputStream.Write($bytes, 0, $bytes.Length)
                $context.Response.Close()
                continue
            }
            $candidate = [IO.Path]::GetFullPath((Join-Path $resolvedRoot $relative))
            if (-not $candidate.StartsWith($resolvedRoot, [StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
                $context.Response.StatusCode = 404
                $context.Response.Close()
                continue
            }
            $bytes = [IO.File]::ReadAllBytes($candidate)
            $context.Response.ContentType = Get-ContentType $candidate
            $context.Response.ContentLength64 = $bytes.Length
            $context.Response.Headers['Cache-Control'] = 'no-store'
            $context.Response.OutputStream.Write($bytes, 0, $bytes.Length)
            $context.Response.Close()
        } catch {
            try { $context.Response.StatusCode = 500; $context.Response.Close() } catch { }
        }
    }
} finally {
    if ($listener.IsListening) { $listener.Stop() }
    $listener.Close()
}
