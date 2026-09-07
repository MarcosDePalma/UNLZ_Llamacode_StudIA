# Auto-tuning de parámetros de inferencia

Búsqueda automática de los flags de `llama-server` que maximizan **velocidad
(tok/s)** SIN degradar la **calidad** del modelo. Inspirado en *llama-launcher
v1.3* (Bayesian/TPE), pero corrige su fallo conocido: tunear el quant de KV
cache solo por velocidad colapsa siempre al quant más bajo y degrada el modelo.
Acá un **gate de calidad** lo impide.

```
LaunchProfile (origen)
   ↓ EffectiveProfile (binario + args + env)
AutoTuner (TPE-lite)  →  TunerEngine  →  llama-server (puerto scratch)
   ↑ loss = -tok/s + penalización si calidad < gate      ↓
   └──────────── TrialResult (tok/s, calidad) ───────────┘
   ↓ mejor config
Nuevo LaunchProfile "-tuned"  (el original queda intacto)
```

---

## Componentes

| Archivo | Rol |
|---------|-----|
| `src/core/tuner/AutoTuner.{h,cpp}` | Optimizador TPE-lite (C++ puro, sin Qt). Parzen discreto por parámetro + gate de calidad. Evaluación inyectada por callback. |
| `src/core/tuner/TunerEngine.{h,cpp}` | Integración real: compone argv, lanza `llama-server`, espera `/health`, mide tok/s vía `/completion`, califica con substrings y valida PPL con `llama-perplexity` si está disponible. |
| `src/core/tuner/TunerWorker.{h,cpp}` | Corre el tuning en un `QThread` (cada trial carga modelo → no congela la UI). Señales `trial`/`finished`, cancelación cooperativa. |
| `AppController` | `startAutoTune` / `cancelAutoTune` + props `autoTuneRunning/Progress/Status` + señales `autoTuneTrial/Finished`. |
| `qml/pages/ProfilesPage.qml` | Botones **Auto-tune** / **Cancelar tune** + línea de estado. |

---

## Optimizador (AutoTuner, TPE-lite)

TPE = Tree-structured Parzen Estimator. Versión discreta y compacta:

1. **Startup** (`startupTrials`): muestreo aleatorio para sembrar el historial.
2. Cada iteración posterior: parte el historial en **bueno** (mejor fracción
   `gamma` por loss) y **malo**. Por cada parámetro modela `l(x)` (de los buenos)
   y `g(x)` (de los malos) como distribuciones discretas con suavizado de Laplace.
3. Muestrea `eiCandidates` candidatos de `l(x)` y elige el de mayor `l(x)/g(x)`
   (Expected Improvement aproximado).
4. Corta al llegar a `maxTrials` o si `shouldStop()` (cancelación).

### Función de loss (el gate)

```
loss = -throughput
if quality < qualityGate:
    loss += 1e6 * (qualityGate - quality)   # penalización dominante
if trial.failed:  # server no arrancó / OOM / timeout
    loss = 1e9
```

La penalización (`1e6`) supera cualquier ganancia realista de tok/s → ninguna
velocidad compensa romper la calidad. Por eso el quant KV bajo no gana si el
modelo se degrada por debajo del umbral.

---

## Espacio de búsqueda (default)

Definido en `AppController::buildTuneParams()`:

| Parámetro | Flag | Opciones | Quality-risk |
|-----------|------|----------|:---:|
| ngl | `-ngl` | 0 / 20 / 40 / 99 | |
| batch | `-b` | 256 / 512 / 1024 / 2048 | |
| ubatch | `-ub` | 128 / 256 / 512 / 1024 / 2048 | |
| flash-attn | `--flash-attn` (switch) | off / on | |
| cache-type-k | `--cache-type-k` | f16 / q8_0 / q4_0 | ✓ |
| cache-type-v | `--cache-type-v` | f16 / q8_0 / q4_0 | ✓ |
| split-mode | `--split-mode` | layer / tensor | |
| spec n-max | `--spec-draft-n-max` | 1..5 (fixed) / n-min..9 (adaptive) | |
| DSpark conf-min | `--spec-draft-conf-min` | 0 / 0.2 / 0.4 / 0.6 / 0.8 | |

Los dos últimos sólo entran cuando el comando efectivo declara speculative
decoding; `conf-min` se explora específicamente para DSpark. Cuando el backend
reporta `timings.draft_n` y `timings.draft_n_accepted`, la UI muestra la
aceptación como `aceptados/propuestos`.

`split-mode` sólo entra al espacio cuando hay al menos dos GPU NVIDIA, el
binario CUDA declara soporte para el flag y el perfil no usa CPU-MoE ni
`--override-tensor`. Esos layouts tienen reglas de residencia propias y no son
intercambiables de forma segura.

Combos inválidos para el binario (p.ej. quant de V cache sin flash-attn) hacen
fallar ese trial → quedan penalizados automáticamente. El optimizador los evita.

### Modo CPU-only

El botón **Tune CPU** fuerza `-ngl 0` y usa un espacio más apropiado para equipos
sin GPU: `threads`, `batch`, `ubatch` y cache K/V. No explora flash-attn ni MTP,
porque en CPU el beneficio suele estar en prompt processing y balance de hilos.

### Gate PPL

Si junto a `llama-server` existe `llama-perplexity` y hay un corpus local
disponible, el tuner mide primero la PPL baseline y valida los trials que cambian
knobs de riesgo de calidad (`cache-type-k/v`). Un trial sólo conserva su calidad
completa si su PPL queda dentro del umbral configurado (default 3%). Si falla PPL,
el trial se marca inviable. Si no hay binario/corpus, el tuner cae al gate liviano
por substrings para no bloquear el flujo.

---

## Medición de un trial (TunerEngine)

1. **Compone argv**: `baseArgs` (del EffectiveProfile, sin host/port ni los
   flags afinados) + flags del candidato + `--host`/`--port`.
2. **Lanza** `llama-server` en un **puerto scratch** (`18099`), con el **entorno
   del perfil** (`effectiveEnv`: PATH/CUDA para cargar las DLLs del backend GPU)
   y working dir = carpeta del binario. *(Sin el env correcto el server no usa
   GPU o crashea — todos los trials darían 0 tok/s.)*
3. **Espera** `/health` 200 (hasta `readyTimeoutMs`); aborta apenas el proceso
   muere.
4. **Mide**: POST `/completion` (`stream:false`), PP y TG por separado desde
   `timings.prompt_per_second` y `timings.predicted_per_second` (con fallback a
   tokens/tiempo). El objetivo combina ambos con el peso elegido en la UI.
5. **Califica**: fracción de substrings de aceptación presentes en `content`
   (estilo EvalSuite), en `[0,1]`.
6. **Registra** la aceptación speculative (`draft_n_accepted / draft_n`) cuando
   el backend la entrega, junto con PP/TG.
7. **Mata** el server para liberar RAM/VRAM.
8. Si corresponde, corre `llama-perplexity` sobre el candidato y combina el score
   con el gate PPL.

---

## Resultado: perfil nuevo, no sobrescribe

Al terminar, `onAutoTuneFinished` sólo **promociona** un candidato que, con
baseline válido, lo supere al menos 1% sin bajar su calidad. Si ningún trial
cumple, no crea un perfil falso "optimizado" y la UI deja el diagnóstico y las
mediciones. Sin baseline medible se conserva el gate de calidad del optimizador.

Cuando sí se promociona, `onAutoTuneFinished` **clona** el LaunchProfile origen
(backend/model/runtime/harness/workspace) en uno nuevo con sufijo `-tuned` y
alias `"Auto-tuned: <origen>"`, y aplica la mejor config en `extraArgs`
(reemplazando flags previos de los mismos parámetros). El perfil original queda
intacto.

---

## Uso (UI)

1. `ProfilesPage` → elegir LaunchProfile → **Auto-tune** o **Tune CPU** (el
   server principal debe estar detenido).
2. Corre `maxTrials` (default 24); la línea de estado muestra
   `Trial i/N — X tok/s, calidad Q [flags]`.
3. **Cancelar tune** corta tras el trial en curso.
4. Al terminar aparece el perfil `-tuned` en el dropdown sólo si pasó el gate
   de promoción; de lo contrario se conserva el perfil original.

### Adaptive speculation

Los perfiles MTP/DFlash pueden activar `--spec-draft-adaptive` desde el editor
de perfiles. `n-min` queda fijo y el tuner busca el techo `n-max` en un rango
válido (desde `n-min` hasta 9); la capacidad debe aparecer en el `--help` del
binario. El modo queda desactivado por defecto y un binario sin esa capacidad
produce un error bloqueante en la vista previa, en lugar de arrancar con una
configuración fija que no refleja lo pedido.

Parámetros (`startAutoTune(launchProfileId, maxTrials, qualityGate, nPredict)`):
default `24, 0.6, 256`.

### Benchmark A/B en hardware real

El tuner valida candidatos dentro de LlamaCode, pero para medir adaptive en
hardware real conviene reiniciar el server por configuración y repetir las
mismas tareas en el control fijo y en el modo adaptive del mismo binario
parcheado. El script reproducible hace eso y registra `promptMs`, `decodeTps`,
`draftN`, `draftAccepted`, wall time y un chequeo de salida por tarea. No es,
por sí solo, una comparación entre dos binarios distintos:

```powershell
tools\benchmark_adaptive_speculation.ps1 `
  -Server D:\Models\llamacpp\adaptive\llama-server.exe `
  -Model D:\Models\qwen\Qwen3.8-27B-Q4_K_M.gguf `
  -Mmproj D:\Models\qwen\mmproj-BF16.gguf `
  -Template C:\Users\<user>\AppData\Local\LlamaCode\LlamaCode\chat-templates\qwen38-tools-fixed.jinja `
  -Passes 2 -Output .\benchmarks\adaptive-qwen38.json
```

Por defecto compara baseline fijo `n-max=3` contra adaptive `3..5`, `3..7`,
`3..8` y `3..9`. Se puede ajustar la matriz con `-FixedNMax` y
`-AdaptiveNMax`. El script corta antes de medir si `--help` no declara tanto
`--spec-draft-adaptive` como `--spec-draft-n-min`, y conserva logs temporales
del proceso cuando una configuración no llega a healthy.

---

## Tests

Sin modelo real (CI-friendly):

- `tools/tuner_selftest.cpp` — core puro. Objetivo sintético donde el quant KV
  bajo es más rápido pero peor: verifica que **con gate** no colapsa a q4_0 y
  **sin gate** sí (contraste vs llama-launcher).
- `tools/tuner_engine_selftest.cpp` — primitivas (`composeArgs`/`parseThroughput`/
  `scoreQuality`/`tunedArgs`) + medición HTTP real contra un mock `QTcpServer`
  que imita `llama-server`.

Compilar y correr ambos:

```powershell
tools\build_tuner_tests.ps1   # MSVC 2022 + Qt 6.8.3; engine necesita /Zc:__cplusplus
```

---

## Validación local multi-GPU

En 2× RTX 3090, llama.cpp oficial y Qwen3.6-27B IQ4_XS, con 6.618 tokens de
prompt, contexto 32k, `batch=2048`, `ubatch=512` y el resto idéntico:

| split | PP tok/s | TG tok/s |
|-------|---------:|---------:|
| layer | 664,65 | 32,09 |
| tensor | 1.108,39 | 29,16 |

En esa versión `tensor` mejoró PP 66,8% y redujo TG 9,1%. La dirección difiere
de reportes de otras builds, lo que confirma que debe medirse por binario,
modelo y carga en vez de fijar una opción global.

## Pendiente

- Afinar espacio de búsqueda / `nPredict` / prompt de medición según hardware.
- Elegir corpus PPL y umbral desde UI avanzada.
