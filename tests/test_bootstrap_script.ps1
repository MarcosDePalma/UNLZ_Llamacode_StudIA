$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$ps  = [IO.File]::ReadAllText((Join-Path $root 'scripts\bootstrap.ps1'))
$sh  = [IO.File]::ReadAllText((Join-Path $root 'scripts\bootstrap.sh'))
$app = [IO.File]::ReadAllText((Join-Path $root 'src\AppController.cpp'))
$fails = 0

function Check([bool]$condition, [string]$message) {
    if ($condition) { Write-Host "  PASS $message" }
    else { Write-Host "  FAIL $message" -ForegroundColor Red; $script:fails++ }
}

Write-Host '== bootstrap / actualizar ahora =='

# El detector consulta cristianlukas; si el bootstrap clona otro repo, "actualizar"
# baja codigo que no es el que disparo el aviso.
Check (-not $ps.Contains('guideahon')) 'bootstrap.ps1 no apunta al repo viejo'
Check (-not $sh.Contains('guideahon')) 'bootstrap.sh no apunta al repo viejo'
Check ($ps.Contains('cristianlukas/UNLZ_Llamacode')) 'bootstrap.ps1 clona el repo publicado'
Check ($sh.Contains('cristianlukas/UNLZ_Llamacode')) 'bootstrap.sh clona el repo publicado'

# LC_DIR viene del app con la instalacion que corre; sin el guard, el
# reset --hard se lleva puesto lo no commiteado de ese checkout.
Check ($ps -match 'git -C \$Dir status --porcelain') 'chequea si el destino tiene cambios sin commitear'
$dirtyIdx = $ps.IndexOf('status --porcelain')
$resetIdx = $ps.IndexOf('git -C $Dir reset --hard')   # el comando, no el comentario
Check ($dirtyIdx -gt 0 -and $dirtyIdx -lt $resetIdx) 'el chequeo corre ANTES del reset --hard'
Check ($ps.Contains('LC_FORCE')) 'se puede forzar explicitamente con LC_FORCE'

# Matar la app antes de configurar dejaba al usuario sin app cuando algo fallaba.
$stopIdx      = $ps.IndexOf('Stop-LlamaCodeProcesses' + [Environment]::NewLine)
$configureIdx = $ps.IndexOf('Info "Configuring..."')
$buildIdx     = $ps.IndexOf('Info "Building ($Config)..."')
Check ($configureIdx -gt 0 -and $buildIdx -gt $configureIdx) 'configure y build en orden'
Check ($stopIdx -gt $configureIdx -and $stopIdx -lt $buildIdx) 'la app se cierra recien antes del build'

# Lado del app.
Check ($app.Contains('installRootForExePath(QCoreApplication::applicationFilePath())')) 'el app calcula la raiz de instalacion'
Check ($app -match '\$env:LC_DIR=') 'el app le pasa LC_DIR al bootstrap'
Check ($app.Contains('QStringLiteral("-NoExit")')) 'la consola del update queda abierta para ver el error'

if ($fails) { throw "$fails bootstrap regression(s) failed" }
Write-Host 'All bootstrap regressions passed.'
