@echo off
REM Copia los documentos originales de StudIA (DATA_StudIA) a C:\StudIA_Docs y
REM deja la app apuntando ahi. Ver copiar_documentos.ps1 para las opciones.
REM
REM   copiar_documentos.bat                 copia
REM   copiar_documentos.bat -SoloApuntar    los deja en el disco externo

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0copiar_documentos.ps1" %*
echo.
pause
