@echo off
setlocal
rem ---------------------------------------------------------------------------
rem Levanta el servidor de embeddings de StudIA (bge-m3) en el puerto 8081.
rem
rem Es un segundo llama-server, aparte del de chat. Van separados a proposito:
rem el de chat ocupa casi toda la VRAM, y un modelo de embeddings es otra clase
rem de modelo (convierte texto en vectores, no genera texto).
rem
rem Por defecto corre en CPU. En uso normal solo tiene que vectorizar la
rem pregunta del estudiante -- un texto corto por consulta -- asi que la CPU
rem alcanza de sobra y la placa le queda libre al modelo de chat.
rem
rem   servidor_embeddings.bat          -> CPU  (para usar StudIA)
rem   servidor_embeddings.bat gpu      -> GPU  (para vectorizar el corpus)
rem
rem Para vectorizar conviene GPU, pero entonces hay que cerrar antes el
rem servidor de chat: los dos juntos no entran en 8 GB.
rem ---------------------------------------------------------------------------

set "MODELO=%LOCALAPPDATA%\LlamaCode\LlamaCode\models\bge-m3-Q8_0.gguf"
set "PUERTO=8081"

rem El binario de llama.cpp que instalo LlamaCode: se toma el mas reciente.
set "SERVIDOR="
for /f "delims=" %%D in ('dir /b /o-n "%APPDATA%\LlamaCode\LlamaCode\tools\llama.cpp-install-*" 2^>nul') do (
    if not defined SERVIDOR set "SERVIDOR=%APPDATA%\LlamaCode\LlamaCode\tools\%%D\extract\llama-server.exe"
)

if not exist "%SERVIDOR%" (
    echo [ERROR] No se encontro llama-server.exe
    echo         Instalalo desde LlamaCode ^(pagina de binarios^).
    exit /b 1
)
if not exist "%MODELO%" (
    echo [ERROR] No se encontro el modelo de embeddings:
    echo         %MODELO%
    echo         Bajalo de huggingface.co/ggml-org/bge-m3-Q8_0-GGUF
    exit /b 1
)

rem --pooling cls es el que corresponde a bge-m3: el vector del documento sale
rem del token especial de inicio. Con otro pooling el servidor igual responde,
rem pero los vectores son peores y no hay ningun error que lo avise.
rem En CPU alcanza un lote chico: solo se vectoriza la pregunta. Para vectorizar
rem el corpus en GPU conviene un ubatch grande, asi entra un lote entero de
rem fragmentos en una sola pasada (medido: 52 fragmentos/s contra 15).
set "CAPAS=0"
set "LOTE=2048"
if /i "%~1"=="gpu" (
    set "CAPAS=999"
    set "LOTE=8192"
)

echo Modelo   : %MODELO%
echo Puerto   : %PUERTO%
if "%CAPAS%"=="0" (echo Ejecucion: CPU) else (echo Ejecucion: GPU)
echo.

"%SERVIDOR%" --host 127.0.0.1 --port %PUERTO% --model "%MODELO%" ^
    --embeddings --pooling cls --ctx-size 8192 ^
    --batch-size %LOTE% --ubatch-size %LOTE% --n-gpu-layers %CAPAS%

endlocal
