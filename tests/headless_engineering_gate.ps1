param([switch]$Build)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildDir = Join-Path $root "build_tests"
$buildBin = Join-Path $buildDir "Debug"
$qtBin = "C:\Qt\6.8.3\msvc2022_64\bin"
$qtKeychainBin = Join-Path $buildDir "_deps\qtkeychain-build\Debug"
$oldQpa = $env:QT_QPA_PLATFORM
$env:QT_QPA_PLATFORM = "offscreen"
$env:PATH = "$buildBin;$qtKeychainBin;$qtBin;$env:PATH"

if ($Build) {
    # Visual Studio puede lanzar proyectos hermanos en paralelo aunque se pase
    # /maxcpucount:1. Core se compila primero y cada test después, para evitar
    # carreras sobre llamacode_core.lib/.pdb en el árbol compartido.
    $targets = @("llamacode_core", "test_engineering_workflows",
        "test_task_security_policy", "test_tasks", "test_appcontroller",
        "test_control_api")
    foreach ($target in $targets) {
        & cmake --build $buildDir --config Debug --target $target -- /m:1
        if ($LASTEXITCODE -ne 0) { throw "Falló la compilación de $target" }
    }
}

$tests = @(
    "test_engineering_workflows",
    "test_task_security_policy",
    "test_tasks",
    "test_appcontroller",
    "test_control_api"
)
foreach ($name in $tests) {
    $exe = Join-Path $buildDir "Debug\$name.exe"
    if (-not (Test-Path -LiteralPath $exe)) {
        throw "Falta $exe. Ejecutá este script con -Build o tests.bat Debug."
    }
    Write-Host "=== $name ==="
    & $exe -o - -txt
    if ($LASTEXITCODE -ne 0) { throw "$name falló con código $LASTEXITCODE" }
}

# El daemon ya es headless y no carga QML; QT_QPA_PLATFORM=offscreen puede
# hacer que Qt GUI termine antes de abrir ControlApi. Se aísla del gate QML.
$env:QT_QPA_PLATFORM = $null
& powershell -NoProfile -ExecutionPolicy Bypass -File `
    (Join-Path $PSScriptRoot "headless_engineering_workflows.ps1")
if ($LASTEXITCODE -ne 0) { throw "Falló el smoke HTTP del daemon" }
if ($null -eq $oldQpa) { $env:QT_QPA_PLATFORM = $null } else { $env:QT_QPA_PLATFORM = $oldQpa }
Write-Host "OK: gate headless de workflows completo"
