@echo off
rem Doble clic para instalar lo que le falte a StudIA.
rem
rem Es un envoltorio del .ps1 que esta al lado: PowerShell no deja ejecutar
rem scripts por doble clic si la politica no lo permite, y -ExecutionPolicy
rem Bypass evita tener que cambiarle la configuracion a la maquina.

echo.
echo   Revisando que le falta a StudIA...
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0instalar_dependencias.ps1" %*

echo.
echo   Si algo quedo sin instalar por permisos, hace clic derecho sobre este
echo   archivo y elegi "Ejecutar como administrador".
echo.
pause
