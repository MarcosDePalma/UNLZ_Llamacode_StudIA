# Instala lo que StudIA necesita para tener TODAS sus funciones.
#
# La app anda sin esto: el chat, la busqueda y los modos funcionan igual. Lo que
# se apaga sin estas herramientas es adjuntar bibliografia propia (Python),
# graficar funciones (matplotlib), los diagramas (mermaid) y el OCR de PDFs
# escaneados (Tesseract).
#
#   .\instalar_dependencias.ps1              instala lo que falte
#   .\instalar_dependencias.ps1 -SoloRevisar  no instala: solo informa
#
# Es idempotente: correrlo dos veces no reinstala nada.

param([switch]$SoloRevisar)

$ErrorActionPreference = "Continue"
$fallos = @()

function Titulo($t) { Write-Host "`n=== $t ===" -ForegroundColor Cyan }
function Ok($t)     { Write-Host "  [ya esta] $t" -ForegroundColor Green }
function Falta($t)  { Write-Host "  [falta]   $t" -ForegroundColor Yellow }
function Mal($t)    { Write-Host "  [ERROR]   $t" -ForegroundColor Red }

# El PATH del proceso no se actualiza solo cuando un instalador lo modifica:
# hay que releerlo del registro o el programa recien instalado "no existe".
function RefrescarPath {
    $env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" +
                [Environment]::GetEnvironmentVariable("Path", "User")
}

function Existe($cmd) { return [bool](Get-Command $cmd -ErrorAction SilentlyContinue) }

function Winget($id, $nombre) {
    if ($SoloRevisar) { return $false }
    Write-Host "  instalando $nombre ..." -ForegroundColor Gray
    # --source winget: sin esto puede fallar por el certificado de otra fuente.
    winget install --id $id --source winget --silent `
        --accept-package-agreements --accept-source-agreements | Out-Null
    RefrescarPath
    return $true
}

# ── Runtime de Visual C++ ────────────────────────────────────────────────────
# StudIA lleva sus propias copias junto al ejecutable, asi que la app abre sin
# esto. El que lo necesita instalado en el sistema es llama-server.exe, que vive
# en otra carpeta (la que baja LlamaCode) y por lo tanto no las ve.
#
# Sin esto el sintoma es feo y desorienta: LlamaCode abre bien y el servidor del
# modelo muere al arrancar sin decir por que.
Titulo "Runtime de Visual C++"
if (Test-Path (Join-Path $env:SystemRoot "System32\vcruntime140_1.dll")) {
    Ok "runtime de Visual C++"
} else {
    Falta "runtime de Visual C++ (lo necesita el motor del modelo)"
    Winget "Microsoft.VCRedist.2015+.x64" "Visual C++ Redistributable" | Out-Null
    if (Test-Path (Join-Path $env:SystemRoot "System32\vcruntime140_1.dll")) {
        Ok "runtime instalado"
    } else { $fallos += "Visual C++ Redistributable"; Mal "no se pudo instalar" }
}

# ── Python ───────────────────────────────────────────────────────────────────
Titulo "Python"
if (Existe python) { Ok ("Python " + (python --version 2>&1)) }
else {
    Falta "Python"
    Winget "Python.Python.3.12" "Python 3.12" | Out-Null
    if (Existe python) { Ok "Python instalado" } else { $fallos += "Python" ; Mal "no se pudo instalar" }
}

# ── Librerias de Python ──────────────────────────────────────────────────────
# modulo -> paquete de pip (no siempre se llaman igual)
$libs = [ordered]@{
    "pypdf"      = "pypdf"
    "docx"       = "python-docx"
    "pptx"       = "python-pptx"
    "openpyxl"   = "openpyxl"
    "numpy"      = "numpy"
    "matplotlib" = "matplotlib"
    "pypdfium2"  = "pypdfium2"
    "pytesseract"= "pytesseract"
}
Titulo "Librerias de Python"
if (Existe python) {
    $porInstalar = @()
    foreach ($m in $libs.Keys) {
        python -c "import $m" 2>$null
        if ($LASTEXITCODE -eq 0) { Ok $m } else { Falta $m; $porInstalar += $libs[$m] }
    }
    if ($porInstalar.Count -gt 0 -and -not $SoloRevisar) {
        Write-Host ("  instalando: " + ($porInstalar -join ", ")) -ForegroundColor Gray
        python -m pip install --quiet --disable-pip-version-check @porInstalar
        foreach ($m in $libs.Keys) {
            python -c "import $m" 2>$null
            if ($LASTEXITCODE -ne 0) { $fallos += $libs[$m]; Mal $m }
        }
    }
} else { Mal "sin Python no se pueden instalar" }

# ── Node y mermaid (diagramas) ───────────────────────────────────────────────
Titulo "Diagramas (Node + mermaid)"
if (Existe node) { Ok ("Node " + (node --version 2>&1)) }
else {
    Falta "Node.js"
    Winget "OpenJS.NodeJS.LTS" "Node.js LTS" | Out-Null
    if (Existe node) { Ok "Node instalado" } else { $fallos += "Node.js"; Mal "no se pudo instalar" }
}
$mmdc = Join-Path $env:APPDATA "npm\mmdc.cmd"
if ((Existe mmdc) -or (Test-Path $mmdc)) { Ok "mermaid-cli" }
elseif (Existe npm) {
    Falta "mermaid-cli"
    if (-not $SoloRevisar) {
        Write-Host "  instalando mermaid-cli (baja un navegador, tarda) ..." -ForegroundColor Gray
        # --allow-scripts=puppeteer: sin esto npm bloquea la descarga del
        # navegador que mermaid necesita para dibujar, y falla al renderizar.
        npm install -g --allow-scripts=puppeteer @mermaid-js/mermaid-cli 2>&1 | Out-Null
        if (Test-Path $mmdc) { Ok "mermaid-cli instalado" } else { $fallos += "mermaid-cli"; Mal "no se pudo instalar" }
    }
} else { Mal "sin npm no se puede instalar mermaid" }

# ── Tesseract (OCR de PDFs escaneados) ───────────────────────────────────────
Titulo "OCR (Tesseract)"
$tessDirs = @(
    (Join-Path $env:ProgramFiles "Tesseract-OCR"),
    (Join-Path ${env:ProgramFiles(x86)} "Tesseract-OCR"),
    (Join-Path $env:LOCALAPPDATA "Programs\Tesseract-OCR")
)
function TessExe {
    if (Existe tesseract) { return (Get-Command tesseract).Source }
    foreach ($d in $script:tessDirs) {
        $e = Join-Path $d "tesseract.exe"
        if (Test-Path $e) { return $e }
    }
    return $null
}
$tess = TessExe
if ($tess) { Ok "Tesseract en $tess" }
else {
    Falta "Tesseract"
    Winget "UB-Mannheim.TesseractOCR" "Tesseract OCR" | Out-Null
    $tess = TessExe
    if ($tess) { Ok "Tesseract instalado en $tess" } else { $fallos += "Tesseract"; Mal "no se pudo instalar" }
}

# El paquete de espanol NO viene por defecto y el corpus esta en castellano: sin
# esto el OCR lee mal las tildes y las palabras acentuadas.
if ($tess) {
    $tessdata = Join-Path (Split-Path $tess) "tessdata"
    $spa = Join-Path $tessdata "spa.traineddata"
    if (Test-Path $spa) { Ok "paquete de espanol" }
    else {
        Falta "paquete de espanol para el OCR"
        if (-not $SoloRevisar) {
            try {
                New-Item -ItemType Directory -Force -Path $tessdata | Out-Null
                $url = "https://github.com/tesseract-ocr/tessdata/raw/main/spa.traineddata"
                Invoke-WebRequest $url -OutFile $spa -UseBasicParsing
                if (Test-Path $spa) { Ok "espanol descargado" }
            } catch {
                $fallos += "spa.traineddata"
                Mal ("no se pudo bajar el espanol: " + $_.Exception.Message)
                Write-Host "         (puede requerir permisos de administrador)" -ForegroundColor Gray
            }
        }
    }
}

# ── Resumen ──────────────────────────────────────────────────────────────────
Titulo "Resumen"
if ($SoloRevisar) {
    Write-Host "  (solo revision: no se instalo nada)" -ForegroundColor Gray
}
if ($fallos.Count -eq 0) {
    Write-Host "  Todo listo. StudIA tiene todas sus funciones." -ForegroundColor Green
    exit 0
}
Write-Host ("  Quedo sin instalar: " + ($fallos -join ", ")) -ForegroundColor Yellow
Write-Host "  StudIA funciona igual; se apagan solo esas funciones." -ForegroundColor Gray
Write-Host "  Si el error menciona permisos, corre este script como administrador." -ForegroundColor Gray
exit 1
