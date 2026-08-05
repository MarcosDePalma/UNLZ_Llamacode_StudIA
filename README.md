<p align="center">
  <img src="https://github.com/JonatanBogadoUNLZ/PPS-Jonatan-Bogado/blob/9952aac097aca83a1aadfc26679fc7ec57369d82/LOGO%20AZUL%20HORIZONTAL%20-%20fondo%20transparente.png?raw=true" alt="Universidad Nacional de Lomas de Zamora — Facultad de Ingeniería" width="520">
</p>

<h1 align="center">UNLZ_Llamacode</h1>

<p align="center">
  <strong>Universidad Nacional de Lomas de Zamora — Facultad de Ingeniería</strong><br>
  Proyecto institucional · Práctica Profesional Supervisada / Investigación aplicada
</p>

<p align="center">
  🇦🇷 <strong>Español</strong> (este documento) ·
  🇬🇧 <a href="README.en.md">English</a>
</p>

---

> **Proyecto institucional de la Universidad Nacional de Lomas de Zamora (UNLZ), Facultad de Ingeniería.**
>
> UNLZ_Llamacode es una estación de trabajo de IA local: una app nativa de escritorio
> (Qt/QML + C++) que, en hardware propio y sin depender de la nube, abarca **chat
> integrado** con historial persistente, **harness de agente de código** sobre
> repositorios locales, **lanzamiento del servidor** de modelos `llama.cpp`
> (multi-binario / multi-GGUF roots / perfiles compuestos), **backends cloud con
> secretos cifrados**, **modo Charla** (voz-a-voz STT/TTS), **memoria/RAG y
> verificación de afirmaciones**, **maestro/supervisor (escalado)**, **cuentas de
> correo**, **automatización de browser (Playwright)**, **adjuntos/documentos +
> visión**, **Tasks** (macros semánticas + scheduler cron) y **watchdog + medidor
> de VRAM en vivo**.
>
> Se desarrolla como base de trabajo académico y de investigación, pensado para
> docencia, experimentación con LLMs locales y trabajo de becarios/tesistas de la
> Facultad.

## Índice

- [Instalación ultra-rápida](#instalación-ultra-rápida-banco-de-pruebas-aislado)
- [Qué es](#qué-es) · [Privacidad y datos locales](#privacidad-y-datos-locales) · [Hardware recomendado](#hardware-recomendado) · [Estado actual](#estado-actual)
- [Objetivo](#objetivo) · [Foco diferencial](#foco-diferencial)
- [Arquitectura](#arquitectura)
- [Diseño Multi-llama.cpp](#diseño-multi-llamacpp) · [Multi-GGUF roots](#diseño-multi-gguf-roots) · [Multi-perfiles](#diseño-multi-perfiles-compuestos)
- [Cookbook de modelos (hardware-fit)](#cookbook-de-modelos-recomendaciones-hardware-fit)
- [Chat integrado](#chat-integrado) · [Harness de Agente](#harness-de-agente-opencode) · [Lanzamiento del servidor](#lanzamiento-del-servidor-launchpage)
- [Backends cloud + secretos](#backends-cloud--secretos-cifrados) · [Modo Charla (voz)](#modo-charla-voz-a-voz) · [Memoria/RAG](#memoria-rag-y-verificación) · [Maestro/supervisor](#maestro--supervisor-escalado)
- [StudIA (asistente de estudio)](#studia-asistente-de-estudio-sobre-corpus-académico)
- [Correo](#cuentas-de-correo) · [Browser (Playwright)](#automatización-de-browser-playwright) · [Adjuntos/visión](#adjuntos-documentos--visión) · [Watchdog + VRAM](#robustez-del-server-watchdog--vram) · [Otras capacidades](#otras-capacidades)
- [Process Lifecycle](#process-lifecycle) · [Stack técnico](#stack-técnico) · [Build](#build) · [Estructura del repo](#estructura-del-repo)
- [Fases](#fases) · [Tasks (macros + scheduler)](#tasks-macros-configurables--scheduler-cron) · [Benchmarking](#benchmarking) · [Auto-tuning](#auto-tuning-de-parámetros) · [Seguridad operativa](#seguridad-operativa)
- [Agradecimientos](#agradecimientos)

## Instalación ultra-rápida (banco de pruebas aislado)

Un solo comando: instala todas las dependencias, clona el repo en una carpeta
aislada, compila y arranca. No requiere clonar a mano ni preparar el entorno.

**Windows** (PowerShell):

```powershell
irm https://raw.githubusercontent.com/guideahon/UNLZ_Llamacode/main/scripts/bootstrap.ps1 | iex
```

**Linux** (bash):

```bash
curl -fsSL https://raw.githubusercontent.com/guideahon/UNLZ_Llamacode/main/scripts/bootstrap.sh | bash
```

Instala automáticamente:

- **git, CMake, Ninja, Python** y el toolchain C++ — MSVC v143 (Build Tools
  2022) en Windows / `g++` + `build-essential` en Linux.
- **Qt 6.8.3** vía `aqtinstall` en ambas plataformas (Windows `msvc2022_64`,
  Linux `gcc_64`), incluyendo módulos requeridos como `qtmultimedia` y `qtsvg`.
  En Linux se usa aqt — **no** los paquetes Qt de la distro —
  porque el código requiere Qt ≥ 6.5 (`QQmlApplicationEngine::loadFromModule`) y
  varias LTS traen Qt viejo (Ubuntu 24.04 = 6.4.2). De la distro sólo salen el
  toolchain y las libs de sistema contra las que Qt enlaza (GL, xcb, glib,
  fontconfig…).

Clona en `%USERPROFILE%\LlamaCode` / `~/LlamaCode` y al terminar lanza la app
(salvo `LC_NORUN=1`). En Windows también crea un acceso directo por usuario en
`%APPDATA%\Microsoft\Windows\Start Menu\Programs\LlamaCode.lnk`, para que aparezca
al buscar "LlamaCode" desde el menú Inicio.

Variables opcionales (setear antes de correr):

| Var | Default | Qué hace |
|---|---|---|
| `LC_DIR` | `~/LlamaCode` | carpeta de instalación aislada |
| `LC_BRANCH` | `main` | rama a clonar |
| `LC_CONFIG` | `Release` | `Release` o `Debug` |
| `LC_QTVER` | `6.8.3` | versión de Qt (sólo Linux) |
| `LC_QTROOT` | `~/Qt` | raíz de instalación de Qt (sólo Linux) |
| `LC_NORUN` | (vacío) | `1` = no lanzar al terminar |

Ejemplo con overrides (Linux):

```bash
LC_DIR=/opt/llamacode LC_CONFIG=Debug LC_NORUN=1 \
  bash -c "$(curl -fsSL https://raw.githubusercontent.com/guideahon/UNLZ_Llamacode/main/scripts/bootstrap.sh)"
```

Requisitos mínimos previos: **Windows** necesita `winget` (App Installer de la
Microsoft Store). **Linux** soporta apt / dnf / pacman / zypper y pide `sudo`
para los paquetes de sistema. Validado en contenedor Ubuntu 24.04 limpio
(toolchain + aqt Qt 6.8.3 + build).

---

## Qué es

UNLZ_Llamacode es una app nativa (Qt/QML + C++) para orquestar múltiples backends `llama.cpp`, gestionar sesiones de chat, y ejecutar harnesses de agente IA (opencode, aider) sobre repos locales.

Principio central:
- La GUI **no** embebe `llama.cpp`.
- La GUI **orquesta binarios externos** (`llama-server.exe`, forks MTP, builds CUDA/Vulkan/CPU).
- La GUI **compone perfiles** reutilizables sobre binarios, modelos y presets.
- La GUI **integra harnesses de agente** (opencode) vía HTTP API nativa.

## Privacidad y datos locales

UNLZ_Llamacode está diseñado como estación local-first: la GUI, los perfiles, el
catálogo de modelos, el historial de chat/agente y los procesos `llama-server`
corren en la máquina del usuario. El proyecto también soporta integraciones
externas opcionales; por eso la privacidad depende del perfil y de las funciones
que se activen en cada sesión.

### Qué permanece local

| Dato / proceso | Ubicación o alcance | Sale de la máquina por defecto |
|---|---|---|
| Modelos GGUF | Carpetas registradas en Model Roots | No |
| Perfiles y presets | `AppLocalData/LlamaCode/profiles/` | No |
| Historial de chat | `AppLocalData/LlamaCode/chat/` | No |
| Tasks programadas | `AppLocalData/LlamaCode/tasks/` | No |
| Resultados de benchmark | `AppLocalData/LlamaCode/benchmarks/` | No |
| Estado de procesos | `AppLocalData/LlamaCode/services.json` | No |
| Secretos | SecretStore del sistema o referencias a env vars | No se guardan en JSON del repo |

### Cuándo hay tráfico externo

- **Descarga de modelos**: si se usa la cola de descargas, la app contacta el
  proveedor del modelo configurado (por ejemplo Hugging Face).
- **Backends cloud**: si un `BackendProfile` apunta a OpenAI, OpenRouter, Groq,
  DeepSeek u otro endpoint OpenAI-compatible, los mensajes enviados a ese perfil
  salen hacia ese servicio.
- **Búsqueda, Deep Research y verificación web**: las consultas y URLs necesarias
  se envían a motores de búsqueda o sitios externos cuando esas funciones están
  habilitadas.
- **Correo**: IMAP/POP3/SMTP conectan contra el proveedor configurado; enviar un
  mail es una acción externa irreversible.
- **STT/TTS cloud**: el modo Charla puede usar endpoints locales o remotos. Si se
  configura un proveedor remoto, el audio/texto viaja a ese proveedor.
- **Browser automation**: Playwright puede navegar sitios externos por pedido del
  usuario o de una Task.

### Nota de seguridad

La API local, los procesos lanzados y los archivos de configuración viven bajo la
cuenta del usuario del sistema operativo. Otros procesos ejecutándose con esa misma
cuenta pueden interactuar con recursos locales si tienen permisos suficientes. Para
trabajo sensible, usar perfiles locales, revisar los toggles de herramientas
externas y mantener las aprobaciones activas para shell, correo, browser y acciones
destructivas.

## Hardware recomendado

LlamaCode no ata el proyecto a un único modelo: indexa GGUFs compatibles con
`llama.cpp`, estima memoria y recomienda opciones según RAM/VRAM disponible. La
tabla sirve como punto de partida práctico; el rendimiento real depende del modelo,
quant, contexto, batch, backend y temperatura del equipo.

| Hardware disponible | Modo típico | Modelos/quant sugeridos | Contexto orientativo | Expectativa |
|---|---|---|---|---|
| CPU + 16 GB RAM | `cpu_only` | 3B–7B `Q4_K_M` / `Q5_K_M` | 4k–8k | Funcional para pruebas y chat liviano |
| CPU + 32 GB RAM | `cpu_only` | 7B–14B `Q4_K_M` | 8k–16k | Mejor calidad, menor velocidad |
| GPU 6–8 GB VRAM + 16 GB RAM | `gpu` o `partial_offload` | 7B–9B `Q4_K_M`, modelos coder compactos | 8k–16k | Buen punto de entrada para agente local |
| GPU 12 GB VRAM + 32 GB RAM | `gpu` | 9B–14B `Q4_K_M` / `Q5_K_M` | 16k–32k | Recomendado para uso diario |
| GPU 16 GB VRAM + 32–64 GB RAM | `gpu` | 14B–32B cuantizados, MoE chicos | 16k–32k | Agente y RAG más estables |
| GPU 24 GB+ VRAM + 64 GB RAM | `gpu` | 32B+ cuantizados o quants altos | 32k+ | Mejor margen para contexto largo y multitarea |

El modo `partial_offload` permite combinar VRAM y RAM cuando el modelo no entra
completo en la GPU, a costa de velocidad. Para notebooks o equipos con poca
memoria, conviene empezar con contexto 8k, `Q4_K_M` y cerrar procesos pesados antes
de lanzar benchmarks o Deep Research.

## Estado actual

**P0–P4 completo y funcionando.**

| Componente | Estado |
|---|---|
| `BinaryRegistry` + `CapabilityDetector` | ✅ |
| `ModelRootRegistry` + `GGUFScanner` | ✅ |
| `ModelCatalog` (SQLite) | ✅ |
| `ProfileManager` (6 entidades) | ✅ |
| `EffectiveProfileBuilder` | ✅ |
| Importador de perfiles desde args CLI | ✅ |
| Start/Stop server (QProcess + Job Object, async stop) | ✅ |
| Chat streaming integrado (P4) | ✅ |
| Historial de chats con proyectos | ✅ |
| Harness opencode via HTTP API (P3) | ✅ |
| Vista Agente (chat bubbles) + Vista terminal | ✅ |
| Historial de sesiones opencode con proyectos | ✅ |
| Process lifecycle (Job Object + PID file) | ✅ |
| `LlamaProcessManager` dedicado | ⏳ P1 refactor |
| Endpoint health check automático | ✅ (polling /health post-start) |
| Pre-check colisión de puerto al iniciar server | ✅ |
| Popup de primer inicio (binario + modelo + perfil automático) | ✅ |
| Detector de nueva versión (flag remoto + popup con changelog) | ✅ |
| Agente nativo (LlamaAgentBackend, ReAct + tools + MCP) | ✅ P5 |

## Objetivo

Launcher serio para `llama-server`, evolucionado a centro de mando de agentes de código con chat integrado e historial persistente.

## Foco diferencial

- **Multi-llama.cpp**: convivir con varias builds/forks sin fricción.
- **Multi-GGUF roots**: indexar varias carpetas/discos de modelos.
- **Multi-perfiles compuestos**: mezclar `Backend + Model + Runtime + Harness + Workspace`.
- **Chat persistente**: historial de conversaciones agrupado por proyecto/perfil.
- **Agente integrado**: opencode via HTTP API sin subproceso por mensaje, con sesiones y proyectos.

## Arquitectura

```text
LlamaCode
├── UI Layer (Qt Quick / QML)
│   ├── Main.qml (ApplicationWindow + NavBar)
│   ├── pages/
│   │   ├── BinariesPage.qml
│   │   ├── ModelRootsPage.qml
│   │   ├── ProfilesPage.qml      ← import desde args CLI
│   │   ├── LaunchPage.qml
│   │   ├── ChatPage.qml          ← chat streaming + historial + proyectos
│   │   └── AgentPage.qml         ← Vista Agente + Vista terminal + sesiones
│   └── components/
│       ├── LcButton, LcTextField, LcDialog
│       ├── NavBar, PageHeader
│       └── CommandPreview
├── AppController (singleton → QML "App")
│   ├── Chat session management   ← JSON local, agrupado por launchProfile
│   ├── Agent session management  ← opencode HTTP API + SSE
│   └── Process lifecycle         ← Job Object + PID state file + orphan kill
├── Backend Manager
│   ├── BinaryRegistry
│   ├── CapabilityDetector
│   ├── ProfileManager            ← 6x ProfileListModel<T> + JSON
│   └── EffectiveProfileBuilder
├── Model Manager
│   ├── ModelRootRegistry
│   ├── GGUFScanner
│   └── ModelCatalog (SQLite)
└── Storage (AppLocalDataLocation)
    ├── binary_registry.json
    ├── model_roots.json
    ├── model_catalog.db
    ├── profiles/{backends,models,runtimes,...}.json
    ├── services.json             ← PID state para orphan detection
    ├── chat/{index.json, *.json} ← sesiones de chat persistidas
    ├── tasks/tasks.json          ← Tasks (macros) + su programación cron
    └── benchmarks/               ← caché del benchmark de calidad + resultados de corridas
```

## Diseño Multi-llama.cpp

### Binary Registry

Entidad `LlamaBinary`:
- `id`, `name`, `path`, `flavor` (`official`, `mtp-fork`, `custom`)
- `backend` (`cuda`, `vulkan`, `cpu`, `metal`)
- `versionHint` (texto libre)
- `supportedFlags`, `conflictingFlags`, `flagAliases`
- `envDefaults`, `workingDirectory`, `binaryHash` (SHA256 primer 1MB)
- `pathValid` (validado en runtime)

### Capabilities Matrix

Cada binario mantiene flags soportados, aliases y conflictos. `EffectiveProfileBuilder.addFlag()` degrada con `warning` o emite `blockingError` según criticidad.

## Diseño Multi-GGUF roots

### Model Root Registry

Entidad `ModelRoot`: `id`, `path`, `label`, `scanMode` (manual/startup/watch), `enabled`, `priority`, `tags`, `isOnline`.

### Catálogo de modelos (SQLite)

Entidad `CatalogModel`: `id`, `rootId`, `absolutePath`, `fileName`, `sizeBytes`, `mtime`, `familyHint`, `quantHint`, `isVisionCandidate`, `isDraftCandidate`, `isAvailable`, `sha256`.

### GGUFScanner

- Escaneo async via `QtConcurrent::run`
- Infiere familia (deepseek, llama, mistral, phi, qwen, gemma...) por regex sobre nombre
- Infiere quant (`Q4_K_M`, `IQ3_XS`, `BF16`...) por regex
- `isDraftCandidate`: contiene "draft"/"small" OR tamaño < 2GB

## Cookbook de modelos (recomendaciones hardware-fit)

`ModelRootsPage` recomienda qué modelos descargar según el hardware detectado (RAM / VRAM / GPU vía `nvidia-smi`), usando el catálogo `assets/hwfit/hf_models.json` (~900 modelos, basado en el cookbook de Odysseus) y señales de ranking externas embebidas para el modo offline.

La lista de descarga se limita a modelos **GGUF compatibles con llama.cpp**. Entradas
MLX/AWQ/GPTQ/EXL2 del catálogo se filtran para no ofrecer repos que requieren otro
runtime o no tienen archivo `.gguf` descargable por la app. Además, se agregan picks
curados recientes (por ejemplo `Qwen3.5-9B-GGUF`) cuando el catálogo base no trae una
fuente GGUF explícita.

### Scoring

Cada modelo recibe un score `0–100` que combina, ponderado al caso de uso *general* (calidad 0.55 / velocidad 0.15 / fit 0.15 / contexto 0.10 / fuentes 0.05):

- **Calidad** — preferentemente un **benchmark real** (Artificial Analysis *Intelligence Index*, remapeado a 0–100); si no hay match, heurística por params + familia + bonus de arquitectura (qwen3.6 +9, qwen3.5 +8, qwen3-next +6, …) con penalización por tier de quant. Modelos coder se penalizan en el scan general para no dominar.
- **Velocidad** — t/s estimados según ancho de banda de la GPU y params activos (MoE-aware). En `partial_offload` la velocidad es un blend armónico GPU/CPU según la fracción residente en VRAM.
- **Fit** — ratio memoria requerida vs. presupuesto. En `partial_offload`, el
  presupuesto es VRAM + RAM utilizable, no sólo VRAM.
- **Contexto** — target moderno: 32k=100, 16k=85, 8k=70, 4k=50 (no se premia el stub de 4k).
- **Fuentes externas** — prioridad acotada desde `assets/benchmarks/local_cookbook_priorities.json` (WhatLLM local/open-weight + leaderboards HF relevantes) o, si no hay match, popularidad Hugging Face (`hf_downloads` + `hf_likes`) como desempate suave.

Desempate por versión (Qwen3.6 > Qwen3.5).

### Tarjetas destacadas

La franja superior no toma ciegamente los tres primeros del ranking plano. Agrupa
por carriles para que una RTX 3070/3080 de 8 GB vea recomendaciones accionables:

- **General** — default local actual; prioriza familias recientes que entren limpias
  en VRAM, como Qwen3 8B Q4 a 32k.
- **Reasoning** — modelos razonadores compactos que entran en 8 GB, por ejemplo
  DeepSeek-R1-Distill-Qwen-7B Q4.
- **Código** — modelos instruct/code cuando el catálogo marca capacidad de coding.

Si un carril no tiene candidato usable, se completa con el siguiente mejor modelo
del ranking que no sea duplicado.

### Cola de descargas

Las descargas de modelos se agregan a una cola serial. Cada item puede pausarse,
reanudarse, reordenarse o cancelarse desde la UI. La pausa conserva el archivo
`.part` y al reanudar intenta continuar con `Range`; si el servidor no acepta
reanudar, reinicia la descarga parcial para no corromper el GGUF.

### Estimación de memoria (`estimateCatalogMemoryGb`)

El estimador usa primero el footprint curado del catálogo (`recommended_ram_gb`) cuando existe, porque representa el tamaño operativo esperado del GGUF recomendado. Si falta ese dato, usa un fallback sintético:

- **Pesos** — params totales × bytes-por-param del quant.
- **KV cache** — escala con params y contexto real de sizing; constante conservadora `7.5e-6 GB/token/B`.
- **Overhead** — compute graph de llama.cpp + buffers MTP/draft (`0.7 GB + 5%` de los pesos).
- **Contexto de sizing** (`sizingContext`) — target 32k, capeado por el ctx máx del modelo, piso 8k.

### Modos de ejecución (run mode / fit)

Calculado contra VRAM (`nvidia-smi`) y RAM del sistema (90% utilizable como headroom):

| Modo | Condición | Notas |
|---|---|---|
| `gpu` | entra en VRAM | todo en GPU |
| `partial_offload` | no entra en VRAM, sí en VRAM+RAM | spill VRAM+RAM (llama.cpp `-ngl` parcial); `gpuFraction = vram/required` |
| `cpu_only` | sin GPU, entra en RAM | todo en RAM |
| `no_fit` | no entra en VRAM+RAM | — |

### Benchmark de calidad (Artificial Analysis)

- **Tabla bundled** `assets/benchmarks/aa_intelligence.json` — piso offline, sin dependencias de red.
- **Refresco semanal**: si la caché (`AppLocalData/LlamaCode/benchmarks/`) tiene >7 días, hace un fetch en background y la sobrescribe; ante cualquier fallo de red/JSON, queda la bundled.
- **Matching**: `benchmarkKey()` normaliza el nombre del catálogo (saca provider, quant/formato, GGUF, `-4bit`, `instruct`/`it`/`base`…) para mapear contra la tabla.
- **Prioridades de cookbook local** `assets/benchmarks/local_cookbook_priorities.json` — hints curados desde WhatLLM (local/self-host y open-weight) y Hugging Face Spaces de leaderboards trending. No reemplazan el fit local: sólo agregan un boost acotado cuando el modelo también entra bien en el hardware.

## Diseño Multi-perfiles compuestos

| Entidad | Qué define |
|---|---|
| `BackendProfile` | host / port / binario / base args |
| `ModelProfile` | modelo principal + mmproj + draft |
| `RuntimePreset` | ctx / batch / threads / gpu-layers / flash-attn / cache |
| `HarnessProfile` | adapter / args / env de harness externo |
| `WorkspaceProfile` | cwd / políticas / permisos de shell |
| `LaunchProfile` | composición de los 5 anteriores + overrides |

Los perfiles nuevos creados desde la UI seleccionan `LlamaAgent` como harness
por defecto. Al duplicar un `LaunchProfile`, se conserva explícitamente la
selección de harness del perfil original.

### Importador de perfiles desde CLI

Pegar un comando de terminal (e.g. `llama-server --model ... --ctx-size 8192 --n-gpu-layers 99`) y UNLZ_Llamacode extrae y configura automáticamente todos los parámetros reconocidos.

## Chat integrado

- **Chat streaming** directo al `llama-server` vía `/v1/chat/completions` SSE
- **Sesiones persistidas** en JSON local (`AppLocalData/LlamaCode/chat/`)
- **Agrupadas por proyecto** (launch profile activo al crear la sesión)
- **Thinking apagado por defecto**: Chat muestra un toggle propio `Pensar`
  cuando el servidor está listo. Es independiente del toggle de Agente /
  Benchmark / Research y envía `reasoning_budget=0` /
  `chat_template_kwargs.enable_thinking=false` salvo que el usuario lo active.
  Si el modelo emite `<think>` igualmente, Chat descarta ese bloque en streaming
  y no lo guarda en el historial.
- **Indicador "⏳ Procesando..."** mientras espera, cursor `▌` durante generación
- **Streaming estable**: durante la generación se actualiza sólo la burbuja activa,
  sin reconstruir toda la lista de mensajes, para evitar saltos verticales.
- **Stop de generación** con guardado de lo recibido

## Harness de Agente (opencode)

- **Integración HTTP nativa**: comunica con opencode server vía REST + SSE, sin subproceso `opencode run` (elimina conflicto de DB SQLite en Windows)
- **Vista Agente**: chat bubbles con streaming en tiempo real
- **Thinking real por servidor**: el toggle `Pensar` del agente se aplica al
  arranque de `llama-server` con la mejor estrategia compatible con el binario y
  el modelo: `--reasoning on/off` en builds actuales, `--reasoning-budget` como
  fallback, o `--chat-template-kwargs {"enable_thinking":...}` en templates Qwen
  antiguos. Cambiarlo con el servidor ya iniciado requiere reiniciar el servidor
  para que el modelo deje de generar tokens de razonamiento.
- **Vista terminal**: log raw para debug
- **Sesiones opencode**: historial persistido en opencode DB, agrupado por directorio/proyecto
- **Resume automático**: retoma la última sesión al reiniciar el agente
- **Títulos auto-generados**: actualización en tiempo real vía `session.updated` SSE

## Backends cloud + secretos cifrados

Aunque el foco es 100% local, cada perfil puede apuntar a un **endpoint OpenAI-compat
externo** (OpenAI, OpenRouter, Groq, DeepSeek, etc.) en vez de a un `llama-server`
propio. `BackendProfile.kind = "cloud"` no lanza proceso ni binario: el chat/agente
pegan directo al `cloudBaseUrl` con el modelo configurado.

- **SecretStore**: las API keys **nunca** se serializan en los JSON del repo. El
  perfil guarda una **referencia** (`cloudKeyRef`) y el valor se resuelve en runtime
  vía variable de entorno o store cifrado en disco — **QtKeychain** (Secret Service /
  WinCred / macOS Keychain) y, si no está disponible, fallback **DPAPI** en Windows.
- Aplica igual a los maestros HTTP, cuentas de correo y proveedores de voz.

## Modo Charla (voz-a-voz)

Hablar con la IA y escuchar la respuesta, manos libres. Sección **🎙 Charla** en la
NavBar (reusa el backend de chat: sesiones e historial incluidos).

- **STT y TTS** van por endpoints **OpenAI-compat** (`/v1/audio/transcriptions`,
  `/v1/audio/speech`). Una sola ruta de código: **local** (whisper.cpp server,
  openedai-speech, piper-http en localhost, sin key) o **cloud** (URL remota +
  keyRef). Configurable por separado para STT y TTS.
- **Captura** PCM16 mono 16 kHz (`QAudioSource`) con **VAD por energía RMS** (fin de
  turno por silencio configurable), **selección de micrófono** y **medidor de nivel**
  en vivo. Botón *Probar micrófono* para validar entrada sin servidor.
- **Barge-in**: interrumpir el TTS al detectar voz nueva. Máquina de estados
  `escuchando → transcribiendo → pensando → hablando` con auto-escucha opcional.

## Memoria, RAG y verificación

El agente nativo no solo lee archivos: mantiene memoria y conocimiento estructurado.

- **MemoryStore por capas**: hechos durables extraídos de las conversaciones
  (consolidación en background al dejar una sesión) + memoria por proyecto en archivo.
- **GraphStore**: grafo de entidades/relaciones para conocimiento estructurado.
- **AgentEventLog**: bitácora append-only por proyecto (`.llamacode/agent_events.jsonl`)
  con eventos tipados de turnos, tool calls, resultados, fallos y alternativas
  rechazadas. Sirve como evidencia operacional: no reemplaza memoria ni grafo, los
  alimenta con un rastro auditable de qué intentó el agente y por qué algo se
  aceptó, falló o se descartó.
- **Tools**: `hybrid_search` (búsqueda híbrida léxica+semántica), `verify_claims`
  (chequeo de afirmaciones), memoria por capas. RAG sobre el material del proyecto.

## StudIA (asistente de estudio sobre corpus académico)

Sección **🎓 StudIA** en la NavBar: chat que responde **sólo con documentación
académica indexada** — apuntes, libros y trabajos prácticos de una carrera — en vez
de con el conocimiento general del modelo. Desarrollado como PPS de la Facultad
(Ing. Mecatrónica, UNLZ).

Es un módulo cerrado: `src/core/studia/` + `qml/pages/StudiaPage.qml` +
`tools/studia/`. No modifica el agente, el chat ni los perfiles; sólo consume el
`llama-server` que ya levantó LlamaCode (la URL se la pasa QML desde
`App.serverBaseUrl`).

### Ingesta (offline, `tools/studia/ingest.py`)

Recorre una carpeta de material, extrae texto (PDF/DOCX/PPTX/XLSX/TXT/MD), lo parte
en fragmentos de ~1200 caracteres con solape y arma un índice **SQLite + FTS5**
(BM25, `remove_diacritics 2` para que "mecanica" encuentre "mecánica"). Sólo lectura
sobre el corpus: nunca modifica los originales.

- Filtra instalaciones de software que se cuelan en el material (`site-packages`,
  `__pycache__`, `*.dist-info`, metadata de paquetes, archivos de bloqueo `~$`).
- Deduplica por huella (md5 de los primeros 4 MB + tamaño).
- Los documentos sin texto extraíble quedan registrados como `necesita_ocr` en vez
  de descartarse, para poder sumarles OCR después sin re-ingestar el resto.
- Re-ejecutable: saltea lo ya procesado y **reintenta lo que falló**, así corregir
  una dependencia que faltaba no obliga a rehacer la corrida entera.
- `tools/studia/estado.py` da el informe del índice y prueba búsquedas;
  `tools/studia/listar_excluidos.py` audita qué quedó afuera y por qué (importa las
  reglas de `ingest.py`, así no se desincronizan).

### Consulta (`StudiaIndex` + `StudiaController`)

Pregunta → recuperación BM25 → prompt con fragmentos numerados → `llama-server` →
respuesta citando `[n]`. Las fuentes se muestran **agrupadas por documento** (un PDF
que aportó tres páginas aparece como `apunte.pdf · pág. 11, 14, 16`, no repetido tres
veces) y son clickeables: abren el original. El selector de materia las lista en
**orden de cursada**, por cuatrimestre — el campo es texto (`"1C".."10C"`), así que
se castea a entero para que `10C` no quede antes que `1C`.

**Abstención determinística**: si la recuperación no encuentra evidencia suficiente,
el controlador responde la frase de abstención y **no llega a llamar al modelo**. La
garantía no depende de que el LLM obedezca el prompt. Tres reglas
(`StudiaIndex::hayEvidencia`):

1. La pregunta debe tener al menos un **término discriminante** — presente en el
   corpus y no tan frecuente como para aparecer en todos lados (`esDiscriminante`,
   tope 1% de los fragmentos con piso absoluto para índices chicos). Una consulta
   como *"¿cuál es el mejor equipo de trabajo?"* no apunta a nada concreto.
2. Score BM25 **normalizado por la cantidad de términos discriminantes** contra un
   umbral configurable. Dividir por el total de términos castigaba a las preguntas
   naturales: *"explicame de qué se trata un motor a inducción"* arrastra relleno que
   no aporta al ranking pero agranda el divisor, y quedaba rechazada aunque el
   material estuviera.
3. Al menos un fragmento del tope debe cubrir **2 o más términos distintos** de la
   pregunta — las consultas ajenas suelen enganchar por una única palabra común.

El umbral default (`-7.0`) se calibró midiendo **30 preguntas escritas en lenguaje
natural** contra un corpus real de 2.690 documentos / 139.000 fragmentos: responde
las 16 cubiertas y se abstiene en las 14 ajenas. La instrucción de abstención del
prompt queda como segunda barrera. **Recalibrar `umbralAbstencion` con cada corpus
nuevo**: los valores de BM25 dependen del tamaño del índice.

## Maestro / supervisor (escalado)

Cuando el modelo local se traba, el agente puede **escalar** el sub-problema a un
modelo o CLI más capaz. Config por `LaunchProfile` (o fallback global).

- **Cadena de fallbacks** ordenada: tipo `profile` (otro perfil del propio
  LlamaCode), `http` (endpoint OpenAI-compat con keyRef) o `cli` (`claude-code` /
  `codex` detectados en el sistema).
- Escalado **manual** (botón), **auto** (tras N fallos de la misma firma de tool) o
  **ambos**, con anti-recursión por firma. Tool `ask_teacher` para el agente.

## Cuentas de correo

Cliente minimalista SMTP (enviar) + IMAP/POP3 (recibir) sobre sockets, con tools
`email_*` para el agente. Presets por proveedor (Gmail/Outlook/custom). El password
va a SecretStore (`mail/<name>`), nunca al JSON. `email_send` pide aprobación salvo
que se active *auto-send* (enviar correo es acción externa irreversible).

## Automatización de browser (Playwright)

Toggle global + override por perfil (`browserAutomation` inherit/on/off) que inyecta
el **MCP de Playwright** en el set de tools del agente. **Modo teach**: el usuario
graba acciones con Playwright codegen y se guardan como **skills reproducibles** que
las Tasks pueden reejecutar.

## Adjuntos (documentos + visión)

`DocumentExtractor` convierte adjuntos **pdf/office → markdown** vía sidecar
**markitdown** (con caché por md5), para inyectarlos al contexto del chat/agente. Con
un modelo de visión (server lanzado con `--mmproj`) también acepta **imágenes**.

## Robustez del server (watchdog + VRAM)

- **Watchdog**: auto-restart de `llama-server` ante crash (con tope de reintentos);
  `serverState` = `stopped|running|restarting|failed`.
- **Medidor de VRAM/stats en vivo**: poll async de `nvidia-smi` mientras el server
  corre (`serverStats`), para ver el consumo real.
- **Diagnóstico del log**: detecta por regex OOM, colisión de puerto, modelo cargado,
  etc., y los emite como eventos con nivel.
- **Colisión de puerto recuperable**: si el puerto del perfil está ocupado, la UI
  ofrece un puerto libre, actualiza el `BackendProfile` y relanza usando esa misma
  fuente de configuración.

## Otras capacidades

- **Router mode (hot-swap)**: un único `llama-server` con varios modelos cargados vía
  preset `.ini`; el chat/agente conmutan por el campo `model` del request.
- **GPU power limit**: fija el límite de potencia (W) por GPU vía `nvidia-smi`
  (en Windows se relanza elevado), global o por perfil.
- **Deep Research**: investigación multi-página con reportes persistidos.
- **Integrations**: registro unificado de **MCP Tool Servers** + **API services**
  (endpoint + key), con test de conexión.
- **ControlApi / headless**: toda feature es controlable por API local (target
  traversal), con variantes sin diálogo para automatización.
- **EvalSuite**: evaluación reproducible de modelos (importable como benchmark custom).
- **Mermaid**: render de diagramas en el chat (sidecar mermaid-cli).
- **Multi-idioma**: UI en español, inglés, chino, francés, italiano y alemán.
- **Export/Import/Wipe** de datos de usuario por categorías.

## Lanzamiento del servidor (`LaunchPage`)

- **Vista previa del comando** con botón *Copiar*.
- **Iniciar servidor + agente** — levanta `llama-server` y el harness de agente.
- **Iniciar solo servidor** — solo `llama-server`, sin agente.
- **Puerto ocupado** — antes de iniciar, detecta si el puerto del perfil está en
  uso; si hay otro libre cercano, pregunta si se desea cambiar el perfil a ese
  puerto y recién después lanza.
- **Endpoint OpenAI** — con el server corriendo muestra `http://<host>:<port>/v1` (read-only, seleccionable) + botón *Copiar*, para apuntar agentes externos (opencode, aider, etc.) al backend local.

## Process Lifecycle

- **Windows Job Object**: todos los subprocesos (llama-server + harness) se asignan al Job Object del proceso principal. Al cerrar UNLZ_Llamacode (normal o crash), los hijos mueren automáticamente.
- **Env vars de trazabilidad**: `LLAMACODE_MANAGED=1`, `LLAMACODE_ROLE=server|harness-*`, `LLAMACODE_APP_PID=<pid>` en todos los procesos spawneados.
- **PID state file** (`services.json`): al iniciar, detecta orphans de sesiones anteriores y los mata antes de levantar nuevos procesos.
- **Stop asíncrono**: `stopServer()` no bloquea la UI. Envía `terminate()`, expone `serverStopping` property, muestra "Deteniendo..." en botón y estado. Kill forzado tras 5s si el proceso no termina.

## Stack técnico

- **Qt 6.8.3** (`msvc2022_64`)
- **Qt modules**: Core, Quick, Sql, Concurrent, Network, Widgets, Multimedia, Svg
- **Secretos**: QtKeychain (Secret Service / WinCred / Keychain) con fallback DPAPI
- **Compilador**: MSVC 2022 (VS BuildTools)
- **CMake 3.21+**, generator: Visual Studio 17 2022 (multi-config)
- **QML theme**: Catppuccin Mocha
- **Persistencia**: JSON (registries/profiles/chat) + SQLite (catalog) + QSettings

## Build

### Rápido (recomendado)

`build.bat` mata procesos colgados, configura, compila, despliega el runtime Qt (`windeployqt`) y regenera los accesos directos. Acepta config y la opción `NOPAUSE` para ejecución automatizada:

```bat
build.bat Both NOPAUSE     REM Debug + Release, recomendado para entregar cambios
build.bat Debug NOPAUSE    REM solo Debug
build.bat Release NOPAUSE  REM solo Release
```

Para subir la versión de la app y del flag de actualización:

```bat
bump-version.bat 0.1.2
bump-version.bat 0.1.2 --summary "Resumen corto" --changelog "Cambio A|Cambio B"
```

Salidas:

| Config | Binario | Acceso directo | Icono |
|--------|---------|----------------|-------|
| Release | `build\Release\LlamaCode.exe` (optimizado, `NDEBUG`) | `LlamaCode.lnk` | `assets\app_icon.ico` (llama normal) |
| Debug | `build\Debug\LlamaCode.exe` (símbolos + asserts) | `LlamaCode-Debug.lnk` | `assets\debug_icon.ico` (llama **roja**) |

El icono rojo del Debug va embebido en el `.exe` (taskbar/explorer) vía
`app_icon.rc` + `#ifdef LC_DEBUG_ICON` (CMake define `LC_DEBUG_ICON` solo en
config Debug), y también se usa en el `.lnk`, la ventana principal y el splash.
Esta selección debe depender de la configuración de LlamaCode mediante
`LC_DEBUG_ICON`, no de `QT_DEBUG`: Qt puede ser una build Release aunque la app
se compile en Debug.

> Tras tocar código siempre recompilar — el QML va embebido en el binario vía `qt_add_qml_module`.

### Manual

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\msvc2022_64"
cmake --build build --config Release --parallel
```

### Calidad de código

- `tests.bat Debug` configura `build_tests`, compila y corre toda la suite Qt Test.
- Si `clang-format` está instalado, CMake expone los targets `format` y
  `format-check` usando `.clang-format`.
- `LC_STRICT_WARNINGS=ON` activa `/W4 /permissive-` en MSVC o
  `-Wall -Wextra -Wpedantic` en GCC/Clang. `LC_WARNINGS_AS_ERRORS=ON` permite
  endurecer CI/local cuando la rama está limpia de warnings.
- `LC_ENABLE_CLANG_TIDY=ON` activa `clang-tidy` si el ejecutable está disponible.

### Primera instalación

`install.bat` / `setup.bat` instalan Python deps + Qt 6.8.3 vía `aqtinstall` antes del primer build.

## Estructura del repo

```text
LlamaCode/
├── CMakeLists.txt          ← raíz CMake
├── app_icon.rc             ← recurso de icono (Debug/Release condicional)
├── build.bat / install.bat / setup.bat
├── update-shortcut.ps1     ← genera los .lnk (parametrizable por config/icono)
├── LlamaCode.lnk / LlamaCode-Debug.lnk
├── src/                    ← C++ (AppController, backends de agente, core)
├── qml/                    ← UI (Main.qml, pages/, components/)
├── assets/
│   ├── app_icon.ico / debug_icon.ico / app_icon.png
│   ├── hwfit/hf_models.json          ← catálogo de modelos (cookbook)
│   └── benchmarks/aa_intelligence.json ← scores de calidad (offline)
├── docs/                   ← documentación (agent.md, TODO.md, plan_harness.md, tuner.md, ...)
├── logs/                   ← logs de runtime/install (gitignored)
├── tests/ + build_tests/   ← suite Qt Test
└── build/                  ← artefactos (Debug/ + Release/, gitignored)
```

## Fases

1. **P0** ✅ Launcher multi-binario/multi-modelo base + UI
2. **P1** ✅ (parcial) Validación, ejecución, logs en vivo
3. **P2** ✅ (parcial) UX de perfiles, importador CLI
4. **P3** ✅ Harness opencode via HTTP API + sesiones + proyectos
5. **P4** ✅ Chat integrado streaming + historial persistente + proyectos
6. **P5** ✅ Built-in coding agent nativo (`LlamaAgentBackend`): loop ReAct contra `llama-server`, tools (read/write/edit/grep/glob/list_dir/run_shell/web_fetch/task), MCP stdio, aprobaciones, plan mode, checkpoint/rollback, subagents paralelos en git worktrees, permisos por patrón, @-mentions, imágenes (visión)
7. **P6** ✅ Tasks (macros semánticas configurables) + scheduler cron in-app, con auto ciclo de vida del agente
8. **P7** ✅ Backends cloud + secretos cifrados, modo Charla (voz-a-voz), correo, browser (Playwright/teach), memoria/RAG, maestro/supervisor, watchdog + VRAM, router hot-swap, headless ControlApi

## Tasks (macros configurables + scheduler cron)

Sección **Tasks** (en la NavBar, arriba de Benchmark): macros que el usuario
configura, guarda y ejecuta. **No son macros tontas** — no graban coordenadas
crudas estilo TinyTask, sino que delegan en el agente IA: cada Task guarda un
**objetivo en lenguaje natural** + **pasos de referencia**, y en la ejecución el
agente re-deriva las acciones con sus tools (browser MCP, shell, mail, etc.) y
**se adapta** si un botón, elemento o archivo cambió de lugar o de nombre.

### Modelo de datos (`TaskStore`)

- `id`, `name`, `description` (el objetivo), `profileId` (perfil de agente opcional).
- `steps[]`: cada paso `{kind, intent, ref}` con `kind` ∈
  `instruction|browser|shell|mail|desktop`. Los pasos `browser` graban un skill
  reproducible vía Playwright codegen (reusa el modo *teach* del browser).
- `scheduleEnabled` / `scheduleCron`, `lastRunAt` / `lastRunStatus`.
- Persistencia JSON en `AppLocalData/LlamaCode/tasks/tasks.json`.
- `composePrompt()` arma el prompt-objetivo con la consigna explícita de que los
  pasos son **guía, no guion literal** (replay adaptativo).

### Ejecución (manual o programada)

`runTask()` unifica el botón ▶ y el scheduler con auto ciclo de vida del agente:

- Si el **agente ya corre**, lo usa tal cual (no lo apaga).
- Si **no hay agente**, auto-inicia servidor + agente (perfil de la Task o el
  activo), ejecuta al quedar listo y **lo apaga** al terminar el turno.
- Sin perfil asignable → marca `lastRun = "error"`.

El cierre del ciclo se apoya en la señal `IAgentBackend::turnFinished` (emitida al
completar el turno), que marca `lastRun = "ok"` y apaga el agente auto-iniciado.

### Scheduler cron (`CronSchedule` + `TaskScheduler`)

- Parser cron puro de 5 campos `min hora díaMes mes díaSem`: `*`, listas `a,b`,
  rangos `a-b`, pasos `*/n` y `a-b/n`, día de semana `0`/`7` = domingo, semántica
  OR de díaMes/díaSem cuando ambos están restringidos.
- `TaskScheduler` evalúa por minuto (timer in-app, de-dup por minuto) y dispara
  `runTask` en cada Task vencida. Toggle global persistido; corre mientras la app
  esté abierta.
- Ejemplos: `0 9 * * *` (9:00 diario) · `*/15 9-17 * * 1-5` (cada 15 min, 9–17h,
  lun–vie) · `0 0 1 * *` (día 1 de cada mes).

## Benchmarking

Módulo para comparar quants y perfiles de forma sistemática: mide RAM, VRAM, velocidad y calidad relativa con resultados persistidos en tabla.

### Flujo de uso

1. Seleccionar uno o más `LaunchProfile` para comparar.
2. Elegir modo de prueba: **Corta** (~30 s) o **Completa** (1–5 min).
3. Ejecutar: UNLZ_Llamacode lanza cada perfil en secuencia, corre los prompts, registra métricas.
4. Ver resultados en tabla comparativa; exportar o guardar para comparaciones futuras.

### Modos de prueba

| Modo | Prompts | `n_predict` | Score | Tiempo estimado |
|------|---------|-------------|-------|-----------------|
| **Corta** | 5 fijos | 256 | 0–2 por prompt (máx 10) | ~30 s |
| **Completa** | 15 configurables | 512 | 0–5 por prompt (máx 75) | 1–5 min |

Parámetros fijos en toda corrida: `temp 0`, `top_p 1`, `top_k 0`, seed fijo, `ctx` según perfil.

### Categorías de prompts (modo Completo)

```text
3 × razonamiento lógico
3 × código / debug
3 × redacción técnica / pericial
3 × extracción de datos estructurada
3 × contexto largo (1 000–4 000 tokens de entrada)
```

Los prompts son editables y persistidos; el usuario puede reemplazarlos con casos reales (logs llama.cpp, pericias, SQL, Airflow, expedientes judiciales, etc.).

### Scoring

```text
Modo Corta:  0 = falla  /  1 = aceptable  /  2 = buena
Modo Completa:
  5 = igual o mejor que baseline
  4 = leve pérdida, usable
  3 = correcto pero menos preciso
  2 = error importante
  1 = falla grave
  0 = no siguió la consigna
```

Calidad relativa normalizada contra el perfil baseline (el de mayor score):

```
calidad_relativa = score_perfil / score_baseline × 100
```

### Métricas registradas por corrida

```text
perfil / modelo / quant
RAM usada (MB)
VRAM usada (MB)
tokens/s — prompt eval
tokens/s — generation
tiempo total (s)
score corto / score completo
errores graves (count)
```

### Persistencia y vista

- Resultados en JSON (`AppLocalData/LlamaCode/benchmarks/{timestamp}.json`).
- Vista tabla en `BenchmarkPage.qml`: columnas ordenables, filtro por perfil/quant/fecha.
- Exportar a CSV desde la UI.

### Tabla de ejemplo

| Quant | Score | Δ baseline | t/s gen | RAM | VRAM |
|-------|-------|------------|---------|-----|------|
| Q8_0 | 92/100 | base | 20 | 2 GB | 28 GB |
| Q6_K | 90/100 | −2.2% | 25 | 2 GB | 22 GB |
| Q5_K_M | 86/100 | −6.5% | 30 | 2 GB | 18 GB |
| Q4_K_M | 80/100 | −13.0% | 38 | 2 GB | 14 GB |
| IQ4_XS | 77/100 | −16.3% | 42 | 2 GB | 12 GB |
| Q3_K_M | 65/100 | −29.3% | 55 | 2 GB | 9 GB |

## Auto-tuning de parámetros

Búsqueda automática de los flags de `llama-server` (`ngl`, `batch`, `ubatch`,
`flash-attn`, `cache-type-k/v`) que maximizan **tok/s** sin degradar la
**calidad**. Optimizador TPE-lite (Parzen discreto) con **gate de calidad** y
validación PPL opcional: a diferencia de *llama-launcher v1.3*, tunear el quant
de KV cache solo por velocidad no colapsa al quant más bajo, porque la pérdida
penaliza fuerte caer bajo el umbral. Si existe `llama-perplexity` junto al
binario y hay corpus local, los trials que tocan cache K/V se validan contra la
PPL baseline con tolerancia default del 3%.

- Corre `N` trials en un puerto scratch (lanza/mide/mata el server por candidato, en un `QThread` aparte para no congelar la UI).
- Mide throughput de `timings.predicted_per_second` (`/completion`) y califica la salida con substrings estilo EvalSuite.
- Modo **Tune CPU**: fuerza `-ngl 0` y explora `threads`, `batch`, `ubatch` y cache K/V para equipos sin GPU.
- Al terminar **clona** el perfil en uno nuevo `-tuned` con la mejor config en `extraArgs`; el original queda intacto.
- UI: `ProfilesPage` → **Auto-tune**, **Tune CPU** / **Cancelar tune** + estado en vivo.

Detalle completo en [`docs/tuner.md`](docs/tuner.md).

## Seguridad operativa

- Nada destructivo sin aprobación explícita.
- Escrituras fuera de workspace: bloqueadas por defecto.
- Comandos shell con allowlist/denylist por `WorkspaceProfile`.
- Subprocesos tagged con env vars para auditoría y control de ciclo de vida.

## Agradecimientos

Código, datos y diseño tomados de otros proyectos:

| Proyecto | Uso en UNLZ_Llamacode | Repo / Fuente |
|---|---|---|
| **llama.cpp** | Binarios orquestados (`llama-server`), API OpenAI-compat, formato GGUF | https://github.com/ggml-org/llama.cpp |
| **opencode** | Harness de agente externo (HTTP API + SSE); formato de config MCP `mcp{}` | https://github.com/sst/opencode |
| **aider** | Harness de agente externo soportado | https://github.com/Aider-AI/aider |
| **markitdown** | Sidecar de extracción de documentos (pdf/office → markdown) en `DocumentExtractor` | https://github.com/microsoft/markitdown |
| **Odysseus cookbook** | Base del catálogo hardware-fit `assets/hwfit/hf_models.json` (~900 modelos) | https://github.com/TheBlokeAI/odysseus-cookbook |
| **Artificial Analysis** | Scores de calidad bundled `assets/benchmarks/aa_intelligence.json` (Intelligence Index) | https://artificialanalysis.ai |
| **Playwright (MCP)** | Automatización de browser + codegen (modo teach) | https://github.com/microsoft/playwright-mcp |
| **API de audio OpenAI** | Contrato `/v1/audio/transcriptions` y `/v1/audio/speech` del modo Charla (whisper.cpp, openedai-speech, piper) | https://platform.openai.com/docs/api-reference/audio |
| **QtKeychain** | Cifrado de secretos respaldado por el SO | https://github.com/frankosterfeld/qtkeychain |
| **Catppuccin (Mocha)** | Paleta del theme QML | https://github.com/catppuccin/catppuccin |
| **archex** | Ideas de pipeline de code-context en `hybrid_search`: empaquetado por presupuesto de tokens + expansión por dep-graph (vecinos vía imports/includes). Revisión: [`docs/archex_context_review.md`](docs/archex_context_review.md) | https://github.com/Mathews-Tom/archex |
| **codehamr** | Ideas de robustez local-first para el harness: empaquetado de contexto, invariantes OpenAI-compatible, timeouts SSE por inactividad y errores autocorrectivos de tools | https://github.com/codehamr/codehamr |

> Al sumar código/datos de otro repo, agregar la fila correspondiente acá.
