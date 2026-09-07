[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Server,
    [Parameter(Mandatory = $true)]
    [string]$Model,
    [string]$Mmproj = "",
    [string]$Template = "",
    [string]$Output = "",
    [int]$Passes = 2,
    [int]$Port = 18091,
    [int]$NMin = 3,
    [string[]]$FixedNMax = @("3"),
    [string[]]$AdaptiveNMax = @("5", "7", "8", "9"),
    [int]$Context = 32768,
    [int]$MaxTokens = 512,
    [string]$Reasoning = "off",
    [string[]]$ExtraServerArgs = @()
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Resolve-ExistingFile([string]$path, [string]$label) {
    if ([string]::IsNullOrWhiteSpace($path)) { return "" }
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "$label no existe: $path"
    }
    return (Resolve-Path -LiteralPath $path).Path
}

function Get-Median([object[]]$values) {
    $numbers = @($values | ForEach-Object { [double]$_ } | Sort-Object)
    if ($numbers.Count -eq 0) { return 0.0 }
    $middle = [int][math]::Floor($numbers.Count / 2)
    if (($numbers.Count % 2) -eq 1) { return $numbers[$middle] }
    return ($numbers[$middle - 1] + $numbers[$middle]) / 2.0
}

function Get-TimingNumber($timings, [string]$name) {
    if ($null -eq $timings) { return 0.0 }
    $property = $timings.PSObject.Properties[$name]
    if ($null -eq $property -or $null -eq $property.Value) { return 0.0 }
    try { return [double]$property.Value } catch { return 0.0 }
}

function Get-PropertyValue($object, [string]$name) {
    if ($null -eq $object) { return $null }
    if ($object -is [System.Collections.IDictionary] -and $object.Contains($name)) {
        return $object[$name]
    }
    $property = $object.PSObject.Properties[$name]
    if ($null -eq $property) { return $null }
    return $property.Value
}

function Get-NumericSum([object[]]$values) {
    $sum = 0.0
    foreach ($value in @($values)) {
        if ($null -eq $value) { continue }
        try { $sum += [double]$value } catch {}
    }
    return $sum
}

function Convert-ToIntList([object[]]$values, [string]$label) {
    $result = @()
    foreach ($value in @($values)) {
        foreach ($part in ([string]$value -split '[,;\s]+')) {
            if ([string]::IsNullOrWhiteSpace($part)) { continue }
            try {
                $result += [int]$part
            } catch {
                throw "$label contiene un valor inválido: $part"
            }
        }
    }
    return ,$result
}

function Get-ResponseAnswer($response) {
    if ($null -eq $response) { return "" }
    $choices = @(Get-PropertyValue $response "choices")
    if ($choices.Count -eq 0) { return "" }
    $message = Get-PropertyValue $choices[0] "message"
    if ($null -eq $message) { return "" }
    $parts = @()
    $content = Get-PropertyValue $message "content"
    $reasoningContent = Get-PropertyValue $message "reasoning_content"
    if ($null -ne $content) { $parts += [string]$content }
    if ($null -ne $reasoningContent) { $parts += [string]$reasoningContent }
    return ($parts -join "`n").Trim()
}

function Wait-Healthy([int]$targetPort, $process, [string]$logBase) {
    for ($i = 0; $i -lt 120; ++$i) {
        if ($process.HasExited) {
            throw "llama-server terminó antes de estar sano. Logs: $logBase.out.log / $logBase.err.log"
        }
        try {
            $health = Invoke-RestMethod "http://127.0.0.1:$targetPort/health" -TimeoutSec 2
            if ($health.status -eq "ok" -or $health.status -eq "ready") { return }
        } catch {}
        Start-Sleep -Seconds 1
    }
    throw "llama-server no llegó a healthy en el puerto $targetPort. Logs: $logBase.out.log / $logBase.err.log"
}

$serverPath = Resolve-ExistingFile $Server "Server"
$modelPath = Resolve-ExistingFile $Model "Model"
$mmprojPath = Resolve-ExistingFile $Mmproj "Mmproj"
$templatePath = Resolve-ExistingFile $Template "Template"

if ($Passes -lt 1) { throw "Passes debe ser >= 1" }
if ($Port -lt 1024 -or $Port -gt 65535) { throw "Port debe estar entre 1024 y 65535" }
if ($NMin -lt 1) { throw "NMin debe ser >= 1" }

$fixedNMaxValues = Convert-ToIntList $FixedNMax "FixedNMax"
$adaptiveNMaxValues = Convert-ToIntList $AdaptiveNMax "AdaptiveNMax"

foreach ($n in @($adaptiveNMaxValues)) {
    if ($n -lt $NMin) { throw "AdaptiveNMax=$n es menor que NMin=$NMin" }
}

# Evita iniciar una prueba que el binario no puede ejecutar. Una build oficial
# puede aceptar draft-mtp pero no conoce adaptive; el error queda explícito.
$helpText = (& $serverPath --help 2>&1 | Out-String)
if ($helpText -notmatch "--spec-draft-adaptive") {
    throw "El binario no declara --spec-draft-adaptive en --help. Registrá una build mtp-fork compatible."
}
if ($helpText -notmatch "--spec-draft-n-min") {
    throw "El binario no declara --spec-draft-n-min en --help. No se puede medir adaptive."
}

$tasks = @(
    [ordered]@{
        id = "structured-json"
        expected = '(?im)^FINAL\s*:\s*\{"language"\s*:\s*"python"\s*,\s*"items"\s*:\s*\[\s*1\s*,\s*4\s*,\s*9\s*\]\}\s*$'
        prompt = 'Respondé únicamente con esta línea exacta, sin markdown ni texto adicional: FINAL: {"language":"python","items":[1,4,9]}'
    },
    [ordered]@{
        id = "coding"
        expected = '(?im)FINAL\s*:\s*implemented'
        prompt = "Escribí una función thread-safe de token bucket en C++17 en menos de 40 líneas. Explicá brevemente la decisión y terminá exactamente con una línea FINAL: implemented."
    },
    [ordered]@{
        id = "reasoning"
        expected = '(?im)FINAL\s*:\s*100/101'
        prompt = "Calculá la suma de k=1..100 de 1/(k(k+1)) en no más de tres líneas y terminá exactamente con FINAL: 100/101."
    },
    [ordered]@{
        id = "high-entropy-prose"
        expected = '(?im)FINAL\s*:\s*NO'
        prompt = "Todas las aves son animales. Algunos animales no vuelan. ¿Se deduce que algunas aves no vuelan? Explicá brevemente y terminá exactamente con FINAL: NO."
    }
)

$configs = @()
foreach ($n in @($fixedNMaxValues)) {
    if ($n -lt 1) { throw "FixedNMax debe contener valores >= 1" }
    $configs += [ordered]@{ id = "fixed-$n"; mode = "fixed"; nMin = 0; nMax = $n }
}
foreach ($n in @($adaptiveNMaxValues)) {
    $configs += [ordered]@{ id = "adaptive-$NMin-$n"; mode = "adaptive"; nMin = $NMin; nMax = $n }
}

$allRuns = @()
foreach ($config in $configs) {
    $logBase = Join-Path ([IO.Path]::GetTempPath()) ("llamacode-adaptive-$PID-" + [guid]::NewGuid().ToString("N"))
    $args = @(
        "-m", $modelPath, "--host", "127.0.0.1", "--port", "$Port",
        "--ctx-size", "$Context", "--parallel", "1", "--n-gpu-layers", "999",
        "--cache-type-k", "q8_0", "--cache-type-v", "q8_0",
        "--flash-attn", "on", "--batch-size", "2048", "--ubatch-size", "512",
        "--spec-type", "draft-mtp", "--spec-draft-n-max", "$($config.nMax)",
        "--temp", "0.6", "--top-p", "0.95", "--top-k", "20", "--min-p", "0.0",
        "--repeat-penalty", "1.0", "--presence-penalty", "0.0", "--jinja", "--metrics"
    )
    if ($config.mode -eq "adaptive") {
        $args += @("--spec-draft-adaptive", "--spec-draft-n-min", "$($config.nMin)")
    }
    if (-not [string]::IsNullOrWhiteSpace($Reasoning)) {
        $args += @("--reasoning", $Reasoning)
    }
    if ($mmprojPath.Length -gt 0) { $args += @("--mmproj", $mmprojPath) }
    if ($templatePath.Length -gt 0) { $args += @("--chat-template-file", $templatePath) }
    if ($ExtraServerArgs.Count -gt 0) { $args += $ExtraServerArgs }

    $serverProcess = $null
    try {
        $serverProcess = Start-Process -FilePath $serverPath -ArgumentList $args -WorkingDirectory (Split-Path $serverPath) `
            -RedirectStandardOutput ($logBase + ".out.log") -RedirectStandardError ($logBase + ".err.log") `
            -PassThru -WindowStyle Hidden
        Wait-Healthy $Port $serverProcess $logBase

        # El calentamiento queda fuera de las muestras para no mezclar carga inicial.
        $warmupBody = @{ messages = @(@{ role = "user"; content = "Answer only: warmup." }); max_tokens = 32; temperature = 0.6; stream = $false } | ConvertTo-Json -Depth 8
        Invoke-RestMethod "http://127.0.0.1:$Port/v1/chat/completions" -Method Post `
            -ContentType "application/json" -Body $warmupBody -TimeoutSec 180 | Out-Null

        for ($pass = 1; $pass -le $Passes; ++$pass) {
            foreach ($task in $tasks) {
                $requestBody = @{
                    messages = @(@{ role = "user"; content = $task.prompt })
                    max_tokens = $MaxTokens
                    temperature = 0.6
                    top_p = 0.95
                    top_k = 20
                    stream = $false
                    cache_prompt = $false
                } | ConvertTo-Json -Depth 8
                $clock = [Diagnostics.Stopwatch]::StartNew()
                try {
                    $response = Invoke-RestMethod "http://127.0.0.1:$Port/v1/chat/completions" -Method Post `
                        -ContentType "application/json" -Body $requestBody -TimeoutSec 300
                    $clock.Stop()
                    $answer = Get-ResponseAnswer $response
                    $timings = Get-PropertyValue $response "timings"
                    $predictedN = [int](Get-TimingNumber $timings "predicted_n")
                    $predictedTps = Get-TimingNumber $timings "predicted_per_second"
                    if ($predictedTps -le 0 -and $clock.Elapsed.TotalSeconds -gt 0) {
                        $predictedTps = $predictedN / $clock.Elapsed.TotalSeconds
                    }
                    $allRuns += [ordered]@{
                        configId = $config.id; mode = $config.mode; nMin = $config.nMin; nMax = $config.nMax
                        pass = $pass; task = $task.id; qualityOk = [regex]::IsMatch($answer, $task.expected)
                        wallSec = [math]::Round($clock.Elapsed.TotalSeconds, 3)
                        promptMs = [math]::Round((Get-TimingNumber $timings "prompt_ms"), 1)
                        decodeTps = [math]::Round($predictedTps, 2); predictedN = $predictedN
                        draftN = [int](Get-TimingNumber $timings "draft_n")
                        draftAccepted = [int](Get-TimingNumber $timings "draft_n_accepted")
                        answer = $answer; error = ""
                    }
                } catch {
                    $clock.Stop()
                    $allRuns += [ordered]@{
                        configId = $config.id; mode = $config.mode; nMin = $config.nMin; nMax = $config.nMax
                        pass = $pass; task = $task.id; qualityOk = $false
                        wallSec = [math]::Round($clock.Elapsed.TotalSeconds, 3); promptMs = 0
                        decodeTps = 0; predictedN = 0; draftN = 0; draftAccepted = 0
                        answer = ""; error = $_.Exception.Message
                    }
                }
            }
        }
    } finally {
        if ($null -ne $serverProcess) {
            if (!$serverProcess.HasExited) { Stop-Process -Id $serverProcess.Id -Force }
            $serverProcess.WaitForExit()
        }
    }
}

$summary = @()
foreach ($group in @($allRuns | Where-Object { (Get-PropertyValue $_ "error") -eq "" } |
        Group-Object { Get-PropertyValue $_ "configId" })) {
    $rows = @($group.Group)
    $accepted = @($rows | Where-Object { [double](Get-PropertyValue $_ "draftN") -gt 0 })
    $draftTotal = Get-NumericSum @($accepted | ForEach-Object { Get-PropertyValue $_ "draftN" })
    $draftAcceptedTotal = Get-NumericSum @($accepted | ForEach-Object { Get-PropertyValue $_ "draftAccepted" })
    $summary += [ordered]@{
        configId = Get-PropertyValue $rows[0] "configId"; mode = Get-PropertyValue $rows[0] "mode"
        nMin = Get-PropertyValue $rows[0] "nMin"; nMax = Get-PropertyValue $rows[0] "nMax"
        quality = "{0}/{1}" -f @($rows | Where-Object { [bool](Get-PropertyValue $_ "qualityOk") }).Count, $rows.Count
        medianDecodeTps = [math]::Round((Get-Median @($rows | ForEach-Object { Get-PropertyValue $_ "decodeTps" })), 2)
        medianPromptMs = [math]::Round((Get-Median @($rows | ForEach-Object { Get-PropertyValue $_ "promptMs" })), 1)
        medianWallSec = [math]::Round((Get-Median @($rows | ForEach-Object { Get-PropertyValue $_ "wallSec" })), 3)
        draftAcceptance = if ($draftTotal -gt 0) { [math]::Round($draftAcceptedTotal / $draftTotal, 4) } else { $null }
    }
}

$result = [ordered]@{
    generatedAt = (Get-Date).ToString("o")
    server = $serverPath; model = $modelPath; mmproj = $mmprojPath; template = $templatePath
    passes = $Passes; port = $Port; context = $Context; maxTokens = $MaxTokens; tasks = @($tasks | ForEach-Object { $_.id })
    configs = $configs; summary = $summary; runs = $allRuns
}

if (-not [string]::IsNullOrWhiteSpace($Output)) {
    $outputParent = Split-Path -Parent $Output
    if (-not [string]::IsNullOrWhiteSpace($outputParent) -and !(Test-Path -LiteralPath $outputParent)) {
        New-Item -ItemType Directory -Path $outputParent -Force | Out-Null
    }
    $result | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $Output -Encoding utf8
}

$summary | Format-Table -AutoSize
if (-not [string]::IsNullOrWhiteSpace($Output)) { Write-Output "Saved: $Output" }
