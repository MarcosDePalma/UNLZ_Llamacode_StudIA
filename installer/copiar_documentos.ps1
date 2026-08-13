# Deja los documentos originales al alcance de StudIA.
#
# StudIA responde sin esto: el texto de cada fragmento vive dentro de studia.db.
# Lo que agregan los documentos es poder ABRIR el PDF, el apunte o la
# presentacion desde una cita.
#
#   .\copiar_documentos.ps1                    copia DATA_StudIA a C:\StudIA_Docs
#   .\copiar_documentos.ps1 -SoloApuntar       los deja donde estan (disco externo)
#   .\copiar_documentos.ps1 -Destino D:\Docs   otra carpeta
#   .\copiar_documentos.ps1 -Simular           no toca nada: dice que haria
#
# Por que robocopy y no una copia comun: las carpetas de la facultad son largas
# ("Vigilancia_Tecnologica_e_Inteligencia_Competitiva\Trabajo FINAL\Recursos
# del Trabajo Final\Ejemplos de Trabajos Monograficos\...") y varias rutas pasan
# los 260 caracteres que Windows admite. El Explorador se planta ahi; robocopy
# no. Por lo mismo el destino es corto: cada caracter de la carpeta destino es
# un caracter menos disponible para el resto de la ruta.

param(
    [string]$Origen,
    [string]$Destino = "C:\StudIA_Docs",
    [switch]$SoloApuntar,
    [switch]$Simular
)

$ErrorActionPreference = "Stop"

function Titulo($t) { Write-Host "`n=== $t ===" -ForegroundColor Cyan }
function Ok($t)     { Write-Host "  $t" -ForegroundColor Green }
function Aviso($t)  { Write-Host "  $t" -ForegroundColor Yellow }
function Mal($t)    { Write-Host "  $t" -ForegroundColor Red }

# ── Origen ───────────────────────────────────────────────────────────────────
if (-not $Origen) {
    foreach ($c in @((Join-Path $PSScriptRoot "DATA_StudIA"),
                     (Join-Path (Split-Path $PSScriptRoot -Parent) "DATA_StudIA"))) {
        if (Test-Path $c) { $Origen = $c; break }
    }
}
if (-not $Origen -or -not (Test-Path $Origen)) {
    Mal "No se encontro la carpeta DATA_StudIA."
    Write-Host ""
    Write-Host "  Tiene que estar al lado de este script, o indicarsela a mano:" -ForegroundColor Gray
    Write-Host "     .\copiar_documentos.ps1 -Origen E:\DATA_StudIA" -ForegroundColor Gray
    exit 1
}
$Origen = (Resolve-Path $Origen).Path

Titulo "Documentos"
$m = Get-ChildItem $Origen -Recurse -File -EA SilentlyContinue |
     Measure-Object -Property Length -Sum
Ok ("origen:  $Origen")
Ok ("         {0} archivos, {1:N2} GB" -f $m.Count, ($m.Sum / 1GB))

# ── Apuntar sin copiar ───────────────────────────────────────────────────────
# Para quien prefiere dejarlos en un disco externo y no ocupar el disco interno.
# StudIA los va a encontrar mientras ese disco este conectado.
if ($SoloApuntar) {
    $Destino = $Origen
    Aviso "no se copia nada: StudIA va a leerlos de donde estan."
    Aviso "si ese disco se desconecta, las citas dejan de abrir (el chat sigue igual)."
} else {
    Titulo "Copia"
    Ok ("destino: $Destino")

    $unidad = (Split-Path $Destino -Qualifier) + "\"
    $libre = (Get-PSDrive -Name $unidad.Substring(0,1) -EA SilentlyContinue).Free
    if ($libre -and $libre -lt $m.Sum) {
        Mal ("no entra: hacen falta {0:N2} GB y hay {1:N2} GB libres en $unidad" -f `
             ($m.Sum / 1GB), ($libre / 1GB))
        exit 1
    }

    if ($Simular) {
        Aviso "simulacion: no se copia nada."
    } else {
        Write-Host "  copiando (tarda varios minutos) ..." -ForegroundColor Gray
        # /E subcarpetas incluso vacias · /R:1 /W:1 no insistir eternamente con
        # un archivo trabado · /MT:8 en paralelo · el resto es para que el log no
        # tape la pantalla con 2937 lineas.
        robocopy $Origen $Destino /E /R:1 /W:1 /MT:8 /NFL /NDL /NJH /NP | Out-Null
        # robocopy no usa 0 como exito: 0-7 es "salio bien", 8 o mas es error.
        if ($LASTEXITCODE -ge 8) {
            Mal "robocopy fallo (codigo $LASTEXITCODE)."
            exit 1
        }

        $d = Get-ChildItem $Destino -Recurse -File -EA SilentlyContinue |
             Measure-Object -Property Length -Sum
        Ok ("copiados: {0} de {1} archivos" -f $d.Count, $m.Count)
        if ($d.Count -lt $m.Count) {
            Aviso ("quedaron {0} sin copiar." -f ($m.Count - $d.Count))
            Aviso "StudIA avisa al hacer clic en esas citas; el resto anda igual."
        }
    }
}

# ── Dejar a StudIA apuntando ahi ─────────────────────────────────────────────
# Misma clave que lee QSettings desde C++ (studia/carpetaDocumentos).
Titulo "Configuracion"
if ($Simular) {
    Aviso "simulacion: no se toca el registro."
} else {
    $clave = "HKCU:\Software\LlamaCode\LlamaCode\studia"
    New-Item -Path $clave -Force | Out-Null
    Set-ItemProperty -Path $clave -Name "carpetaDocumentos" `
                     -Value ($Destino -replace '\\', '/')
    Ok "StudIA va a buscar los documentos en $Destino"
}

Write-Host "`n  Listo. Abri StudIA y hace clic en una cita para probarlo." -ForegroundColor Green

# robocopy no usa 0 para "salio bien": devuelve 1 cuando copio algo. Sin este
# exit explicito el script termina heredando ese 1 y quien lo llame lo lee como
# un fallo.
exit 0
