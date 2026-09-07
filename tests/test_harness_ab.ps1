<#
  test_harness_ab.ps1 - regresion de tools\harness_ab.ps1 (barrido A/B de harness).

  No corre en ctest (es infra PS, no C++). Correr a mano:
      powershell -NoProfile -ExecutionPolicy Bypass -File tests\test_harness_ab.ps1
  Exit 0 = todo verde.

  El script real habla con el ControlApi de un daemon headless. Aca levantamos un
  STUB con HttpListener que responde /health, /prop y /invoke, y registra que se
  invoco. Asi se puede verificar el contrato completo sin modelo ni GPU:
  que valide antes de gastar una corrida, que use el verbo correcto y que espere
  a que termine cada benchmark antes de arrancar el siguiente.
#>
# 'Continue' a proposito: este test INVOCA al script real y verifica su salida y
# su exit code. Con 'Stop', el stderr de una corrida que falla a proposito (test 1)
# se convierte en excepcion y aborta el test en vez de dejarlo verificar el mensaje.
$ErrorActionPreference = 'Continue'
$root   = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$script = Join-Path $root 'tools\harness_ab.ps1'
$fails  = 0
function Ok($cond, $msg) {
    if ($cond) { Write-Host "  PASS $msg" }
    else { Write-Host "  FAIL $msg" -Foreground Red; $script:fails++ }
}

Write-Host "== test 0: los scripts de infra son ASCII puro =="
# Windows PowerShell 5.1 lee un .ps1 sin BOM como ANSI: un caracter UTF-8 dentro
# de un STRING se decodifica mal y puede terminarlo (ver CLAUDE.md). En un script
# que alguien corre desde cualquier host, eso es un ParserError silencioso.
foreach ($f in @('tools\harness_ab.ps1', 'tests\test_harness_ab.ps1')) {
    $txt = [IO.File]::ReadAllText((Join-Path $root $f))
    $bad = [regex]::Matches($txt, '[^\x00-\x7F]')
    $sample = if ($bad.Count) { " (primero: '" + $bad[0].Value + "')" } else { '' }
    Ok ($bad.Count -eq 0) "$f sin caracteres no-ASCII$sample"
}

# --- Stub del ControlApi ---------------------------------------------------
# Corre en un runspace aparte; deja el registro de llamadas en un JSONL para que
# el test lo lea despues (compartir objetos entre runspaces es mas fragil que un
# archivo append-only).
function Get-FreePort {
    $l = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $l.Start(); $port = $l.LocalEndpoint.Port; $l.Stop(); return $port
}

$stubScript = {
    param($port, $logPath, $profilesJson, $stopAfterPolls)
    $known = ($profilesJson | ConvertFrom-Json)
    $listener = [Net.HttpListener]::new()
    $listener.Prefixes.Add("http://127.0.0.1:$port/")
    $listener.Start()
    # benchmarkRunning: true en el primer poll de cada corrida y false despues,
    # para que el script tenga que esperar de verdad al menos una vuelta.
    $running = $false
    $polls = 0
    $startCounts = @{}
    while ($listener.IsListening) {
        $ctx = $listener.GetContext()
        $req = $ctx.Request
        $path = $req.Url.AbsolutePath
        $body = ''
        if ($req.HasEntityBody) {
            $reader = [IO.StreamReader]::new($req.InputStream, $req.ContentEncoding)
            $body = $reader.ReadToEnd(); $reader.Close()
        }
        $resp = '{"ok":true}'
        if ($path -eq '/health') {
            $resp = '{"ok":true}'
        } elseif ($path -eq '/prop') {
            $name = $req.QueryString['name']
            if ($name -eq 'benchmarkRunning') {
                $polls++
                if ($polls -ge $stopAfterPolls) { $running = $false }
                $resp = '{"value":' + $running.ToString().ToLower() + '}'
            } else {
                $resp = '{"value":0}'
            }
        } elseif ($path -eq '/invoke') {
            $call = $body | ConvertFrom-Json
            $entry = @{ target = $req.QueryString['target']; method = $call.method
                        args = $call.args } | ConvertTo-Json -Depth 8 -Compress
            Add-Content -Path $logPath -Value $entry
            switch ($call.method) {
                'agentProfileSpec' {
                    $id = $call.args[0]
                    if ($known -contains $id) { $resp = '{"ok":true,"result":{"loop":{}}}' }
                    else { $resp = '{"ok":false,"error":"no existe"}' }
                }
                'harnessSpecSummary' {
                    $resp = '{"ok":true,"result":{"toolCount":7,"approxTokens":640,"warnings":[]}}'
                }
                'startBenchmark' {
                    $id = $call.args[5]
                    if (-not $startCounts.ContainsKey($id)) { $startCounts[$id] = 0 }
                    $startCounts[$id]++
                    $running = $true; $polls = 0; $resp = '{"ok":true}'
                }
                'startCustomBenchmark' {
                    $id = $call.args[5]
                    if (-not $startCounts.ContainsKey($id)) { $startCounts[$id] = 0 }
                    $startCounts[$id]++
                    $running = $true; $polls = 0; $resp = '{"ok":true}'
                }
                'compareHarnessBenchmarks' {
                    $runCount = 0
                    if ($startCounts.Count -gt 0) {
                        $runCount = [int](($startCounts.Values | Measure-Object -Maximum).Maximum)
                    }
                    $rows = @()
                    foreach ($id in $known) {
                        $rows += ('{"profileId":"' + $id + '","profileName":"' + $id +
                                  '","medianQualityPct":90.0,"successRatePct":100.0,' +
                                  '"medianElapsedSec":100.0,"medianFilesChanged":2.0,"runs":' +
                                  $runCount + '}')
                    }
                    $comparisons = '[]'
                    if ($known.Count -ge 2) {
                        $comparisons = '[{"baselineProfileId":"' + $known[0] +
                            '","candidateProfileId":"' + $known[1] +
                            '","qualityDeltaPctPoints":0.0,"successRateDeltaPctPoints":0.0,' +
                            '"comparisonTimeChangePct":0.0,"filesChangedDelta":0.0}]'
                    }
                    $resp = '{"ok":true,"result":{"balanced":true,"profiles":[' +
                            ($rows -join ',') + '],"comparisons":' + $comparisons + '}}'
                }
                default { $resp = '{"ok":true}' }
            }
        } else {
            $ctx.Response.StatusCode = 404
        }
        $bytes = [Text.Encoding]::UTF8.GetBytes($resp)
        $ctx.Response.ContentType = 'application/json'
        $ctx.Response.ContentLength64 = $bytes.Length
        $ctx.Response.OutputStream.Write($bytes, 0, $bytes.Length)
        $ctx.Response.OutputStream.Close()
        if ($path -eq '/shutdown') { break }
    }
    $listener.Stop()
}

function Start-Stub([string]$logPath, [string[]]$profiles, [int]$stopAfterPolls = 1) {
    $port = Get-FreePort
    if (Test-Path $logPath) { Remove-Item $logPath -Force }
    New-Item -ItemType File -Path $logPath -Force | Out-Null
    $ps = [PowerShell]::Create()
    $ps.AddScript($stubScript).
        AddArgument($port).AddArgument($logPath).
        AddArgument(($profiles | ConvertTo-Json -Compress)).
        AddArgument($stopAfterPolls) | Out-Null
    $handle = $ps.BeginInvoke()
    # Esperar a que el listener este arriba antes de devolver el control.
    for ($i = 0; $i -lt 50; $i++) {
        try { Invoke-RestMethod "http://127.0.0.1:$port/health" -TimeoutSec 1 | Out-Null; break }
        catch { Start-Sleep -Milliseconds 100 }
    }
    return [pscustomobject]@{ Port = $port; Ps = $ps; Handle = $handle; Log = $logPath }
}
function Stop-Stub($stub) {
    try { Invoke-RestMethod "http://127.0.0.1:$($stub.Port)/shutdown" -TimeoutSec 2 | Out-Null } catch {}
    try { $stub.Ps.Stop() } catch {}
    try { $stub.Ps.Dispose() } catch {}
}
function Calls($stub) {
    if (-not (Test-Path $stub.Log)) { return @() }
    return @(Get-Content $stub.Log | Where-Object { $_.Trim().Length -gt 0 } |
             ForEach-Object { $_ | ConvertFrom-Json })
}
function RunAb($stub, [string[]]$agents, [hashtable]$extra = @{}) {
    $out = Join-Path $env:TEMP ("harness-ab-test-" + [guid]::NewGuid().ToString('N') + ".json")
    $args = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $script,
              '-LaunchProfileId', 'launch-1',
              '-AgentProfileIds', ($agents -join ','),
              '-Port', $stub.Port, '-PollSeconds', '1', '-MaxWaitMinutes', '1',
              '-Out', $out)
    foreach ($k in $extra.Keys) { $args += @("-$k", $extra[$k]) }
    $text = (& powershell @args 2>&1) | Out-String
    return [pscustomobject]@{ Output = $text; ExitCode = $LASTEXITCODE; OutFile = $out }
}

$logDir = Join-Path $env:TEMP 'harness-ab-tests'
New-Item -ItemType Directory -Force $logDir | Out-Null

Write-Host "== test 1: sin daemon falla temprano y con mensaje claro =="
# Puerto libre = nadie escuchando. No debe arrancar ningun benchmark.
$deadPort = Get-FreePort
$r = & powershell -NoProfile -ExecutionPolicy Bypass -File $script `
        -LaunchProfileId 'launch-1' -AgentProfileIds 'a,b' -Port $deadPort 2>&1 | Out-String
Ok ($r -match 'daemon') "avisa que no hay daemon: $($r.Trim().Split([char]10)[-1])"

Write-Host "== test 2: un perfil inexistente corta ANTES de correr nada =="
$stub = Start-Stub (Join-Path $logDir 'calls2.jsonl') @('agent-intermedio')
try {
    $r = RunAb $stub @('agent-intermedio', 'no-existe')
    $calls = Calls $stub
    Ok ($r.Output -match 'inexistente|no existe') "avisa el perfil invalido"
    Ok (($calls | Where-Object { $_.method -like 'start*Benchmark' }).Count -eq 0) `
       "no arranco ningun benchmark"
} finally { Stop-Stub $stub }

Write-Host "== test 3: una corrida por perfil, esperando entre corridas =="
$stub = Start-Stub (Join-Path $logDir 'calls3.jsonl') @('agent-intermedio', 'agent-minimal') 2
try {
    $r = RunAb $stub @('agent-intermedio', 'agent-minimal') @{ Passes = 3; OrderSeed = 11 }
    $calls = Calls $stub
    $starts = @($calls | Where-Object { $_.method -eq 'startBenchmark' })
    Ok ($starts.Count -eq 6) "arranco 6 benchmarks (3 pasadas por perfil): $($starts.Count)"
    $perPass = @()
    for ($pass = 0; $pass -lt 3; $pass++) {
        $perPass += ,@($starts[($pass * 2)..($pass * 2 + 1)] | ForEach-Object { $_.args[5] })
    }
    Ok ((@($perPass[0]).Count -eq 2) -and (@($perPass[1]).Count -eq 2) -and
        (@($perPass[2]).Count -eq 2)) "cada pasada contiene todos los perfiles"
    Ok ((@($perPass[0]) -join ',') -ne (@($perPass[1]) -join ',') -or
        (@($perPass[1]) -join ',') -ne (@($perPass[2]) -join ',')) "el orden se intercala por pasada"
    Ok ($starts[0].args[3] -eq 'agent') "corre con target=agent (no 'model')"
    Ok ($starts[0].args[2] -eq 1) "cada llamada solicita una sola pasada balanceada"
    $polls = @($calls | Where-Object { $_.method -eq 'compareHarnessBenchmarks' })
    Ok ($polls.Count -eq 1) "compara una sola vez, al final"
    # El filtro temporal es lo que evita mezclar el historial del usuario con el
    # barrido: sin el, un perfil con corridas viejas gana por acumulacion.
    Ok ($polls[0].args.Count -ge 3 -and [double]$polls[0].args[2] -gt 0) `
       "acota la comparacion a las corridas de este barrido (sinceEpochMs)"
    Ok ($r.ExitCode -eq 0) "exit 0"
    Ok (Test-Path $r.OutFile) "escribio el informe JSON"
    if (Test-Path $r.OutFile) {
        $report = Get-Content $r.OutFile -Raw | ConvertFrom-Json
        Ok ($report.profiles.Count -eq 2) "el informe trae los dos perfiles"
        Ok ($report.comparisons.Count -eq 1) "y el delta entre ellos"
        Ok ($report.passesRequested -eq 3) "persiste la cantidad de pasadas solicitada"
        Ok ($report.orderSeed -eq 11) "persiste la semilla del orden"
        Ok (@($report.profileOrderByPass).Count -eq 3) "persiste el orden de cada pasada"
        Remove-Item $r.OutFile -Force -ErrorAction SilentlyContinue
    }
    Ok ($r.Output -match 'calidad') "imprime el resumen legible por perfil"
    Ok ($r.Output -match 'tools: F1') "imprime los deltas de calidad de tools"
    Ok ($r.Output -match 'NO baja calidad') "recuerda el criterio de lectura"
} finally { Stop-Stub $stub }

Write-Host "== test 4: -CustomBenchmarkId usa startCustomBenchmark =="
$stub = Start-Stub (Join-Path $logDir 'calls4.jsonl') @('agent-intermedio') 2
try {
    $r = RunAb $stub @('agent-intermedio') @{ CustomBenchmarkId = 'cb-1' }
    $calls = Calls $stub
    $custom = @($calls | Where-Object { $_.method -eq 'startCustomBenchmark' })
    $plain  = @($calls | Where-Object { $_.method -eq 'startBenchmark' })
    Ok ($custom.Count -eq 5) "uso startCustomBenchmark en las 5 pasadas por defecto"
    Ok ($plain.Count -eq 0)  "y NO el generico"
    Ok ($custom[0].args[1] -eq 'cb-1') "paso el id del benchmark custom"
    Ok ($custom[0].args[2] -eq 1) "el custom tambien queda balanceado a una pasada por llamada"
} finally { Stop-Stub $stub }

Write-Host ""
Remove-Item -Recurse -Force $logDir -ErrorAction SilentlyContinue
if ($fails -eq 0) { Write-Host "=== ALL HARNESS-AB TESTS PASSED ===" -Foreground Green; exit 0 }
else { Write-Host "=== $fails HARNESS-AB TEST(S) FAILED ===" -Foreground Red; exit 1 }
