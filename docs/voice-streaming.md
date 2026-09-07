# STT streaming y Parakeet

LlamaCode conserva tres transportes de STT:

- `http_batch`: el contrato OpenAI-compatible existente. Es la ruta estable para
  whisper.cpp, servicios cloud y cualquier endpoint `/v1/audio/transcriptions`.
- `process_batch`: un CLI nativo administrado por la app. Parakeet usa esta ruta:
  LlamaCode crea un WAV temporal por turno, ejecuta `parakeet-cli` y extrae la
  línea de transcripción sin pedir Python, NeMo ni configurar un sidecar.
- `stream_process`: una sesión persistente administrada por la app. El audio se
  envía como PCM16 mono 16 kHz por stdin y el sidecar responde mensajes NDJSON
  por stdout.

## Protocolo NDJSON v1

La app envía una línea de configuración por sesión:

```json
{"type":"config","sample_rate":16000,"language":"es","model":"..."}
```

Después envía bloques de audio. `sequence` es monotónico por sesión y sirve para
diagnóstico; el sidecar no debe asumir que los números son un reloj de audio:

```json
{"type":"audio","sequence":1,"pcm16_base64":"..."}
```

El sidecar puede emitir el prefijo completo revisado varias veces:

```json
{"type":"partial","text":"quiero abrir"}
{"type":"partial","text":"quiero abrir el navegador"}
```

Al terminar, la app envía `{"type":"end"}` y espera:

```json
{"type":"final","text":"quiero abrir el navegador"}
```

`{"type":"cancel"}` invalida la sesión actual. Los resultados que lleguen
después de una cancelación se descartan. El proceso se lanza sin shell, recibe
las variables `LLAMACODE_STT_*`, hereda la máscara CUDA de voz y queda dentro del
Job Object de LlamaCode.

## Parakeet experimental

El catálogo incluye `parakeet-tdt-0.6b-v3` como motor seleccionable y administrado.
Desde Charla, el botón **Instalar Parakeet** descarga el modelo GGUF Q4_0 y, si
falta, el paquete de binarios de `whisper.cpp`, que contiene `parakeet-cli`.
Después alcanza con seleccionar Parakeet y pulsar **Iniciar Charla**; no requiere
instalar NeMo ni escribir un comando sidecar.

El modelo nativo se ejecuta por turno (`process_batch`), por lo que entrega el
texto final al terminar el habla y no parciales mientras se habla. El transporte
`stream_process` y `tools/parakeet_stt_sidecar.py` se conservan para experimentar
con runtimes externos y parciales rolling, pero siguen siendo una ruta avanzada.

La variante oficial Parakeet TDT v3 anuncia soporte multilingüe, incluido español.
Antes de convertirla en default hay que comparar WER/CER, p50/p90 y VRAM contra
Whisper base con el mismo corpus y hardware.

## Diagnóstico

Las métricas locales conservan `endpointReason`, el primer texto del LLM, el
primer texto útil y el primer audio enviado al dispositivo. `firstAudioQueued` es
un proxy de reproducción; no equivale a una medición acústica del parlante.
Para comparar motores se debe mirar fin de habla → primer audio útil, no sólo
TTFT o tokens por segundo.

El transporte batch sigue siendo el fallback recomendado si el sidecar no está
instalado, si NeMo no arranca o si se necesita una ruta cloud.
