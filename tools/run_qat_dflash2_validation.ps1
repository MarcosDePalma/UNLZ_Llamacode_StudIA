[CmdletBinding()]
param(
    [int]$Port = 18237,
    [int]$ContextSize = 32768,
    [int]$MaxTokens = 256,
    [string]$RunTag = 'qat-dflash2-20260828',
    [ValidateSet('baseline-autoregressive', 'ngram-mod', 'dflash2-draft-q4', 'mtp-q4')]
    [string[]]$Modes = @('baseline-autoregressive', 'ngram-mod', 'dflash2-draft-q4')
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$server = 'D:\Models\llamacpp\llama.cpp-b10331-cuda12.4\llama-server.exe'
$model = 'D:\Models\llamacpp\Qwen3.8-27B-GGUF-Q4_K_M\Qwen3.8-27B-Q4_K_M.gguf'
$draftQ4 = 'D:\Models\llamacpp\Qwen3.8-27B-DFlash2-GGUF\Qwen3.8-27B-DFlash2-Q4_K_M.v2.gguf'
$mtpQ4 = 'D:\Models\llamacpp\Qwen3.8-27B-GGUF-DynamicV3\MTP\mtp-Qwen3.8-27B-Q4_0.gguf'
$template = (Resolve-Path (Join-Path $PSScriptRoot '..\assets\chat-templates\qwen38-tools-fixed.jinja')).Path
$outputRoot = Join-Path $PSScriptRoot ('..\docs\benchmark-artifacts\' + $RunTag)
$base = "http://127.0.0.1:$Port"

foreach ($required in @($server, $model, $draftQ4, $mtpQ4, $template)) {
    if (!(Test-Path -LiteralPath $required)) {
        throw "Missing required file: $required"
    }
}

New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

$tasks = @(
    [ordered]@{
        id = 'last-digit'
        expected = '(?im)FINAL:\s*3\s*$'
        prompt = 'Compute the last digit of 7^(7^7), with right associativity. Explain briefly and end with exactly FINAL: 3.'
    },
    [ordered]@{
        id = 'mississippi'
        expected = '(?im)FINAL:\s*34650\s*$'
        prompt = 'How many distinct permutations does MISSISSIPPI have? Explain briefly and end with exactly FINAL: 34650.'
    },
    [ordered]@{
        id = 'python-output'
        expected = '(?im)FINAL:\s*\[1,4,9\]\s*$'
        prompt = 'What does Python print for [x*x for x in range(1,4)]? Explain briefly and end with exactly FINAL: [1,4,9].'
    },
    [ordered]@{
        id = 'code-review'
        expected = '(?im)FINAL:\s*PASS\s*$'
        prompt = 'In one short paragraph, review this Python requirement: group dictionaries by the key run, preserve input order, never mutate input, and return an empty result for no rows. State the edge case and end with exactly FINAL: PASS.'
    }
)

function Get-ManifestEntry([string]$path) {
    $item = Get-Item -LiteralPath $path
    $hash = Get-FileHash -LiteralPath $path -Algorithm SHA256
    [ordered]@{
        path = $item.FullName
        bytes = $item.Length
        lastWriteTimeUtc = $item.LastWriteTimeUtc.ToString('o')
        sha256 = $hash.Hash.ToLowerInvariant()
    }
}

function Wait-Healthy([System.Diagnostics.Process]$process) {
    for ($attempt = 0; $attempt -lt 180; $attempt++) {
        if ($process.HasExited) {
            throw "llama-server exited before /health became ready (code $($process.ExitCode))"
        }
        try {
            $health = Invoke-RestMethod "$base/health" -TimeoutSec 3
            if ($health.status -eq 'ok') { return }
        } catch {}
        Start-Sleep -Seconds 2
    }
    throw "llama-server did not become healthy on port $Port"
}

function Invoke-Chat([string]$prompt) {
    $body = [ordered]@{
        messages = @([ordered]@{ role = 'user'; content = $prompt })
        max_tokens = $MaxTokens
        temperature = 0.6
        top_p = 0.95
        top_k = 20
        min_p = 0.0
        repeat_penalty = 1.0
        presence_penalty = 0.0
        seed = 42
        stream = $false
        cache_prompt = $false
    } | ConvertTo-Json -Depth 10
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $response = Invoke-RestMethod "$base/v1/chat/completions" -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 300
    $clock.Stop()
    [ordered]@{ response = $response; wallSec = [math]::Round($clock.Elapsed.TotalSeconds, 3) }
}

function Run-Mode([string]$label, [string[]]$specArgs) {
    $safeLabel = $label -replace '[^a-zA-Z0-9_-]', '_'
    $stdout = Join-Path $outputRoot "$safeLabel.server.stdout.log"
    $stderr = Join-Path $outputRoot "$safeLabel.server.stderr.log"
    $args = @(
        '-m', $model,
        '--host', '127.0.0.1', '--port', "$Port",
        '--ctx-size', "$ContextSize", '--parallel', '1', '--n-gpu-layers', '999',
        '--split-mode', 'layer', '--tensor-split', '1,1',
        '--cache-type-k', 'q5_1', '--cache-type-v', 'q5_1',
        '--flash-attn', 'on', '--batch-size', '1024', '--ubatch-size', '256',
        '--temp', '0.6', '--top-p', '0.95', '--top-k', '20', '--min-p', '0.0',
        '--repeat-penalty', '1.0', '--presence-penalty', '0.0', '--seed', '42',
        '--reasoning', 'off', '--jinja', '--chat-template-file', $template,
        '--metrics', '--no-warmup', '--no-context-shift', '--predict', "$MaxTokens",
        '--fit', 'on'
    ) + @($specArgs | Where-Object { $null -ne $_ -and $_ -ne '' })
    $process = Start-Process -FilePath $server -ArgumentList $args -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru -WindowStyle Hidden
    $rows = @()
    $metrics = ''
    try {
        Wait-Healthy $process
        Invoke-Chat 'Answer only: WARMUP.' | Out-Null
        foreach ($task in $tasks) {
            $call = Invoke-Chat $task.prompt
            $response = $call.response
            $message = $response.choices[0].message
            $content = (($message.content, $message.reasoning_content) -join "`n").Trim()
            $timings = $response.timings
            $row = [ordered]@{
                mode = $label
                task = $task.id
                ok = [regex]::IsMatch($content, $task.expected)
                wallSec = $call.wallSec
                promptTokens = [int]$timings.prompt_n
                promptMs = [math]::Round([double]$timings.prompt_ms, 1)
                predictedTokens = [int]$timings.predicted_n
                decodeMs = [math]::Round([double]$timings.predicted_ms, 1)
                decodeTps = [math]::Round([double]$timings.predicted_per_second, 2)
                content = $content
            }
            $rows += $row
            Write-Host ("{0} {1}: ok={2}, ttftMs={3}, decodeTps={4}, tokens={5}" -f $label, $task.id, $row.ok, $row.promptMs, $row.decodeTps, $row.predictedTokens)
        }
        try { $metrics = (Invoke-RestMethod "$base/metrics" -TimeoutSec 10 | Out-String) } catch { $metrics = "metrics unavailable: $($_.Exception.Message)" }
    } finally {
        if (!$process.HasExited) { Stop-Process -Id $process.Id -Force }
        $process.WaitForExit()
        Start-Sleep -Seconds 3
    }
    [ordered]@{ mode = $label; rows = $rows; metrics = $metrics; stdout = $stdout; stderr = $stderr }
}

$manifest = @(
    Get-ManifestEntry $server
    Get-ManifestEntry $model
    Get-ManifestEntry $draftQ4
    Get-ManifestEntry $mtpQ4
    Get-ManifestEntry $template
)

$runs = @()
foreach ($mode in $Modes) {
    $specArgs = switch ($mode) {
        'baseline-autoregressive' { @() }
        'ngram-mod' { @('--spec-type', 'ngram-mod', '--spec-ngram-mod-n-min', '4', '--spec-ngram-mod-n-max', '32', '--spec-ngram-mod-n-match', '24') }
        'dflash2-draft-q4' { @('--spec-type', 'draft-dflash', '--spec-draft-model', $draftQ4, '--spec-draft-n-max', '5', '--spec-draft-n-min', '1') }
        'mtp-q4' { @('--spec-type', 'draft-mtp', '--spec-draft-model', $mtpQ4, '--spec-draft-n-max', '4', '--spec-draft-n-min', '1') }
    }
    try {
        $runs += Run-Mode $mode $specArgs
    } catch {
        Write-Output ("{0}: FAILED: {1}" -f $mode, $_.Exception.Message)
        $runs += [ordered]@{ mode = $mode; status = 'failed'; error = $_.Exception.Message; rows = @() }
    }
}

$allRows = @($runs | ForEach-Object { $_.rows })
$summary = @($runs | Where-Object { $_.rows } | ForEach-Object {
    $group = @($_.rows)
    [ordered]@{
        mode = $group[0].mode
        quality = "{0}/{1}" -f @($group | Where-Object ok).Count, $group.Count
        medianPromptMs = [math]::Round(($group.promptMs | Sort-Object)[[int][math]::Floor($group.Count / 2)], 1)
        medianDecodeTps = [math]::Round(($group.decodeTps | Sort-Object)[[int][math]::Floor($group.Count / 2)], 2)
        medianWallSec = [math]::Round(($group.wallSec | Sort-Object)[[int][math]::Floor($group.Count / 2)], 3)
    }
})

$versionErrorAction = $ErrorActionPreference
try {
    $ErrorActionPreference = 'Continue'
    $serverVersion = [string]((& $server --version 2>&1 | Select-Object -First 1))
} finally {
    $ErrorActionPreference = $versionErrorAction
}
$result = [ordered]@{
    generatedAt = (Get-Date).ToString('o')
    server = $server
    serverVersion = $serverVersion
    contextSize = $ContextSize
    maxTokens = $MaxTokens
    hardware = '2x RTX 3090 24GB; split-mode layer; tensor-split 1,1'
    sampling = [ordered]@{ temperature = 0.6; topP = 0.95; topK = 20; minP = 0.0; repeatPenalty = 1.0; presencePenalty = 0.0; seed = 42 }
    manifest = $manifest
    omissions = @('QAT Q2 main GGUF not present locally', 'DFlash2 Q2_K_S-MIX draft not present locally', '131k stages are capacity smokes when max_tokens is intentionally capped; they are not quality verdicts')
    summary = $summary
    runs = $runs
}
$jsonPath = Join-Path $outputRoot 'result.json'
$result | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $jsonPath -Encoding utf8
Write-Output "Saved: $jsonPath"
$summary | ConvertTo-Json -Depth 8
