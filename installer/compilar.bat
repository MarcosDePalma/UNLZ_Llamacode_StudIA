@echo off
REM Arma los instaladores de StudIA. Ver compilar.ps1 para las opciones.
REM
REM   compilar.bat            solo el principal
REM   compilar.bat -Corpus    tambien el de documentos originales (tarda)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0compilar.ps1" %*
exit /b %ERRORLEVEL%
