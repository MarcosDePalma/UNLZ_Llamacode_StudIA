<#
  headless_harness_smoke.ps1 - smoke del harness modular contra el ControlApi REAL.

  El stub de tests\test_harness_ab.ps1 prueba el contrato del script; esto prueba
  que los verbos existan de verdad en el daemon. Es la diferencia entre "mi
  cliente habla bien" y "el servidor entiende": un rename en un Q_INVOKABLE
  pasaria el test con stub y romperia en produccion.

  NO corre un benchmark (eso necesita modelo y GPU): valida descubrimiento,
  spec, diff, resumen y el verbo de comparacion. La corrida completa sigue
  siendo manual, con tools\harness_ab.ps1.

  Correr a mano (fuera de ctest, necesita el exe compilado):
      powershell -NoProfile -ExecutionPolicy Bypass -File tests\headless_harness_smoke.ps1
#>
param(
    [int]$Port = 8879,
    [string]$Exe = ".\build\Release\LlamaCode.exe"
)

$ErrorActionPreference = "Continue"
$qtRoot = "C:\Qt\6.8.3\msvc2022_64"
$env:QT_QPA_PLATFORM = "offscreen"
$env:QT_PLUGIN_PATH = Join-Path $qtRoot "plugins"
$env:PATH = (Join-Path $qtRoot "bin") + ";" + $env:PATH
# Aislamiento COMPLETO: este smoke crea un perfil de agente y escribe directivas.
# LLAMACODE_PROFILES_DIR cubre los perfiles, pero las directivas cuelgan de
# QStandardPaths::AppLocalDataLocation, que en Windows sale de %LOCALAPPDATA% y
# ese env var NO lo toca. Sin redirigirlo, el test sembraba el ejemplo bundleado
# y creaba/borraba archivos en la instalacion real del usuario.
$sandbox    = Join-Path ([IO.Path]::GetTempPath()) ".harness-smoke-sandbox"
$profileDir = Join-Path $sandbox "profiles"
$env:LLAMACODE_CONTROL_PORT = "$Port"
$env:LLAMACODE_PROFILES_DIR = $profileDir
# Qt resuelve AppLocalData por la API de shell de Windows, NO por %LOCALAPPDATA%:
# la unica forma de redirigirlo desde afuera es el modo test (main.cpp lo activa
# con esta env var y llama a QStandardPaths::setTestModeEnabled).
$env:LLAMACODE_TEST_MODE = "1"
New-Item -ItemType Directory -Force $profileDir | Out-Null

$fails = 0
function Ok($cond, $msg) {
    if ($cond) { Write-Host "  PASS $msg" }
    else { Write-Host "  FAIL $msg" -Foreground Red; $script:fails++ }
}
function Inv([string]$target, [string]$method, $arguments) {
    $q = if ([string]::IsNullOrEmpty($target)) { "" } else { "?target=$target" }
    $body = @{ method = $method; args = $arguments } | ConvertTo-Json -Depth 12 -Compress
    return Invoke-RestMethod "http://127.0.0.1:$Port/invoke$q" -Method Post `
        -ContentType "application/json" -Body $body
}

$proc = $null
try {
    $proc = Start-Process -FilePath $Exe -ArgumentList "--agent-daemon --handoff-ui" `
        -WindowStyle Hidden -PassThru
    $up = $false
    for ($i = 0; $i -lt 60; $i++) {
        try { Invoke-RestMethod "http://127.0.0.1:$Port/health" -TimeoutSec 1 | Out-Null
              $up = $true; break }
        catch { Start-Sleep -Milliseconds 500 }
    }
    Ok $up "el daemon responde /health en el puerto $Port"
    if (-not $up) { throw "El daemon no levanto; sin eso el resto no significa nada." }

    Write-Host "== verbos del harness en el ControlApi real =="
    $packs = Inv "profileManager" "harnessPackCatalog" @()
    Ok ($packs.ok -and $packs.result.Count -ge 5) "harnessPackCatalog devuelve packs: $($packs.result.Count)"

    $facts = Inv "" "harnessDirectiveFacts" @()
    Ok ($facts.ok -and $facts.result -contains "project.hasGit") `
       "harnessDirectiveFacts enumera los hechos del gate when"

    $spec = Inv "profileManager" "agentProfileSpec" @("agent-intermedio")
    Ok ($spec.ok) "agentProfileSpec resuelve un preset de sistema"

    $summary = Inv "" "harnessSpecSummary" @("agent-intermedio")
    Ok ($summary.ok -and $summary.result.toolCount -gt 0) `
       "harnessSpecSummary: $($summary.result.toolCount) tools, ~$($summary.result.approxTokens) tok"
    Ok ($null -ne $summary.result.promptChars) "el resumen incluye el tamano del prompt"

    # Perfil propio: el camino que usa el editor (crear -> spec -> diff).
    $newId = (Inv "profileManager" "addAgentProfile" @("smoke-harness")).result
    Ok (-not [string]::IsNullOrEmpty($newId)) "addAgentProfile devuelve un id"
    $setOk = Inv "profileManager" "setAgentProfileSpec" @($newId, @{
        extends = "agent-intermedio"
        loop = @{ sameCallLimit = 2 }
    })
    Ok ($setOk.result -eq $true) "setAgentProfileSpec acepta un spec con herencia"

    $diff = Inv "profileManager" "agentProfileDiff" @($newId)
    Ok ($diff.ok -and $diff.result.Count -ge 1) `
       "agentProfileDiff muestra lo que cambia vs el padre: $($diff.result.Count)"

    $parents = Inv "profileManager" "eligibleParents" @($newId)
    $selfOffered = @($parents.result | Where-Object { $_.profileId -eq $newId }).Count
    Ok ($selfOffered -eq 0) "eligibleParents no ofrece el propio perfil"

    # Directivas: alta -> catalogo -> baja, contra el store real.
    $save = Inv "" "saveHarnessDirective" @("smoke-directiva", "creada por el smoke", "",
                                            "cuerpo de prueba", "global")
    Ok ($save.result.ok -eq $true) "saveHarnessDirective escribe la directiva"
    $cat = Inv "profileManager" "harnessDirectiveCatalog" @("")
    $found = @($cat.result | Where-Object { $_.name -eq "smoke-directiva" }).Count
    Ok ($found -eq 1) "aparece en el catalogo"
    $loaded = Inv "" "harnessDirective" @("smoke-directiva")
    Ok ($loaded.result.body -eq "cuerpo de prueba") "se relee con el mismo parser"
    $del = Inv "" "removeHarnessDirective" @("smoke-directiva", "global")
    Ok ($del.result.ok -eq $true) "removeHarnessDirective la borra"

    # El aislamiento tiene que ser real: en modo test Qt cuelga AppLocalData de
    # AppData/Local/qttest/<Org>/<App>. Si hubiera escrito en la instalacion real,
    # aca no habria nada y el test estaria mintiendo sobre su propio aislamiento.
    $testRoot = Join-Path $env:LOCALAPPDATA "qttest"
    $seeded = Get-ChildItem -Path $testRoot -Recurse -Filter "*.md" -ErrorAction SilentlyContinue |
              Where-Object { $_.FullName -like "*harness*directives*" }
    Ok ($seeded.Count -ge 1) "las directivas fueron al AppData de test, no a la instalacion real"

    # El verbo de comparacion existe y responde aunque no haya corridas.
    $cmp = Inv "" "compareHarnessBenchmarks" @(@("agent-intermedio"), "")
    Ok ($cmp.ok) "compareHarnessBenchmarks responde (sin corridas: informe vacio)"
    Ok ($cmp.result.groupBy -eq "agentProfileId") "y agrupa por perfil de AGENTE"
} finally {
    if ($proc) { try { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } catch {} }
    Remove-Item -Recurse -Force $sandbox -ErrorAction SilentlyContinue
    # El AppData de test es compartido por todos los smokes: se limpia solo lo del
    # harness para no pisarle datos a otro test que corra en paralelo.
    Remove-Item -Recurse -Force (Join-Path $env:LOCALAPPDATA "qttest\LlamaCode\LlamaCode\harness") `
        -ErrorAction SilentlyContinue
}

Write-Host ""
if ($fails -eq 0) { Write-Host "=== HARNESS HEADLESS SMOKE OK ===" -Foreground Green; exit 0 }
else { Write-Host "=== $fails CHECK(S) FAILED ===" -Foreground Red; exit 1 }
