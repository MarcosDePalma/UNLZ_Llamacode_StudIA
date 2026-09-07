param(
    [int]$Passes = 3,
    [int]$Port = 18088,
    [string]$Output = ""
)

$ErrorActionPreference = "Stop"

$server = "D:\Models\llamacpp\llama.cpp-b10331-cuda12.4\llama-server.exe"
$profiles = @(
    [ordered]@{
        id = "thinkingcap-q4-mtp3"
        model = "D:\Models\llamacpp\ThinkingCap-Qwen3.6-27B-GGUF\ThinkingCap-Qwen3.6-27B-Q4_K_M.gguf"
        mmproj = "D:\Models\llamacpp\ThinkingCap-Qwen3.6-27B-GGUF\mmproj-ThinkingCap-Qwen3.6-27B-f16.gguf"
    },
    [ordered]@{
        id = "fablefusion-q6-mtp3"
        model = "D:\Models\llamacpp\Qwen3.6-27B-Fable-Fusion-Q6\Qwen3.6-27B-Fable-Fus-711-UnHeretic-NM-DAU-NEO-MAX-NEO-MTP-Q6_K.gguf"
        mmproj = "D:\Models\llamacpp\Qwen3.6-27B-Fable-Fusion-Q6\mmproj-F16.gguf"
    }
)

$tasks = @(
    @{ id="last-digit"; expected='(?im)^FINAL:\s*3\s*$'; prompt="Compute the last digit of 7^(7^7), with right associativity. End with exactly FINAL: <integer>." },
    @{ id="mississippi"; expected='(?im)^FINAL:\s*34650\s*$'; prompt="How many distinct permutations does MISSISSIPPI have? End with exactly FINAL: <integer>." },
    @{ id="telescoping"; expected='(?im)^FINAL:\s*100/101\s*$'; prompt="Compute sum from k=1 to 100 of 1/(k(k+1)). End with exactly FINAL: <reduced fraction>." },
    @{ id="modular"; expected='(?im)^FINAL:\s*1\s*$'; prompt="Compute 2^100 mod 125. End with exactly FINAL: <integer>." },
    @{ id="logic"; expected='(?im)^FINAL:\s*NO\s*$'; prompt="All ravens are birds. Some birds cannot fly. Does it logically follow that some ravens cannot fly? End with exactly FINAL: YES or FINAL: NO." },
    @{ id="intervals"; expected='(?im)^FINAL:\s*\[\[1,6\],\[8,12\]\]\s*$'; prompt="Merge the intervals [[1,3],[2,6],[8,10],[9,12]]. End with exactly FINAL: followed by compact JSON only." },
    @{ id="binary-search"; expected='(?im)^FINAL:\s*1\s*$'; prompt="For sorted list [1,2,2,2,5,9], what index does a correct lower_bound binary search return for target 2 using zero-based indexing? End with exactly FINAL: <integer>." },
    @{ id="parser-edge"; expected='(?im)^FINAL:\s*b=c\s*$'; prompt="A parser splits 'a=b=c' only on the first '=' and preserves '=' inside values. What value belongs to key a? End with exactly FINAL: <value>." },
    @{ id="strict-format"; expected='(?m)^FINAL:\s*alpha beta gamma$'; prompt="Ignore any desire to elaborate. End your response with exactly these three lowercase words and prefix: FINAL: alpha beta gamma" },
    @{ id="python-output"; expected='(?im)^FINAL:\s*\[1,4,9\]\s*$'; prompt="What does Python print for [x*x for x in range(1,4)]? End with exactly FINAL: followed by compact JSON only." }
)

function Wait-Healthy([int]$targetPort) {
    for ($i = 0; $i -lt 60; $i++) {
        try {
            $health = Invoke-RestMethod "http://127.0.0.1:$targetPort/health" -TimeoutSec 2
            if ($health.status -eq "ok") { return }
        } catch {}
        Start-Sleep -Seconds 2
    }
    throw "llama-server did not become healthy on port $targetPort"
}

function Run-Profile($profile) {
    $logRoot = Join-Path ([IO.Path]::GetTempPath()) ("llamacode-quality-" + $profile.id)
    $args = @(
        "-m", $profile.model,
        "--host", "127.0.0.1", "--port", "$Port",
        "--ctx-size", "32768", "--parallel", "1", "--n-gpu-layers", "999",
        "--split-mode", "layer", "--tensor-split", "1,1",
        "--cache-type-k", "q8_0", "--cache-type-v", "q8_0",
        "--flash-attn", "on", "--batch-size", "2048", "--ubatch-size", "512",
        "--spec-type", "draft-mtp", "--spec-draft-n-max", "3",
        "--temp", "0.6", "--top-p", "0.95", "--top-k", "20", "--min-p", "0.0",
        "--repeat-penalty", "1.0", "--presence-penalty", "0.0",
        "--reasoning", "off", "--jinja", "--load-mode", "mmap"
    )
    $process = Start-Process -FilePath $server -ArgumentList $args `
        -RedirectStandardOutput ($logRoot + ".out.log") `
        -RedirectStandardError ($logRoot + ".err.log") -PassThru -WindowStyle Hidden
    try {
        Wait-Healthy $Port
        $rows = @()
        for ($pass = 1; $pass -le $Passes; $pass++) {
            foreach ($task in $tasks) {
                $body = @{
                    messages = @(@{ role="user"; content=$task.prompt })
                    max_tokens = 512
                    temperature = 0.6
                    top_p = 0.95
                    stream = $false
                    cache_prompt = $false
                } | ConvertTo-Json -Depth 8
                $clock = [Diagnostics.Stopwatch]::StartNew()
                $response = Invoke-RestMethod "http://127.0.0.1:$Port/v1/chat/completions" `
                    -Method Post -ContentType "application/json" -Body $body -TimeoutSec 180
                $clock.Stop()
                $message = $response.choices[0].message
                $answer = (($message.content, $message.reasoning_content) -join "`n").Trim()
                $rows += [ordered]@{
                    pass = $pass
                    task = $task.id
                    ok = [regex]::IsMatch($answer, $task.expected)
                    wallSec = [math]::Round($clock.Elapsed.TotalSeconds, 3)
                    promptTps = [math]::Round([double]$response.timings.prompt_per_second, 3)
                    decodeTps = [math]::Round([double]$response.timings.predicted_per_second, 3)
                    tokens = [int]$response.timings.predicted_n
                    answer = $answer
                }
            }
        }
        return $rows
    } finally {
        if (!$process.HasExited) { Stop-Process -Id $process.Id -Force }
        $process.WaitForExit()
    }
}

foreach ($path in @($server) + ($profiles | ForEach-Object { $_.model; $_.mmproj })) {
    if (!(Test-Path -LiteralPath $path)) { throw "Missing required file: $path" }
}

$all = @()
foreach ($profile in $profiles) {
    $rows = Run-Profile $profile
    $all += [ordered]@{ profile=$profile.id; runs=$rows }
}

$summary = foreach ($entry in $all) {
    $runs = $entry.runs
    [ordered]@{
        profile = $entry.profile
        score = @($runs | Where-Object { $_.ok }).Count
        total = @($runs).Count
        medianDecodeTps = [math]::Round(($runs.decodeTps | Sort-Object)[[int][math]::Floor($runs.Count / 2)], 3)
        medianWallSec = [math]::Round(($runs.wallSec | Sort-Object)[[int][math]::Floor($runs.Count / 2)], 3)
    }
}

$result = [ordered]@{
    generatedAt = (Get-Date).ToString("o")
    llamaCppBuild = 10331
    passes = $Passes
    tasks = $tasks.id
    summary = $summary
    profiles = $all
}

$json = $result | ConvertTo-Json -Depth 12
if ($Output) { Set-Content -LiteralPath $Output -Value $json -Encoding utf8 }
$summary | Format-Table -AutoSize
if ($Output) { Write-Output "Saved: $Output" }
