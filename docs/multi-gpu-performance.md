# Diagnóstico y rendimiento multi-GPU

## Afinidad de perfiles por GPU

El menú de lanzamiento conserva el gate de VRAM, pero además calcula una señal
de afinidad contra el hardware detectado. Una configuración cuyo
`minVramGb` sólo puede alcanzarse sumando varias placas se marca como perfil
dual-GPU; por ejemplo, 2× RTX 3090 habilita y enfatiza los perfiles de 48 GB.

Los motores especializados pueden declarar una afinidad opcional en
`assets/system_profiles.json`:

```json
"hardwareAffinity": {
  "gpuNamePatterns": ["RTX 3090"],
  "requireGpuName": true,
  "gpuScope": "single",
  "label": "Nativa para RTX 3090 · 1 GPU"
}
```

La coincidencia sólo agrega la marca `🎯` y datos `gpuAffinity*` al menú; no
oculta ni bloquea perfiles. Así una NInfer para una GPU concreta puede convivir
con perfiles llama.cpp que aprovechan la VRAM agregada. La selección sigue
siendo una recomendación: el rendimiento final depende del modelo, backend,
reparto, enlace PCIe y calidad medida.

LlamaCode conserva el detalle de cada GPU detectada por `nvidia-smi` dentro de
`hardwareSummary.gpus`. Cada entrada incluye VRAM total/libre, bus PCIe,
generación, lanes, temperatura y potencia cuando el driver los informa.

El resumen también incluye:

- `hardwareFingerprint`: identificador estable para no mezclar benchmarks de
  máquinas o topologías distintas.
- `recommendedSplitMode`: recomendación conservadora (`layer` o `tensor`).
- `performanceRecommendation`: modo, KV sugerido, confianza y explicación.
- `p2pAvailable` / `nvlinkAvailable`: señales best-effort de `nvidia-smi topo -m`
  y `nvidia-smi nvlink -s`.
- `topology`: relaciones entre GPU (`NV*`, `PIX`, `PXB`, `PHB`, `SYS`) cuando el
  driver las publica.

## Reserva automática para Ingi Charla

Cuando el equipo expone dos o más GPU NVIDIA, `HardwareDiagnostics::voiceGpuPlan`
construye un plan efímero para el modo Charla. La GPU más débil se elige por menor
VRAM total (empates: VRAM libre y luego índice CUDA), y se reserva allí un margen
de 2048 MiB para STT, TTS y procesos auxiliares. El LLM conserva esa misma GPU,
pero sólo recibe la VRAM libre que queda después de la reserva; el resto de GPU
se agrega completo. `modelTensorSplit` contiene las proporciones calculadas a
partir de esas capacidades, no una división fija `1,1`.

Al entrar a Charla, si el perfil no tiene una selección manual de GPU ni declara
su propio `--tensor-split`, LlamaCode relanza una vez el perfil normal con ese
reparto (`--split-mode` + `--tensor-split`). Mientras Charla está activa,
`nvidia-smi` se consulta cada 2 segundos. Si otra aplicación toma o libera una
cantidad material de VRAM, el cambio debe aparecer en tres muestras consecutivas;
recién entonces se detiene y relanza el server con un split nuevo. Si el modelo
ya no entra de forma segura, no se hace un relanzamiento riesgoso: se registra el
aviso y se espera a que vuelva a haber margen.

STT gestionado y los TTS locales que se ejecutan como procesos (Piper, Qwen3-TTS
e Inflect) reciben `CUDA_VISIBLE_DEVICES` apuntando a la GPU reservada. Para un
STT/TTS HTTP local externo, la configuración de Charla permite indicar un
`sttManagedCommand`/`ttsManagedCommand` y sus listas de argumentos; LlamaCode
lo lanza sin shell, lo asocia a su job y lo detiene al cerrar Charla. Un endpoint
remoto, o un proceso que ya estaba corriendo y la app no lanzó, no puede ser
movido ni aislado retroactivamente.

El contrato se expone también como `App.voiceGpuPlan()` y dentro de
`hardwareSummary.voiceGpuPlan`, con `voiceGpuIndex`, `voiceGpuMask`,
`modelGpuMask`, `modelAvailableGb`, `modelRequiredGb`, `modelFitsCapacity`,
`voicePlacementSafe`, `modelByGpu` y `reason`. La selección manual del usuario y los perfiles
experimentales con un reparto explícito conservan prioridad. Si la GPU reservada
no tiene el margen libre suficiente, o el modelo/contexto activo supera la
capacidad combinada segura, el plan lo indica en `modelPlacementSafe=false` y no
fuerza el split. Para un perfil de sistema, Charla busca automáticamente el mayor
perfil normal instalado que sí entre; un perfil de usuario no se cambia en silencio
y la UI indica que hay que elegir uno más chico. Para Qwen3-TTS o
Inflect CUDA la reserva sube automáticamente (4–5 GiB según el caso); Piper y
Whisper usan el baseline de 2 GiB.

## Regla inicial

Con una sola GPU se conserva `layer` como valor seguro. En multi-GPU, si el
enlace mínimo conocido es menor que `PCIe generation × lanes = 16`, se sugiere
`split-mode layer`, porque reduce la comunicación frecuente durante el decode.
Con P2P/NVLink y enlaces más rápidos se permite probar `tensor`; la
recomendación sigue siendo una hipótesis y debe validarse con benchmark. Si el
driver no publica topología, el sistema conserva `layer` como fallback.

La selección de KV es orientativa:

- `q8_0` para equilibrio y contexto largo.
- `f16` para calidad cuando hay memoria suficiente.

El sistema no recomienda drivers P2P modificados ni reemplazos de motherboard.
Esas alternativas son dependientes del equipo y quedan fuera del flujo seguro
de perfiles.

## Pruebas

El parser y las reglas se prueban sin GPU real en `test_hardware_diagnostics`.
La validación de rendimiento real debe comparar, con el mismo modelo y prompt,
`pp/s`, `tg/s`, TTFT, VRAM por GPU, estabilidad y versión de `llama.cpp`.

### Smoke headless

El contrato completo, incluyendo el probe asíncrono y la recomendación, se
prueba sin QML ni modelo con:

```powershell
.\tests\headless_smoke.ps1 -Exe .\build\Debug\LlamaCode.exe
```

El script inicia `--agent-daemon`, invoca `runStartupScan`, espera
`hardwareSummary.hardwareFingerprint`, invoca
`performanceRecommendation("balanced")`, genera candidatos, anota una muestra
realista y valida el ranking. También valida que `splitMode` sea `layer` o
`tensor`. En una máquina sin NVIDIA también debe pasar, usando el fallback CPU.
El script borra el directorio temporal y detiene el daemon al finalizar.

### Prueba física de dos GPU

Con dos placas instaladas, la prueba siguiente compara la salida real de
`nvidia-smi` con el plan calculado por el daemon. En la máquina de desarrollo
espera las dos RTX 3090 de 24 GiB:

```powershell
.\tests\dual_gpu_voice_smoke.ps1 -Exe .\build\Debug\LlamaCode.exe -RequireRtx3090
```

No arranca un modelo ni reserva VRAM adicional. Verifica cantidad, modelo,
bus/VRAM libre observados, GPU de voz y el `modelTensorSplit` para ambas placas.

### Matriz declarativa

`performanceMatrixCandidates(target, withVision)` devuelve candidatos seguros
para medir. Cada fila contiene `splitMode`, `kvCache`, `ctxSize`, `mmproj`,
`target`, `id` y `status=pending`. Luego de una medición, el cliente puede
enriquecer la muestra con `annotatePerformanceMatrix(sample, candidate)` y
ordenar varias muestras con `rankPerformanceMatrix(samples, target)`. El score
acepta tanto métricas técnicas (`promptTps`/`generationTps`) como artefactos de
benchmark (`avgTps`, `qualityScore/qualityTotal`, `failed`, `timedOut`). Agrega
`performanceMatrixId`, fingerprint, estado, `performanceScore` y `rank` donde
corresponde. Ninguna fila queda marcada como medida sin haber ejecutado
realmente el servidor.
