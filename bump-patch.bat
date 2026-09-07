@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

REM bump-patch.bat: auto-incrementa el patch (+0.0.1) de la version actual.
REM Lee la version de CMakeLists.txt (fuente de verdad), suma 1 al patch y
REM delega en bump-version.bat para sincronizar todos los archivos
REM (CMakeLists.txt, src/main.cpp, src/AppController.h, assets/update/latest.json).
REM Usa --no-update-flag: un build local NO marca "hay update" para el auto-updater.
REM Lo invocan build.bat y build_auto.bat antes de compilar, asi cada build sube
REM la version que muestra la UI (Configuracion -> vX.Y.Z).

for /f "usebackq delims=" %%V in (`powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$t=[IO.File]::ReadAllText('CMakeLists.txt');" ^
  "if ($t -match 'project\(LlamaCode VERSION (\d+)\.(\d+)\.(\d+)') { '{0}.{1}.{2}' -f $matches[1],$matches[2],([int]$matches[3]+1) } else { Write-Error 'no VERSION en CMakeLists.txt'; exit 1 }"`) do set "NEWVER=%%V"

if not defined NEWVER (
    echo [ERROR] bump-patch: no pude leer/incrementar la version
    exit /b 1
)

echo [INFO] Auto-bump version -^> %NEWVER%
call "%~dp0bump-version.bat" %NEWVER% --no-update-flag
if errorlevel 1 (
    echo === bump-patch FAILED ===
    exit /b 1
)
exit /b 0
