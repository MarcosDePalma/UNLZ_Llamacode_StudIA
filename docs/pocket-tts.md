# Pocket TTS en Ingi Charla

Pocket TTS se integra como un motor TTS local opcional. LlamaCode no embebe
PyTorch dentro del ejecutable: al elegir `pocket`, Charla lanza un sidecar
Python administrado bajo `AppLocalData/voice/pocket-tts/` y mantiene el modelo
cargado entre oraciones.

## Instalación

1. Abrí **🎙 Ingi Charla** y elegí `pocket` en **Motor TTS**.
2. Configurá un Python 3.10–3.14 (`python` en PATH o la ruta completa).
3. Pulsá **Instalar**. La app crea un venv, instala `pocket-tts`, precarga el
   modelo/voz y guarda la caché local.
4. Verificá que el estado diga **Pocket TTS listo** y arrancá Charla.

La instalación necesita internet. Las sesiones posteriores usan el sidecar en
`127.0.0.1`, la caché de la app y `HF_HUB_OFFLINE=1`; no se envía texto ni voz a
un servicio remoto.

## Idioma y clonación

Los idiomas disponibles son `spanish`, `english`, `french`, `german`,
`portuguese` e `italian`. Se puede usar una voz incorporada o indicar en
**Muestra / embedding** una ruta local a WAV/MP3 o a un embedding
`.safetensors`. El archivo se carga al iniciar el sidecar y no se copia a la
nube.

`pocketAutoEnable` permanece apagado por defecto. Primero conviene comparar
latencia, inteligibilidad y uso de CPU con Piper en el equipo real; después se
puede habilitar **Permitir en automático**.

## Arquitectura técnica

El adapter local expone `POST /v1/audio/speech` y responde con un WAV PCM16
chunked. El cliente Qt extrae el chunk `data` y lo envía directamente al
`QAudioSink`, de modo que la reproducción puede empezar antes del final de la
síntesis. El servidor se detiene junto con la sesión de Charla y no se añade
ninguna reserva de VRAM al plan multi-GPU.

Implementación upstream: [Kyutai Pocket TTS](https://github.com/kyutai-labs/pocket-tts).
