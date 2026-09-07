$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$exe = Join-Path $root "build\Debug\LlamaCode.exe"
if (-not (Test-Path -LiteralPath $exe)) { throw "No existe $exe. Ejecutá build.bat Debug NOPAUSE." }
$qtRoot = "C:\Qt\6.8.3\msvc2022_64"
$env:QT_QPA_PLATFORM = "offscreen"
$env:QT_PLUGIN_PATH = Join-Path $qtRoot "plugins"
$env:PATH = (Join-Path $qtRoot "bin") + ";" + $env:PATH
$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$listener.Start(); $port = $listener.LocalEndpoint.Port; $listener.Stop()
$profileDir = Join-Path ([IO.Path]::GetTempPath()) ("llamacode-workflow-" + [guid]::NewGuid())
$oldPort = $env:LLAMACODE_CONTROL_PORT; $oldProfiles = $env:LLAMACODE_PROFILES_DIR; $proc = $null
function Remove-ProfileDir([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return }
    # Start-Process redirection can keep stdout/stderr handles alive for a
    # short moment after WaitForExit/Stop-Process. Cleanup must not turn a
    # passing HTTP smoke into a false failure on Windows.
    for ($attempt = 0; $attempt -lt 40; $attempt++) {
        try {
            Remove-Item -LiteralPath $path -Recurse -Force -ErrorAction Stop
            return
        } catch {
            if ($attempt -eq 39) {
                Write-Warning ("No pude limpiar el perfil temporal {0}: {1}" -f $path, $_.Exception.Message)
                return
            }
            Start-Sleep -Milliseconds 250
        }
    }
}
try {
    New-Item -ItemType Directory -Force -Path $profileDir | Out-Null
    $env:LLAMACODE_CONTROL_PORT = [string]$port; $env:LLAMACODE_PROFILES_DIR = $profileDir
    $stdoutPath = Join-Path $profileDir "daemon.stdout.log"
    $stderrPath = Join-Path $profileDir "daemon.stderr.log"
    $proc = Start-Process -FilePath $exe -ArgumentList "--headless" `
        -WorkingDirectory $root -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    $base = "http://127.0.0.1:$port"; $ready = $false
    # El primer arranque puede cargar catálogos y perfiles antes de abrir
    # ControlApi; el timeout debe cubrir una notebook fría sin modelo.
    for ($i = 0; $i -lt 120; $i++) {
        if ($proc.HasExited) {
            $tail = @()
            if (Test-Path $stdoutPath) { $tail += Get-Content $stdoutPath -Tail 12 }
            if (Test-Path $stderrPath) { $tail += Get-Content $stderrPath -Tail 12 }
            throw ("El daemon terminó con código {0}. Log: {1}" -f $proc.ExitCode, ($tail -join " | "))
        }
        try { if ((Invoke-RestMethod "$base/health").ok) { $ready = $true; break } }
        catch { Start-Sleep -Milliseconds 500 }
    }
    if (-not $ready) { throw "El daemon no respondió en $base" }
    function Invoke-Llama([string]$method, [object[]]$argumentList) {
        $body = @{ method = $method; args = @($argumentList) } | ConvertTo-Json -Depth 20 -Compress
        Invoke-RestMethod "$base/invoke" -Method Post -ContentType "application/json" -Body $body
    }
    $catalog = Invoke-Llama "engineeringWorkflows" @()
    if ($catalog.result.Count -ne 6) { throw "Se esperaban 6 workflows, llegaron $($catalog.result.Count)" }
    $qa = ($catalog.result | Where-Object { $_.id -eq "qa" })
    if ((Invoke-Llama "validateWorkflow" @($qa)).result -ne "") { throw "QA inválido" }
    $taskId = (Invoke-Llama "installEngineeringWorkflow" @("qa")).result
    if ([string]::IsNullOrWhiteSpace($taskId)) { throw "No se instaló qa" }
    $getBody = @{ target = "taskStore"; method = "get"; args = @($taskId) } |
        ConvertTo-Json -Depth 20 -Compress
    $saved = (Invoke-RestMethod "$base/invoke" -Method Post -ContentType "application/json" -Body $getBody).result
    if ($saved.workflow.id -ne "qa") { throw "La Task no conserva workflow qa" }
    if ([string]::IsNullOrWhiteSpace($saved.safetyProfile)) { throw "Falta safetyProfile" }
    $autoprompt = ($catalog.result | Where-Object { $_.id -eq "autoprompt" })
    if ((Invoke-Llama "validateWorkflow" @($autoprompt)).result -ne "") { throw "Autoprompt inválido" }
    if ($autoprompt.budget.maxRepairs -ne 3) { throw "Autoprompt sin presupuesto de reparaciones" }
    Write-Host "OK: catálogo, validación, instalación y persistencia headless"
} finally {
    if ($proc -and -not $proc.HasExited) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
    if ($proc) { $proc.WaitForExit(10000) | Out-Null }
    if ($null -eq $oldPort) { Remove-Item Env:LLAMACODE_CONTROL_PORT -ErrorAction SilentlyContinue } else { $env:LLAMACODE_CONTROL_PORT = $oldPort }
    if ($null -eq $oldProfiles) { Remove-Item Env:LLAMACODE_PROFILES_DIR -ErrorAction SilentlyContinue } else { $env:LLAMACODE_PROFILES_DIR = $oldProfiles }
    Remove-ProfileDir $profileDir
}
