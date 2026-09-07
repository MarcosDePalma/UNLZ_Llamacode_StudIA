@echo off
setlocal
cd /d "%~dp0"

REM Publica un release de GitHub con la version actual (sin argumentos = dry run).
REM   release.bat            -> muestra el plan, no publica
REM   release.bat -Publish   -> taggea, pushea y publica
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\release.ps1" %*
exit /b %errorlevel%
