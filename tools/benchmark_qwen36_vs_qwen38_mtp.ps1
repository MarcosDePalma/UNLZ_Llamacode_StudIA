param(
    [int]$Passes = 2,
    [int]$Port = 18089,
    [string]$Output = ""
)

$ErrorActionPreference = "Stop"
$server = "D:\Models\llamacpp\llama.cpp-b10331-cuda12.4\llama-server.exe"
$qwen38 = "D:\Models\llamacpp\Qwen3.8-27B-GGUF-Q4_K_M\Qwen3.8-27B-Q4_K_M.gguf"
$qwen38Mmproj = "D:\Models\llamacpp\Qwen3.8-27B-GGUF-Q4_K_M\mmproj-BF16.gguf"
$qwen38Template = "C:\Users\cristian\AppData\Local\LlamaCode\LlamaCode\chat-templates\qwen38-tools-fixed.jinja"
$qwen36 = "D:\Models\llamacpp\ThinkingCap-Qwen3.6-27B-GGUF\ThinkingCap-Qwen3.6-27B-Q4_K_M.gguf"
$qwen36Mmproj = "D:\Models\llamacpp\ThinkingCap-Qwen3.6-27B-GGUF\mmproj-ThinkingCap-Qwen3.6-27B-f16.gguf"

$tasks = @(
    @{ id="last-digit"; expected='(?im)^FINAL:\s*3\s*$'; prompt="Compute the last digit of 7^(7^7), with right associativity. Explain briefly and end with exactly FINAL: <integer>." },
    @{ id="mississippi"; expected='(?im)^FINAL:\s*34650\s*$'; prompt="How many distinct permutations does MISSISSIPPI have? Explain briefly and end with exactly FINAL: <integer>." },
    @{ id="telescoping"; expected='(?im)^FINAL:\s*100/101\s*$'; prompt="Compute sum from k=1 to 100 of 1/(k(k+1)). Explain briefly and end with exactly FINAL: <reduced fraction>." },
    @{ id="modular"; expected='(?im)^FINAL:\s*1\s*$'; prompt="Compute 2^100 mod 125. Explain briefly and end with exactly FINAL: <integer>." },
    @{ id="logic"; expected='(?im)^FINAL:\s*NO\s*$'; prompt="All ravens are birds. Some birds cannot fly. Does it logically follow that some ravens cannot fly? Explain briefly and end with FINAL: YES or FINAL: NO." },
    @{ id="python-output"; expected='(?im)^FINAL:\s*\[1,4,9\]\s*$'; prompt="What does Python print for [x*x for x in range(1,4)]? Explain briefly and end with exactly FINAL: followed by compact JSON only." }
)

function Wait-Healthy([int]$targetPort) {
    for ($i = 0; $i -lt 90; $i++) {
        try {
            $health = Invoke-RestMethod "http://127.0.0.1:$targetPort/health" -TimeoutSec 2
            if ($health.status -eq "ok") { return }
        } catch {}
        Start-Sleep -Seconds 2
    }
    throw "llama-server did not become healthy on port $targetPort"
}

function Run-Model($label, $model, $mmproj, $template) {
    $rows = @()
    for ($mtp = 2; $mtp -le 8; $mtp++) {
        $logBase = Join-Path ([IO.Path]::GetTempPath()) ("llamacode-mtp-sweep-" + $label + "-" + $mtp)
        $args = @(
            "-m", $model, "--host", "127.0.0.1", "--port", "$Port",
            "--ctx-size", "65536", "--parallel", "1", "--n-gpu-layers", "999",
            "--split-mode", "layer", "--tensor-split", "1,1", "--cache-type-k", "q8_0", "--cache-type-v", "q8_0",
            "--flash-attn", "on", "--batch-size", "2048", "--ubatch-size", "512",
            "--spec-type", "draft-mtp", "--spec-draft-n-max", "$mtp",
            "--temp", "0.6", "--top-p", "0.95", "--top-k", "20", "--min-p", "0.0",
            "--repeat-penalty", "1.0", "--presence-penalty", "0.0", "--reasoning", "off", "--jinja", "--metrics",
            "--no-warmup", "--no-context-shift", "--predict", "512"
        )
        if ($mmproj) { $args += @("--mmproj", $mmproj) }
        if ($template) { $args += @("--chat-template-file", $template) }
        $process = Start-Process -FilePath $server -ArgumentList $args -RedirectStandardOutput ($logBase + ".out.log") -RedirectStandardError ($logBase + ".err.log") -PassThru -WindowStyle Hidden
        try {
            Wait-Healthy $Port
            # Warm-up request is intentionally excluded from the measurements.
            $warm = @{ messages=@(@{role="user";content="Answer only: warmup."}); max_tokens=32; temperature=0.6; stream=$false } | ConvertTo-Json -Depth 8
            Invoke-RestMethod "http://127.0.0.1:$Port/v1/chat/completions" -Method Post -ContentType "application/json" -Body $warm -TimeoutSec 180 | Out-Null
            for ($pass = 1; $pass -le $Passes; $pass++) {
                foreach ($task in $tasks) {
                    $body = @{ messages=@(@{role="user";content=$task.prompt}); max_tokens=256; temperature=0.6; top_p=0.95; stream=$false; cache_prompt=$false } | ConvertTo-Json -Depth 8
                    $clock = [Diagnostics.Stopwatch]::StartNew()
                    $response = Invoke-RestMethod "http://127.0.0.1:$Port/v1/chat/completions" -Method Post -ContentType "application/json" -Body $body -TimeoutSec 180
                    $clock.Stop()
                    $answer = (($response.choices[0].message.content, $response.choices[0].message.reasoning_content) -join "`n").Trim()
                    $tim = $response.timings
                    $rows += [ordered]@{
                        model=$label; mtp=$mtp; pass=$pass; task=$task.id
                        ok=[regex]::IsMatch($answer, $task.expected)
                        wallSec=[math]::Round($clock.Elapsed.TotalSeconds,3)
                        ttftMs=[math]::Round([double]$tim.prompt_ms,1)
                        decodeTps=[math]::Round([double]$tim.predicted_per_second,2)
                        tokens=[int]$tim.predicted_n
                    }
                }
            }
        } finally {
            if (!$process.HasExited) { Stop-Process -Id $process.Id -Force }
            $process.WaitForExit()
        }
    }
    return $rows
}

foreach ($path in @($server,$qwen38,$qwen38Mmproj,$qwen38Template,$qwen36,$qwen36Mmproj)) {
    if (!(Test-Path -LiteralPath $path)) { throw "Missing required file: $path" }
}

$all = @(Run-Model "Qwen3.8-Q4_K_M" $qwen38 $qwen38Mmproj $qwen38Template) + @(Run-Model "Qwen3.6-ThinkingCap-Q4_K_M" $qwen36 $qwen36Mmproj "")
$summary = $all | Group-Object @{ e = { "$($_.model)|$($_.mtp)" } } | ForEach-Object {
    $r = $_.Group
    [ordered]@{
        model=$r[0].model; mtp=$r[0].mtp; quality=("{0}/{1}" -f @($r|? ok).Count,$r.Count)
        medianTps=[math]::Round(($r.decodeTps|Sort-Object)[[int][math]::Floor($r.Count/2)],2)
        medianTtftMs=[math]::Round(($r.ttftMs|Sort-Object)[[int][math]::Floor($r.Count/2)],1)
        medianWallSec=[math]::Round(($r.wallSec|Sort-Object)[[int][math]::Floor($r.Count/2)],3)
    }
}
$result=[ordered]@{ generatedAt=(Get-Date).ToString("o"); llamaCppBuild=10331; passes=$Passes; tasks=$tasks.id; summary=$summary; runs=$all }
if ($Output) { $result | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $Output -Encoding utf8 }
$summary | Format-Table -AutoSize
if ($Output) { Write-Output "Saved: $Output" }
