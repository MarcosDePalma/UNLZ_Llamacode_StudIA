@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

REM build_auto.bat: variante SIN pause para uso automatizado (Claude / CI).
REM El build.bat con pause queda para el usuario. No agregar pause aca.
REM Usage: build_auto.bat [Debug|Release|Both]   (default: Debug)
set CONFIGS=%1
if "%CONFIGS%"=="" set CONFIGS=Debug

set QT_DIR=C:\Qt\6.8.3\msvc2022_64
set CMAKE=%PROGRAMFILES%\CMake\bin\cmake.exe
if not exist "%CMAKE%" set CMAKE=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
set GENERATOR=Visual Studio 16 2019
if exist "C:\BuildTools2022\MSBuild\Current\Bin\MSBuild.exe" set GENERATOR=Visual Studio 17 2022
if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" set GENERATOR=Visual Studio 17 2022
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" set GENERATOR=Visual Studio 17 2022

if not exist "%CMAKE%" (
    echo [ERROR] CMake not found.
    exit /b 1
)

if not exist "%QT_DIR%\lib\cmake\Qt6\Qt6Config.cmake" (
    echo [ERROR] Qt6 not found at %QT_DIR%
    exit /b 1
)

REM ── Encolamiento inteligente entre sesiones paralelas ────────────────────────
REM Si ya hay un build EN CURSO con la misma fuente, adopto su resultado (REUSE)
REM en vez de recompilar. Si es otra fuente, espero mi turno. Ver build_coord.ps1.
set COORD=powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_coord.ps1"
for /f %%P in ('powershell -NoProfile -Command "$p=(Get-CimInstance Win32_Process -Filter ('ProcessId=' + $PID)).ParentProcessId; (Get-CimInstance Win32_Process -Filter ('ProcessId=' + $p)).ParentProcessId"') do set COORD_OWNER_PID=%%P
set HELD_LOCK=0
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_coord.ps1" -Lane build -Action acquire -OwnerPid %COORD_OWNER_PID%
set COORD_RC=%errorlevel%
if "%COORD_RC%"=="10" (
    echo [INFO] Build compartido en curso completado OK -> reusando artefactos, no recompilo.
    echo === Build complete ^(reused^) ===
    exit /b 0
)
if not "%COORD_RC%"=="0" (
    echo [ERROR] No pude coordinar el build ^(rc=%COORD_RC%^).
    exit /b 1
)
set HELD_LOCK=1

REM Cada build sube la version +0.0.1 (patch) antes de configurar/compilar.
call "%~dp0bump-patch.bat"
if errorlevel 1 ( goto :done_fail )

echo [INFO] Killing LlamaCode and managed children...
taskkill /F /IM LlamaCode.exe      >nul 2>&1
taskkill /F /IM llama-server.exe   >nul 2>&1
taskkill /F /IM opencode.exe       >nul 2>&1
taskkill /F /IM aider.exe          >nul 2>&1

echo [INFO] Killing stale build tools...
taskkill /F /IM MSBuild.exe        >nul 2>&1
taskkill /F /IM CL.exe             >nul 2>&1
taskkill /F /IM link.exe           >nul 2>&1
taskkill /F /IM rc.exe             >nul 2>&1
taskkill /F /IM qmlcachegen.exe    >nul 2>&1
taskkill /F /IM rcc.exe            >nul 2>&1
taskkill /F /IM moc.exe            >nul 2>&1
ping -n 3 127.0.0.1 >nul 2>&1

echo [INFO] Clearing stale tlogs...
if exist build (
    for /r "build" %%f in (*.tlog) do del /f /q "%%f" >nul 2>&1
)

if not exist build mkdir build
cd build

REM Generador: si el arbol ya existe, manda el suyo. Migrarlo a la fuerza deja
REM los subbuilds de FetchContent en el generador viejo y el configure aborta.
if exist CMakeCache.txt (
    for /f "tokens=2 delims==" %%G in ('findstr /b /c:"CMAKE_GENERATOR:INTERNAL=" CMakeCache.txt') do set "GENERATOR=%%G"
)
if not exist CMakeCache.txt if exist _deps\qtkeychain-subbuild\CMakeCache.txt (
    for /f "tokens=2 delims==" %%G in ('findstr /b /c:"CMAKE_GENERATOR:INTERNAL=" _deps\qtkeychain-subbuild\CMakeCache.txt') do set "GENERATOR=%%G"
)

set NEED_CONFIG=0
if not exist CMakeCache.txt set NEED_CONFIG=1
if not exist CMakeFiles\VerifyGlobs.cmake set NEED_CONFIG=1

REM Un subbuild de QtKeychain de OTRO generador mata el configure entero ("Does
REM not match the generator used previously"). Se revisa SIEMPRE, no solo al
REM crear el arbol: la mezcla aparece en arboles ya configurados cuando dos
REM scripts detectaron VS distinto.
if exist _deps\qtkeychain-subbuild\CMakeCache.txt (
    set "DEP_GENERATOR="
    for /f "tokens=2 delims==" %%G in ('findstr /b /c:"CMAKE_GENERATOR:INTERNAL=" _deps\qtkeychain-subbuild\CMakeCache.txt') do set "DEP_GENERATOR=%%G"
    if defined DEP_GENERATOR if /I not "!DEP_GENERATOR!"=="!GENERATOR!" (
        echo [INFO] Removing incompatible generated QtKeychain build metadata.
        rmdir /s /q _deps\qtkeychain-subbuild
        rmdir /s /q _deps\qtkeychain-build
        set NEED_CONFIG=1
    )
)

REM VS is a multi-config generator: configure once, build per --config below.
if "%NEED_CONFIG%"=="1" (
    "%CMAKE%" .. -G "!GENERATOR!" -A x64 ^
        -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
        -DFETCHCONTENT_UPDATES_DISCONNECTED=ON
    if errorlevel 1 ( echo. & echo === Configure FAILED === & cd .. & goto :done_fail )
) else (
    echo [INFO] Reusing CMake cache; ZERO_CHECK reconfigura solo si hace falta.
)

cd ..

set DID_DEBUG=0
set DID_RELEASE=0

if /I "%CONFIGS%"=="Both"    ( call :build_one Debug && call :build_one Release ) else ( call :build_one %CONFIGS% )
if errorlevel 1 ( goto :done_fail )

REM ── Shortcuts ───────────────────────────────────────────────────────────────
if "%DID_RELEASE%"=="1" (
    echo [INFO] Updating Release shortcut...
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0update-shortcut.ps1" -Config Release -ShortcutName "LlamaCode" -Icon "assets\app_icon.ico"
)
if "%DID_DEBUG%"=="1" (
    echo [INFO] Updating Debug shortcut...
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0update-shortcut.ps1" -Config Debug -ShortcutName "LlamaCode-Debug" -Icon "assets\debug_icon.ico"
)

echo.
echo === Build complete ===
if "%DID_RELEASE%"=="1" echo Release: build\Release\LlamaCode.exe  (shortcut: LlamaCode.lnk)
if "%DID_DEBUG%"=="1"   echo Debug:   build\Debug\LlamaCode.exe    (shortcut: LlamaCode-Debug.lnk)
goto :done_ok

REM ── Liberacion del lock coordinado ───────────────────────────────────────────
:done_ok
if "%HELD_LOCK%"=="1" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_coord.ps1" -Lane build -Action release -Result OK -OwnerPid %COORD_OWNER_PID%
    if errorlevel 12 call :warn_dirty
)
exit /b 0
:done_fail
if "%HELD_LOCK%"=="1" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_coord.ps1" -Lane build -Action release -Result FAIL -OwnerPid %COORD_OWNER_PID%
    if errorlevel 12 call :warn_dirty
)
exit /b 1

REM La fuente cambio mientras compilabamos: otra sesion edito el working tree.
:warn_dirty
echo.
echo [WARN] La fuente cambio DURANTE el build ^(otra sesion, o vos mismo^).
echo [WARN] El binario no corresponde a la fuente con la que arranco el build,
echo [WARN] y un error de compilacion puede ser de la otra sesion, no tuyo.
echo [WARN] Para aislarte: powershell -File worktree.ps1 -Action new -Name ^<tarea^>
exit /b 0

REM ── Build + deploy one config ────────────────────────────────────────────────
:build_one
set CFG=%~1
echo.
echo [INFO] ===== Building %CFG% =====
"%CMAKE%" --build build --config %CFG% -- /maxcpucount:4
if errorlevel 1 ( echo. & echo === %CFG% Build FAILED === & exit /b 1 )

set "WINDEPLOYQT=%QT_DIR%\bin\windeployqt.exe"
set "EXE_DIR=%~dp0build\%CFG%"
set "EXE_PATH=%EXE_DIR%\LlamaCode.exe"
if not exist "%WINDEPLOYQT%" ( echo [ERROR] windeployqt not found & exit /b 1 )
if not exist "%EXE_PATH%"    ( echo [ERROR] %EXE_PATH% missing & exit /b 1 )

set DEPLOY_FLAG=--release
if /I "%CFG%"=="Debug" set DEPLOY_FLAG=--debug

echo [INFO] Deploying Qt runtime (%CFG%)...
"%WINDEPLOYQT%" %DEPLOY_FLAG% --qmldir "%~dp0qml" --no-translations --compiler-runtime "%EXE_PATH%"
if errorlevel 1 ( echo. & echo === windeployqt %CFG% FAILED === & exit /b 1 )

echo [INFO] Copying Qt.labs.settings (%CFG%)...
xcopy /E /I /Y "%QT_DIR%\qml\Qt\labs\settings" "%EXE_DIR%\qml\Qt\labs\settings" >nul

if /I "%CFG%"=="Debug"   set DID_DEBUG=1
if /I "%CFG%"=="Release" set DID_RELEASE=1
exit /b 0
