@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

REM Configura, compila y corre TODA la suite de tests (unit + integration).
REM Uso: tests.bat [Debug|Release]   (default: Debug)
REM Sin 'pause' al final → seguro para correr en CI / desde scripts.

set CFG=%1
if "%CFG%"=="" set CFG=Debug

set QT_DIR=C:\Qt\6.8.3\msvc2022_64
set CMAKE=%PROGRAMFILES%\CMake\bin\cmake.exe
if not exist "%CMAKE%" set CMAKE=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
set CTEST=%PROGRAMFILES%\CMake\bin\ctest.exe
if not exist "%CTEST%" set CTEST=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe

if not exist "%CMAKE%" ( echo [ERROR] CMake not found. & exit /b 1 )
if not exist "%QT_DIR%\lib\cmake\Qt6\Qt6Config.cmake" ( echo [ERROR] Qt6 not found at %QT_DIR% & exit /b 1 )

REM ── Encolamiento inteligente entre sesiones paralelas ────────────────────────
REM Lane 'tests' (lock separado del app build, corren en paralelo). Si ya hay una
REM corrida de tests EN CURSO con la misma fuente, adopto su resultado (REUSE).
set COORD=powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_coord.ps1"
for /f %%P in ('powershell -NoProfile -Command "$p=(Get-CimInstance Win32_Process -Filter ('ProcessId=' + $PID)).ParentProcessId; (Get-CimInstance Win32_Process -Filter ('ProcessId=' + $p)).ParentProcessId"') do set COORD_OWNER_PID=%%P
set HELD_LOCK=0
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_coord.ps1" -Lane tests -Action acquire -OwnerPid %COORD_OWNER_PID%
set COORD_RC=%errorlevel%
if "%COORD_RC%"=="10" (
    echo [INFO] Tests compartidos en curso pasaron OK -> reuso resultado, no recompilo.
    echo === All tests passed ^(reused^) ===
    exit /b 0
)
if not "%COORD_RC%"=="0" (
    echo [ERROR] No pude coordinar los tests ^(rc=%COORD_RC%^).
    exit /b 1
)
set HELD_LOCK=1

REM Build dir separado para no pisar la build del app. CMake configura una sola
REM vez; luego ZERO_CHECK regenera sólo cuando cambian sus entradas.
if not exist build_tests mkdir build_tests

if not exist build_tests\CMakeCache.txt (
    "%CMAKE%" -S . -B build_tests -A x64 -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
        -DBUILD_TESTS=ON -DFETCHCONTENT_UPDATES_DISCONNECTED=ON
    if errorlevel 1 ( echo. & echo === Configure FAILED === & goto :done_fail )
) else (
    echo [INFO] Reusing build_tests CMake cache.
)

"%CMAKE%" --build build_tests --config %CFG% -- /maxcpucount:4
if errorlevel 1 ( echo. & echo === Build FAILED === & goto :done_fail )

REM Los test exes necesitan las DLLs de Qt en PATH (no se hace windeployqt).
set PATH=%QT_DIR%\bin;%PATH%
REM CTest usa QT_QPA_PLATFORM=offscreen para no abrir ventanas. En los tests
REM ejecutables no se hace windeployqt, por lo que hay que indicar también el
REM árbol de plugins de la instalación Qt (en especial platforms/qoffscreen.dll).
set QT_PLUGIN_PATH=%QT_DIR%\plugins

echo.
echo [INFO] ===== Running ctest (%CFG%) =====
"%CTEST%" --test-dir build_tests -C %CFG% --output-on-failure
if errorlevel 1 ( echo. & echo === TESTS FAILED === & goto :done_fail )

REM El runner externo de harnesses y su contrato JSONL tienen tests de Python.
where python >nul 2>&1
if errorlevel 1 ( echo [ERROR] Python is required for tests\test_harness_matrix.py. & goto :done_fail )
python -m unittest discover -s tests -p "test_harness_matrix.py"
if errorlevel 1 ( echo. & echo === PYTHON TESTS FAILED === & goto :done_fail )
python -m unittest tests.test_kv_cache_ab
if errorlevel 1 ( echo. & echo === KV CACHE A/B PYTHON TESTS FAILED === & goto :done_fail )

echo.
echo === All tests passed ===
goto :done_ok

:done_ok
if "%HELD_LOCK%"=="1" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_coord.ps1" -Lane tests -Action release -Result OK -OwnerPid %COORD_OWNER_PID%
    if errorlevel 12 call :warn_dirty
)
exit /b 0
:done_fail
if "%HELD_LOCK%"=="1" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_coord.ps1" -Lane tests -Action release -Result FAIL -OwnerPid %COORD_OWNER_PID%
    if errorlevel 12 call :warn_dirty
)
exit /b 1

REM La fuente cambio mientras corrian los tests: otra sesion edito el working tree.
:warn_dirty
echo.
echo [WARN] La fuente cambio DURANTE la corrida ^(otra sesion, o vos mismo^).
echo [WARN] El gate NO es confiable: los tests no corrieron sobre la fuente que
echo [WARN] estas por commitear. Volve a correr tests.bat con el tree quieto, o
echo [WARN] aislate: powershell -File worktree.ps1 -Action new -Name ^<tarea^>
exit /b 0
