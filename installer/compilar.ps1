# Arma el instalador de StudIA con Inno Setup. Sale a dist\
#
# Antes de compilar hace dos cosas que no son obvias y sin las cuales el
# instalador sale roto:
#
#   1. Copia el runtime de Visual C++ al lado del ejecutable. Sin eso la app no
#      abre en una PC sin Visual Studio, o sea en cualquiera que no sea esta.
#   2. Junta los scripts sueltos (tools\studia, installer) dentro de
#      build\Release\StudIA, que es lo que el .iss empaqueta.
#
# El material de estudio NO va en el instalador. Viaja aparte, en una carpeta
# con studia.db y DATA_StudIA adentro, que el usuario copia donde quiera y elige
# desde la app. No entra en un .exe (Windows lo limita a 4,2 GB) y ademas separa
# dos cosas con ritmos distintos: el programa cambia con cada version, el indice
# solo cuando se reindexa.

param()

$ErrorActionPreference = "Stop"
$raiz    = Split-Path $PSScriptRoot -Parent
$release = Join-Path $raiz "build\Release"
$dist    = Join-Path $raiz "dist"

function Titulo($t) { Write-Host "`n=== $t ===" -ForegroundColor Cyan }
function Ok($t)     { Write-Host "  $t" -ForegroundColor Green }
function Aviso($t)  { Write-Host "  $t" -ForegroundColor Yellow }

# ── Inno Setup ───────────────────────────────────────────────────────────────
$iscc = @(
    (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe"),
    (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"),
    (Join-Path $env:ProgramFiles "Inno Setup 6\ISCC.exe")
) | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $iscc) {
    Write-Host "No se encontro Inno Setup." -ForegroundColor Red
    Write-Host "  winget install --id JRSoftware.InnoSetup --source winget" -ForegroundColor Gray
    exit 1
}
Titulo "Inno Setup"; Ok $iscc

# ── Que este el build ────────────────────────────────────────────────────────
Titulo "Build"
foreach ($f in @("LlamaCode.exe")) {
    $p = Join-Path $release $f
    if (-not (Test-Path $p)) {
        Write-Host "  falta $p" -ForegroundColor Red
        Write-Host "  Compila primero: build.bat Release" -ForegroundColor Gray
        exit 1
    }
    Ok ("$f  " + [math]::Round((Get-Item $p).Length / 1MB, 1) + " MB")
}

# El modelo de embeddings si va adentro: sin el, la busqueda queda solo por
# palabras. No es fatal, pero conviene enterarse antes de repartir el paquete.
$modelos = Join-Path $release "StudIA\modelos"
if ((Test-Path $modelos) -and (Get-ChildItem $modelos -Filter *.gguf -EA SilentlyContinue)) {
    $g = Get-ChildItem $modelos -Filter *.gguf | Select-Object -First 1
    Ok ("modelos\" + $g.Name + "  " + [math]::Round($g.Length / 1MB, 1) + " MB")
} else {
    Aviso "no hay modelo de embeddings en StudIA\modelos: la busqueda semantica no va a andar."
}

# ── Runtime de Visual C++ al lado del ejecutable ─────────────────────────────
# Copiarlas aca (no instalarlas en el sistema) evita pedir permisos de
# administrador. llama-server.exe, que vive en otra carpeta, no las alcanza: de
# ese se ocupa instalar_dependencias.ps1.
Titulo "Runtime de Visual C++"
$crt = Get-ChildItem 'C:\Program Files*\Microsoft Visual Studio\*\*\VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT' `
        -Directory -ErrorAction SilentlyContinue | Sort-Object FullName | Select-Object -Last 1
if ($crt) {
    Copy-Item (Join-Path $crt.FullName "*.dll") $release -Force
    Ok ("copiadas de " + $crt.FullName)
} else {
    Aviso "no se encontro el redistribuible de MSVC."
    Aviso "El instalador se arma igual, pero la app puede no abrir en una PC sin Visual Studio."
}

# ── Scripts que viajan con la app ────────────────────────────────────────────
# Viven en el repo, no en build\. Copiarlos aca (y no a mano) es lo que evita
# empaquetar la version vieja de un script que se toco despues del ultimo build.
Titulo "Scripts"
$destino = Join-Path $release "StudIA"
New-Item -ItemType Directory -Force -Path $destino | Out-Null
foreach ($s in @("tools\studia\instalar_dependencias.bat",
                 "tools\studia\instalar_dependencias.ps1")) {
    $o = Join-Path $raiz $s
    if (Test-Path $o) { Copy-Item $o $destino -Force; Ok (Split-Path $s -Leaf) }
    else { Aviso "falta $s" }
}

# ── Compilar ─────────────────────────────────────────────────────────────────
New-Item -ItemType Directory -Force -Path $dist | Out-Null

Titulo "Instalador"
$t0 = Get-Date
& $iscc (Join-Path $PSScriptRoot "StudIA.iss")
if ($LASTEXITCODE -ne 0) {
    Write-Host "  fallo la compilacion" -ForegroundColor Red
    exit 1
}
Ok ("listo en " + [math]::Round(((Get-Date) - $t0).TotalMinutes, 1) + " min")

# ── Resumen ──────────────────────────────────────────────────────────────────
Titulo "Listo"
Get-ChildItem $dist -Filter *.exe | ForEach-Object {
    Write-Host ("  {0,-40} {1,8} GB" -f $_.Name, [math]::Round($_.Length / 1GB, 2))
}
Write-Host "`n  Carpeta: $dist" -ForegroundColor Gray
