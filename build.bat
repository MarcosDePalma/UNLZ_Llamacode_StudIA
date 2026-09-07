@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

REM Usage: build.bat [Debug|Release|Both] [NOPAUSE]   (default: Debug)
set CONFIGS=%1
if "%CONFIGS%"=="" set CONFIGS=Debug
set NO_PAUSE=0
if /I "%2"=="NOPAUSE" set NO_PAUSE=1

set QT_DIR=C:\Qt\6.8.3\msvc2022_64
set CMAKE=%PROGRAMFILES%\CMake\bin\cmake.exe
if not exist "%CMAKE%" set CMAKE=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
set GENERATOR=Visual Studio 16 2019
if exist "C:\BuildTools2022\MSBuild\Current\Bin\MSBuild.exe" set GENERATOR=Visual Studio 17 2022
if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" set GENERATOR=Visual Studio 17 2022
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" set GENERATOR=Visual Studio 17 2022

if not exist "%CMAKE%" (
    echo [ERROR] CMake not found.
    goto :failed
)

if not exist "%QT_DIR%\lib\cmake\Qt6\Qt6Config.cmake" (
    echo [ERROR] Qt6 not found at %QT_DIR%
    goto :failed
)

REM ── Encolamiento inteligente entre sesiones paralelas ────────────────────────
REM Si otra sesion (IA/CI) ya esta compilando la misma fuente, adopto su
REM resultado (REUSE) en vez de recompilar; si es otra fuente, espero turno.
set COORD=powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_coord.ps1"
for /f %%P in ('powershell -NoProfile -Command "$p=(Get-CimInstance Win32_Process -Filter ('ProcessId=' + $PID)).ParentProcessId; (Get-CimInstance Win32_Process -Filter ('ProcessId=' + $p)).ParentProcessId"') do set COORD_OWNER_PID=%%P
set HELD_LOCK=0
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_coord.ps1" -Lane build -Action acquire -OwnerPid %COORD_OWNER_PID%
set COORD_RC=%errorlevel%
if "%COORD_RC%"=="10" (
    echo [INFO] Build compartido en curso completado OK -^> reusando artefactos.
    echo === Build complete ^(reused^) ===
    goto :success
)
if not "%COORD_RC%"=="0" (
    echo [ERROR] No pude coordinar el build ^(rc=%COORD_RC%^).
    goto :failed
)
set HELD_LOCK=1

REM Cerrar solo la app que puede bloquear el .exe de salida. Los servidores y
REM compiladores de otras sesiones pertenecen a sus dueños; el coordinador ya
REM serializa esta lane.
echo [INFO] Closing LlamaCode before linking...
taskkill /F /IM LlamaCode.exe      >nul 2>&1

if not exist build mkdir build
cd build

if exist CMakeCache.txt (
    for /f "tokens=2 delims==" %%G in ('findstr /b /c:"CMAKE_GENERATOR:INTERNAL=" CMakeCache.txt') do set "GENERATOR=%%G"
)
if not exist CMakeCache.txt if exist _deps\qtkeychain-subbuild\CMakeCache.txt (
    for /f "tokens=2 delims==" %%G in ('findstr /b /c:"CMAKE_GENERATOR:INTERNAL=" _deps\qtkeychain-subbuild\CMakeCache.txt') do set "GENERATOR=%%G"
)

REM Configurar sólo al crear/migrar el árbol. En builds calientes, MSBuild/ZERO_CHECK
REM reejecuta CMake automáticamente si CMakeLists o los globs cambiaron. Evitar el
REM configure incondicional ahorra el escaneo de Qt/QML y FetchContent. Las fuentes
REM ya descargadas no consultan GitHub en cada regeneración.
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

if "%NEED_CONFIG%"=="1" (
    "%CMAKE%" .. -G "!GENERATOR!" -A x64 ^
        -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
        -DFETCHCONTENT_UPDATES_DISCONNECTED=ON
    if errorlevel 1 (
        echo.
        echo === Configure FAILED ===
        goto :failed
    )
) else (
    echo [INFO] Reusing CMake cache; incremental build will regenerate only if needed.
)

cd ..

set DID_DEBUG=0
set DID_RELEASE=0

if /I "%CONFIGS%"=="Both"    ( call :build_one Debug && call :build_one Release ) else ( call :build_one %CONFIGS% )
if errorlevel 1 (
    goto :failed
)

REM Shortcuts
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
if "%DID_RELEASE%"=="1" echo Release: build\Release\LlamaCode.exe - shortcut: LlamaCode.lnk
if "%DID_DEBUG%"=="1" echo Debug: build\Debug\LlamaCode.exe - shortcut: LlamaCode-Debug.lnk
goto :success

REM Build + deploy one config
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

REM El target Debug enlaza las DLL Debug de Qt (Qt6* d.dll), por lo que
REM windeployqt también debe desplegar los plugins Debug (en particular
REM platforms\qwindowsd.dll). Usar --release aquí deja qwindows.dll
REM incompatible y produce "no Qt platform plugin could be initialized".
set "DEPLOY_FLAG="
if /I "%CFG%"=="Release" set "DEPLOY_FLAG=--release"

echo [INFO] Deploying Qt runtime (%CFG%)...
"%WINDEPLOYQT%" %DEPLOY_FLAG% --qmldir "%~dp0qml" --no-translations --compiler-runtime "%EXE_PATH%" >nul
if errorlevel 1 ( echo. & echo === windeployqt %CFG% FAILED === & exit /b 1 )

echo [INFO] Copying Qt.labs.settings (%CFG%)...
xcopy /E /I /Y "%QT_DIR%\qml\Qt\labs\settings" "%EXE_DIR%\qml\Qt\labs\settings" >nul

if /I "%CFG%"=="Debug"   set DID_DEBUG=1
if /I "%CFG%"=="Release" set DID_RELEASE=1
exit /b 0

:failed
if "%HELD_LOCK%"=="1" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_coord.ps1" -Lane build -Action release -Result FAIL -OwnerPid %COORD_OWNER_PID%
    if errorlevel 12 call :warn_dirty
)
if "%NO_PAUSE%"=="0" pause
exit /b 1

:success
if "%HELD_LOCK%"=="1" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_coord.ps1" -Lane build -Action release -Result OK -OwnerPid %COORD_OWNER_PID%
    if errorlevel 12 call :warn_dirty
)
if "%NO_PAUSE%"=="0" pause
exit /b 0

REM La fuente cambio mientras compilabamos: otra sesion edito el working tree.
:warn_dirty
echo.
echo [WARN] La fuente cambio DURANTE el build ^(otra sesion, o vos mismo^).
echo [WARN] El binario no corresponde a la fuente con la que arranco el build,
echo [WARN] y un error de compilacion puede ser de la otra sesion, no tuyo.
echo [WARN] Para aislarte: powershell -File worktree.ps1 -Action new -Name ^<tarea^>
exit /b 0
