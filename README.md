<p align="center">
  <img src="https://github.com/JonatanBogadoUNLZ/PPS-Jonatan-Bogado/blob/9952aac097aca83a1aadfc26679fc7ec57369d82/LOGO%20AZUL%20HORIZONTAL%20-%20fondo%20transparente.png?raw=true" alt="Universidad Nacional de Lomas de Zamora — Facultad de Ingeniería" width="520">
</p>

<h1 align="center">UNLZ_Llamacode_StudIA</h1>

<p align="center">
  <strong>Universidad Nacional de Lomas de Zamora — Facultad de Ingeniería</strong><br>
  Fork de <a href="https://github.com/cristianlukas/UNLZ_Llamacode">UNLZ_Llamacode</a> · Agrega el módulo <strong>StudIA</strong> · Práctica Profesional Supervisada 2026 1C
</p>

<p align="center">
  🇦🇷 <strong>Español</strong> (este documento) ·
  🇬🇧 <a href="README.en.md">English</a>
</p>

---

## 🎓 Sobre este fork

Este repositorio es un **fork de [cristianlukas/UNLZ_Llamacode](https://github.com/cristianlukas/UNLZ_Llamacode)**
—la estación de trabajo de IA local de la Facultad— que agrega **StudIA**, un asistente
de estudio que responde preguntas **usando exclusivamente la documentación académica
indexada** (apuntes, libros y trabajos prácticos de la carrera), citando el documento y
la página de cada afirmación y ejecutándose por completo en la máquina del estudiante.

StudIA se desarrolló como **Práctica Profesional Supervisada** de Ingeniería
Mecatrónica (FI-UNLZ).

| | |
|---|---|
| **Autor del módulo** | Marcos De Palma — Ing. Mecatrónica, FI-UNLZ |
| **Rama del desarrollo** | `feature/studia` |
| **Aporte sobre el proyecto base** | 55 archivos · ~14.700 líneas · 7 commits |
| **Documentación de la PPS** | [2026_1C_PPS_Asistente_de_Estudio_DE-PALMA](https://github.com/MarcosDePalma/2026_1C_PPS_Asistente_de_Estudio_DE-PALMA) — informe, cronograma, Gantt y manuales |
| **Qué agrega** | [`src/core/studia/`](src/core/studia/) · [`qml/pages/StudiaPage.qml`](qml/pages/StudiaPage.qml) · [`tools/studia/`](tools/studia/) · [`tests/test_studia.cpp`](tests/test_studia.cpp) · [`installer/`](installer/) |
| **Qué NO modifica** | el agente, el chat, los backends, los servidores de modelos ni el modo voz del proyecto original |

**Documentación del módulo:** [StudIA (asistente de estudio)](#studia-asistente-de-estudio-sobre-corpus-académico) — más abajo en este mismo README.

Ver exactamente qué aporta la PPS sobre el proyecto original:

```
https://github.com/cristianlukas/UNLZ_Llamacode/compare/main...MarcosDePalma:UNLZ_Llamacode_StudIA:feature/studia
```

> El resto de este README es la documentación del **proyecto original**, que se mantiene
> tal cual salvo la sección de StudIA.

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
- [Chat integrado](#chat-integrado) · [Harness de Agente](#harness-de-agente-opencode) · [Corridas administradas](#corridas-administradas-de-claude-code-y-codex) · [Lanzamiento del servidor](#lanzamiento-del-servidor-launchpage)
- [Backends cloud + secretos](#backends-cloud--secretos-cifrados) · [Modo Charla (voz)](#modo-charla-voz-a-voz) · [Memoria/RAG](#memoria-rag-y-verificación) · [Asistente continuo](#asistente-continuo) · [Maestro/supervisor](#maestro--supervisor-escalado)
- [StudIA (asistente de estudio)](#studia-asistente-de-estudio-sobre-corpus-académico)
- [Correo](#cuentas-de-correo) · [Browser (Playwright)](#automatización-de-browser-playwright) · [Data Lab](#data-lab) · [Adjuntos/visión](#adjuntos-documentos--visión) · [Watchdog + VRAM](#robustez-del-server-watchdog--vram) · [Otras capacidades](#otras-capacidades)
- [Process Lifecycle](#process-lifecycle) · [Stack técnico](#stack-técnico) · [Build](#build) · [Estructura del repo](#estructura-del-repo)
- [Fases](#fases) · [Tasks (macros + scheduler)](#tasks-macros-configurables--scheduler-cron) · [Workflows de ingeniería](#workflows-de-ingeniería) · [Benchmarking](#benchmarking) · [Rendimiento multi-GPU](#rendimiento-multi-gpu) · [Auto-tuning](#auto-tuning-de-parámetros) · [Seguridad operativa](#seguridad-operativa)
- [Corridas durables y entregables](#corridas-durables-y-entregables)
- [Agradecimientos](#agradecimientos)

## Instalación ultra-rápida (banco de pruebas aislado)

Un solo comando: instala todas las dependencias, clona el repo en una carpeta
aislada, compila y arranca. No requiere clonar a mano ni preparar el entorno.

**Windows** (PowerShell):

```powershell
irm https://raw.githubusercontent.com/cristianlukas/UNLZ_Llamacode/main/scripts/bootstrap.ps1 | iex
```

**Linux** (bash):

```bash
curl -fsSL https://raw.githubusercontent.com/cristianlukas/UNLZ_Llamacode/main/scripts/bootstrap.sh | bash
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
| `LC_CONFIG` | `Debug` | `Debug` (release candidate) o `Release` (estable) |
| `LC_QTVER` | `6.8.3` | versión de Qt (sólo Linux) |
| `LC_QTROOT` | `~/Qt` | raíz de instalación de Qt (sólo Linux) |
| `LC_NORUN` | (vacío) | `1` = no lanzar al terminar |

Ejemplo con overrides (Linux):

```bash
LC_DIR=/opt/llamacode LC_CONFIG=Debug LC_NORUN=1 \
  bash -c "$(curl -fsSL https://raw.githubusercontent.com/cristianlukas/UNLZ_Llamacode/main/scripts/bootstrap.sh)"
```

Requisitos mínimos previos: **Windows** necesita `winget` (App Installer de la
Microsoft Store). **Linux** soporta apt / dnf / pacman / zypper y pide `sudo`
para los paquetes de sistema. Validado en contenedor Ubuntu 24.04 limpio
(toolchain + aqt Qt 6.8.3 + build).

---

## Qué es

### Índice y preflight de contexto

El agente dispone de un índice local inspirado en Graft y archex. `context_scout`
prepara candidatos por objetivo, rangos exactos, handles y un recibo de frescura y
presupuesto; `context_fetch` valida el hash antes de devolver el código. También
existen `repo_slice`/`hybrid_search` con expansión del grafo y recibo estructurado.
Después de cada `write_file` o `edit_file` se actualizan los chunks y relaciones
afectados, incluso en Release. `GraphStore` conserva citas con ruta, rango y hash;
`graph query` puede devolver un paquete estructurado y `graph doctor` detecta
fuentes obsoletas o edges huérfanos. `KnowledgePacket` combina ese grafo con la
memoria durable, separa decisiones de hechos de apoyo y declara la precedencia
código/tests → decisiones vigentes → memoria verificada → inferencias. El módulo
`knowledge` del `HarnessSpec` controla si entra en preflight. El preflight también
muestra el trabajo activo de otras sesiones y bloquea escrituras sobre rutas
reclamadas por un agente vivo; las claims tienen TTL y viven en
`.llamacode/active_work.json`. La búsqueda estructural no usa servicios externos;
embeddings y reranking siguen siendo opcionales. El preflight se activa por perfil
y los presets `agent-avanzado` y `agent-maximo` lo habilitan de forma automática;
`agent-chat`, `agent-minimal`, RPA y Browser conservan el flujo liviano, mientras
`agent-intermedio-next` sigue siendo comparable con Intermedio. Las mutaciones
indirectas exitosas de `run_shell` y las tools externas que declaran rutas también
disparan una detección incremental de cambios antes de actualizar el contexto.
Las mutaciones shell/MCP deben declarar `changed_paths`; si son opacas, el host
las rechaza antes de ejecutar para preservar coordinación e integridad del índice.
`MemoryStore` usa locks y escrituras atómicas, y aplica un `decay` amortizado que
marca stale sólo hechos viejos/de bajo valor, protegiendo decisiones verificadas.
Las corridas durables sólo pasan a `completed` después de capturar snapshot y
manifiesto de entregables; si el lease vence durante ese cierre, quedan
`uncertain` para resolución humana.
Release conserva el flujo histórico de los perfiles que no declaran preflight hasta
validar el beneficio con benchmarks.
La consolidación reutiliza `verify_claims`: descarta inferencias sin evidencia,
reduce la confianza de las parciales y registra edges automáticos de tool/sesión
para archivos tocados y relaciones decisión→bug, siempre como `unreviewed`.
Ver
[`docs/context-graph.md`](docs/context-graph.md).

UNLZ_Llamacode es una app nativa (Qt/QML + C++) para orquestar múltiples backends `llama.cpp`, gestionar sesiones de chat, y ejecutar harnesses de agente IA (opencode, aider) sobre repos locales.

Principio central:
- La GUI **no** embebe `llama.cpp`.
- La GUI **orquesta binarios externos** (`llama-server.exe`, forks MTP, builds CUDA/Vulkan/CPU).
- La GUI **compone perfiles** reutilizables sobre binarios, modelos y presets.
- La GUI **integra harnesses de agente** (opencode) vía HTTP API nativa.

## Privacidad y datos locales

### Perfiles de personalidad y estilo

Los perfiles de agente pueden asociar artefactos locales reutilizables de
`personality` y `writing-style`. LlamaCode conserva una ficha resumida y
ejemplos acotados, y los inyecta como preferencias de expresión en el system
prompt sin modificar permisos, tools ni guardrails. Se guardan en el directorio
local de perfiles y pueden importarse/exportarse como JSON. Ver
[`docs/personality-style-profiles.md`](docs/personality-style-profiles.md).

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
| Corridas administradas de Claude/Codex | `AppLocalData/LlamaCode/managed-agent-runs/` | El prompt sale sólo al CLI/runtime elegido |
| Resultados de benchmark | `AppLocalData/LlamaCode/benchmarks/` | No |
| Métricas de latencia de voz | `AppLocalData/LlamaCode/voice/latency.jsonl` | No |
| Estado de procesos | `AppLocalData/LlamaCode/services.json` | No |
| Corridas del agente y entregables | `AppLocalData/LlamaCode/<harness>/agent_runs/` y `agent_deliverables/` | No |
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
- **STT local gestionado**: si al iniciar Charla falta el modelo de voz configurado,
  la app solicita confirmación para descargarlo o permite posponer la descarga.
- **Browser automation**: Playwright puede navegar sitios externos por pedido del
  usuario o de una Task.
- **Control del escritorio**: el agente prioriza controles semánticos de Windows
  (UI Automation), usa captura visual sólo cuando aporta información y verifica el
  resultado después de actuar. Los clics visuales aceptan únicamente coordenadas
  normalizadas `0..1`: el schema y el backend rechazan valores fuera de rango antes
  de mover el mouse, evitando confundir una grilla de grounding `0..1000` con
  coordenadas ejecutables. Si el objetivo no aparece con claridad, el agente se
  abstiene en vez de elegir un control parecido. Para canvas, iconos o escritorios
  remotos sin controles semánticos, el agente dispone de `desktop_find_image`,
  `desktop_click_image`, `desktop_wait_image` y `desktop_assert_image`: buscan una
  plantilla en memoria con umbral y escala acotados, rechazan coincidencias ambiguas
  y verifican la geometría antes de actuar. Teach v2/v3 captura automáticamente una
  plantilla cuando un clic no tiene ancla semántica; F8 captura una referencia rápida
  y F9 abre una capa de selección multimonitor para arrastrar una región exacta sin
  enviar ese gesto a la aplicación subyacente (Escape cancela). Tasks permite probar, reemplazar, eliminar
  o agregar variantes claro/oscuro de cada plantilla sin dejar referencias huérfanas.
  El matcher usa OpenCV `matchTemplate` si está disponible al compilar y conserva un
  backend Qt muestreado y acotado como fallback portable. Play resuelve cada
  clic mediante `UI Automation → OCR → plantilla → coordenada normalizada`, restaura
  el estado maximizado o el tamaño exterior de la ventana antes de reproducir
  gestos, evitando que un cambio accidental de tamaño desplace dibujos y clicks.
  Mientras controla ventanas, mouse o teclado se
  muestra durante toda la automatización un indicador siempre visible, un reborde
  independiente en cada monitor y un aro alrededor del puntero; el conjunto puede ocultarse
  desde Configuración > perfiles de agente > Indicador de escritorio.

- **Frugalidad opt-in**: los perfiles de agente pueden activar `Honey`, una política
  YAGNI que prioriza reutilizar código y detenerse en la primera solución mínima
  correcta sin eliminar validaciones, seguridad, tests, accesibilidad ni manejo de
  errores. La acción **Revisar frugalidad** audita el diff actual en modo read-only
  y devuelve métricas y candidatos de sobre-ingeniería para revisión humana.

El probe opt-in `qa_visual_automation` valida búsqueda real, DPI y multimonitor en
una ventana propia. Por defecto no mueve el mouse; `--execute-click --screen N`
habilita el clic E2E explícitamente. No forma parte de `ctest` para no interferir con
el escritorio del usuario ni con runners headless.
`--matrix` recorre automáticamente todos los monitores disponibles, dos objetivos
gráficos y temas claro/oscuro; puede combinarse con `--execute-click` para verificar
también el dispatch foreground de cada caso.
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
| GPU 16 GB VRAM + 32–64 GB RAM | `partial_offload` | KAT Coder 2.5 35B-A3B `Q4_K_M` (`--n-cpu-moe 18`) | 32k | Validado E2E: 11/11 ×3, 3/3 sin reparar y 4,70× más rápido que Qwen3.6 IQ4_XS |
| GPU 24 GB+ VRAM + 64 GB RAM | `partial_offload` | KAT Coder 2.5 35B-A3B `Q4_K_M` (default coding) | 32k | Validado E2E: misma calidad final que Qwen base y ~4× menor tiempo mediano |

El modo `partial_offload` permite combinar VRAM y RAM cuando el modelo no entra
completo en la GPU, a costa de velocidad. Para notebooks o equipos con poca
memoria, conviene empezar con contexto 8k, `Q4_K_M` y cerrar procesos pesados antes
de lanzar benchmarks o Deep Research.

Para **2× RTX 3090 (48 GB agregados)** también queda disponible, sólo como experimento,
`KAT APEX-MTP + Qwen mmproj` (`sys-48-katcoder-mtp-vision`). Usa el GGUF comunitario
`KAT-Coder-V2.5-Dev-MTP-APEX-i-quality-v2.gguf` junto con `mmproj-F16.gguf` de
Qwen3.6-35B-A3B, contexto 32k, KV q8 y MTP2. El KAT oficial es text-only, por lo
que la visión injertada puede perder calidad después del fine-tune y la aceptación
MTP puede ser menor que en Qwen; una baja aceptación sólo reduce velocidad, no la
corrección del modelo target. Incluye controles MTP3 y sin MTP para separar ambos
efectos. Requiere llama.cpp b10331+ y se valida en este orden: texto, imagen sin MTP,
imagen + MTP2 y luego MTP3. No reemplaza al perfil KAT estable ni se descarga junto
con los perfiles automáticos.

Como experimento separado para una **GPU de 16 GB**, el catálogo incorpora tres
candidatos de Qwen3.8-27B basados en el post de LocalLLaMA adjunto: RVN-IQ3_XXS
con `ngram-mod` a 131k, DFlash2+ngram a 105k y MTP+ngram a 105k. Usan KV
`q5_1`, `reasoningEffort=medium` y el sampling conservador de LlamaCode; no son
una recomendación automática ni una réplica exacta del post, que usaba
`froggeric_fix_qwen38.jinja` y una build experimental propia. El candidato MTP
requiere copiar manualmente el GGUF fusionado; el de DFlash2 necesita una build
beellama/dflash2-capable. Ejecutar HE0 antes de HE20/BCB y conservar las
aprobaciones de tools porque el modelo es uncensored.

En equipos de **24 GB VRAM + 128 GB RAM** el catálogo ofrece además, sólo bajo
instalación manual, el perfil experimental
`[experimental] Laguna S 2.1 118B-A8B Q2`. Usa el GGUF
`UD-Q2_K_XL` de ~39,7 GB en una sola PC mediante GPU+RAM (`--n-cpu-moe 32`),
contexto 100k y `ubatch 768`; requiere `llama.cpp b10087+` y acepta cualquier
build oficial posterior compatible (no queda fijado a b10087). No forma parte de la
recomendación automática, no se descarga junto con MAX-Q/FAST-GEMMA y debe
compararse mediante benchmark antes de reemplazar MAX-Q. MAX-Q usa ThinkingCap
Qwen3.6-27B a 131k; el anterior Qwen base de 262k se conserva como MAX-CTX.

Como perfil experimental de 24 GB, el catálogo también ofrece **Qwen3.8-27B**
de Unsloth con MTP integrado, `mmproj-BF16.gguf` y la plantilla
`qwen38-tools-fixed.jinja` (safe-v2, conservadora respecto del formato entrenado).
La plantilla conserva el wording original de herramientas, bloques históricos de
thinking y argumentos JSON/XML sin coerciones silenciosas, además de validar roles
y contenido multimodal. Incluye variantes UD-Q4/Q4_K_M/Q5_K_M y pruebas MTP2,
MTP3 y MTP4 para compararlo con MAX-Q/ThinkingCap bajo la misma suite. Es opt-in:
la app descarga los pesos desde Hugging Face cuando se acepta el perfil, pero no
los incluye en el repositorio; el toggle de thinking controla `enable_thinking`,
`preserve_thinking` y el esfuerzo `low`/`high`/`max` del template.

Como experimento de modelo auxiliar, el catálogo incorpora **Ling 3.0 Tiny**:
7,9B parámetros totales y 1,3B activos por token, con GGUF comunitario y
thinking configurable. `sys-ling30-tiny-q6-131k` usa Q6 + KV q8 y thinking
apagado para medir compresión, resúmenes y otras tareas no críticas; su variante
UD-Q4 de 64k mide el punto de baja memoria. `sys-hybrid-ling30-qwen38` reutiliza
el harness híbrido secuencial existente: Ling planifica sin tools y Qwen3.8
ejecuta. Es una prueba de la hipótesis del post, no routing automático de cada
subtarea ni un resultado ya medido. Los perfiles son opt-in, requieren una build
con soporte BailingMoE3 (se fija b10331+ como mínimo conservador) y descargan el
GGUF sólo al aceptarlos. Fuentes: [modelo oficial](https://huggingface.co/inclusionAI/Ling-3.0-tiny)
y [GGUF](https://huggingface.co/bloomer010/Ling-3.0-tiny-GGUF).

Para equipos de **48 GB de VRAM** el catálogo agrega además el candidato de
benchmark `Qwen3.8 Uncensored Q8_0` de JonathanColetti, con visión, MTP3, 196k y
B2048/U256. Sus variantes separan `split-mode tensor`, `--no-mmproj-offload` y
prompt cache warm; todas siguen HE0 → HE20 → BCB y no se promueven automáticamente.

También queda disponible, sólo para benchmark en **48 GB**, el candidato
`Qwen3.8 UD-Q6_K_XL` de Unsloth: contexto 96k, MTP2, `mmproj-BF16`, split layer
1,1 y KV q4_0. Sus variantes comparan MTP3/MTP4, 64k/131k, KV q8, B2048/U512,
mmproj en RAM, tensor split, cache warm y reasoning on. Es la traducción
reproducible de la receta del post de Qwen_AI; no debe compararse directamente
con sus 80–110 tok/s porque el post usa otra GPU/backend y no se descarga ni se
promueve automáticamente.

Para **2× RTX 3090 (48 GB agregados) + 64 GB RAM o más**, el perfil paralelo
`[experimental 48GB] Laguna S 2.1 118B-A8B Q2 · 100k` reutiliza el mismo GGUF y
lo mantiene completo en GPU (`split-mode layer`, `tensor-split 1,1`, mmap y KV
q4_0). Medido con b10228: carga en 16,5 s, ocupa 22.168/20.609 MiB, procesa
67.660 tokens a 1.673 t/s y genera a 37,5 t/s; a 32k alcanzó 1.954 t/s de prefill
y 55–69 t/s de decode. Emitió tool calls OpenAI válidas. Ambos perfiles preservan
el razonamiento entre turnos. En BigCodeBench-Hard obtuvo dos veces 2/8 (4/16,
25,0%; 91,1 s por pasada), por debajo de KAT/ThinkingCap (37,5%) y por encima de
DeepSeek V4 IQ3_S (12,5%). La variante 48 GB queda marcada como favorita y perfil
evaluado, pero sigue siendo opt-in: no reemplaza a KAT/ThinkingCap para coding.

El mismo tier ofrece ahora, también **opt-in**, `[experimental ultra] ULTRA-Q`,
basado en `DeepSeek-V4-Flash-0731 UD-IQ3_S` (~116 GB en cuatro shards). Su punto
inicial para RTX 3090 + Ryzen 9900X + 128 GB DDR5 es contexto 131k, 44 capas GPU,
KV q4_0, `mmap`, `--n-cpu-moe 39`, margen de VRAM `--fit-target 512` y DSpark
integrado (`--spec-type draft-dspark --spec-draft-n-max 5`); requiere llama.cpp
oficial b10228 o posterior. En Windows, `no-mmap` puede intentar reservar cerca de 99 GB de memoria CUDA Host y fallar incluso con 128 GB de RAM. El
perfil declara presets clonables 64k/131k/192k/256k/384k y el auto-tuner explora
31/35/39/43 capas MoE en CPU. Incluye además doce perfiles opt-in de benchmark
para comparar batch/ubatch, DSpark y reparto CPU-MoE sin alterar el baseline. No se
recomienda ni descarga automáticamente: antes
de promoverlo se debe medir estabilidad, pagefile, calidad y tiempo total en el
hardware local. Detalle operativo en [`docs/ultra-q.md`](docs/ultra-q.md).

Para validar el soporte upstream con el drafter real sin modificar ese baseline,
el catálogo agrega el perfil paralelo `[experimental ultra] ULTRA-Q · DSpark
externo`. Reutiliza los cuatro shards IQ3_S y descarga como dependencia obligatoria
el GGUF DSpark separado (~10,9 GB), emitiendo `--spec-draft-model` junto con
`draft-dspark`. Sigue siendo opt-in: en 24 GB de VRAM el costo adicional puede
anular la aceleración y debe compararse contra ULTRA-Q y la variante `nospec`.

Como prueba separada de la optimización del fork, LlamaCode incorpora el perfil
`[experimental] DeepSeek V4 Flash · LID CUDA · f16 KV · 131k`. Compila desde la
rama `spencer-zaid/llama.cpp:deepseek-lid-cuda`, que agrega Lightning Indexer
CUDA, y se selecciona por el flavor dedicado para no sustituir al binario oficial.
La configuración verificada cargó los cuatro shards y respondió `OK` a 131k en
2× RTX 3090 + 128 GB RAM; usa KV f16, `GGML_CUDA_NO_PINNED=1`, `--fit-ctx 131072`
y desactiva el build npm de la UI; permite assets prebuilt y agrega un
`loading.html` mínimo si el fork lo omite. También expone 256k/512k/1M como presets
experimentales para medir, no como capacidades garantizadas. El baseline ULTRA-Q
q4_0 no se modifica porque la rama LID exige f16.

El tier dual de 48 GB incluye además el perfil opt-in
`[experimental 48GB] Fable Fusion Qwen3.6-27B Q6 · MTP · visión`. Requiere
llama.cpp b10331+, descarga el GGUF MTP Q6 y `mmproj-F16`, y usa MTP3 a 32k con
KV K=q8/V=q8 según la política vigente. El resultado histórico con K=f16/V=q8
queda sólo como antecedente y debe repetirse antes de compararlo. En BigCodeBench-Hard repitió 3/8 dos veces (6/16, 37,5%), igualando
la calidad de KAT/ThinkingCap pero con 71,5 s por pasada. El ajuste corto MTP4 a
120k no queda como base: bajo carga sostenida produjo accesos CUDA ilegales. El
perfil se mantiene fuera de las recomendaciones automáticas hasta completar la
suite agentica E2E. La comparación textual y el barrido están documentados en
[`docs/research/fable-fusion-qwen36-27b.md`](docs/research/fable-fusion-qwen36-27b.md).

En **2× RTX 3090 + 128 GB RAM** existe además el perfil favorito/benchmark opt-in
`[experimental 48GB] MiniMax M2.7 Q3_K_S · 32k`. El GGUF ocupa 98,69 GB. La
receta optimizada requiere llama.cpp b10331 (`--n-cpu-moe 45`, reparto
`layer 3,1`): usa aproximadamente 9,1/22,6 GB de VRAM, completa tool-calling y
midió 11,4 tok/s de decode en una llamada corta. En BigCodeBench-Hard repitió 1/8
dos veces (2/16, 12,5%), a 1.783,8 s por pasada y 5,4 tok/s sostenidos: estable,
pero sin ventaja de calidad y mucho más lento que los perfiles Qwen. b10228
derribaba el server con tools. Permanece fuera de recomendaciones automáticas;
Q4 tampoco es viable con 128 GB por tamaño.

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
| Detector de nueva versión (última GitHub Release + popup con changelog) | ✅ |
| Agente nativo (LlamaAgentBackend, ReAct + tools + MCP) | ✅ P5 |
| Agentes persistentes versionados + feedback supervisado + triggers | ✅ |

El agente nativo combina dos guardas anti-loop: canoniza nombre y argumentos JSON,
permite dos llamadas idénticas consecutivas y bloquea la tercera antes de ejecutarla.
El bloqueo cierra el turno en vez de volver a consultar al modelo con otro aviso,
evitando que reinicie el ciclo. Una llamada diferente reinicia la racha; además se
detectan espirales de fallos equivalentes aunque cambien comandos o argumentos, y
un éxito o una escritura comprobable reinicia esa racha de errores.
Sobre esas guardas opera un gobernador de progreso elástico: agrupa intenciones
equivalentes aunque varíen superficialmente los argumentos (por ejemplo una serie
no solicitada de `run_test.*`), renueva el presupuesto cuando aparece evidencia
nueva y exige un replanteo antes de detener una trayectoria estancada. Las tareas
multilenguaje explícitas conservan sus artefactos independientes. Los valores de
crédito/replanteo/cierre pertenecen al `AgentProfile`, de modo que Chat liviano es
más frugal y Máximo admite exploración más extensa sin reglas por nombre de modelo.
Cada tool tiene además watchdog por inactividad: operaciones locales rápidas usan
un límite corto, red/investigación uno amplio y `run_shell` respeta su `timeout_s`;
la salida incremental renueva el watchdog, por lo que un build largo con actividad
no se corta. Todo timeout produce `tool_result`, reinicia el worker y cierra el
turno con diagnóstico en vez de consumir el timeout global. Benchmark de agente
usa temperatura acotada, seed fijo y guarda métricas de progreso/estancamiento.
Los perfiles de lanzamiento pueden fijar `reasoningEffort` y `reasoningBudget`
por request. ULTRA-Q usa `high` explícito y un techo de 8192 tokens para evitar
la cola larga del `low` implícito de DeepSeek V4 Flash sin desactivar el
razonamiento en tareas complejas.
Los perfiles de agente editables incluyen además la opción de compatibilidad
`thinkingLeakGuard`, apagada por defecto. Al activarla para un modelo cuyo template
filtra razonamiento, el harness pide no preservar thinking entre llamadas de tools
y descarta la cola posterior a un `</think>` huérfano; los demás perfiles conservan
el comportamiento estándar del modelo/template.
También vigila el stream de cada generación: si un bloque largo se repite tres
veces consecutivas, conserva una copia, detiene esa generación y registra
`stream_repetition`. Esto cubre loops de razonamiento/respuesta que ocurren antes
de que el modelo llegue a solicitar una herramienta.

### Agentes persistentes

La página **Agentes** agrupa en una entidad de producto la identidad e instrucciones
del agente, su `AgentProfile`, `LaunchProfile`, workspace, skills, permisos, Tasks
y triggers asociados. Al activarlo, LlamaCode aplica sus instrucciones y perfil de
capacidades al agente nativo. Las definiciones se guardan en
`AppLocalData/LlamaCode/agents/agents.json`.

Para crear uno alcanza con completar nombre, propósito e instrucciones; las
referencias por ID a perfiles, workspace, Tasks y skills son avanzadas y opcionales.
La propia página explica el flujo de revisiones, feedback y triggers, y todos sus
controles siguen la paleta del tema activo.

Cada cambio semántico genera una revisión inmutable con motivo y snapshot. La UI
permite inspeccionar el historial y restaurar una revisión anterior; restaurar crea
una revisión nueva y nunca reescribe la historia. El feedback también es
supervisado: **Proponer** no cambia el comportamiento; sólo **Aprobar** incorpora
la corrección a las instrucciones y crea otra revisión. Este mecanismo no puede
elevar permisos, activar tools ni modificar secretos.

Las Tasks vinculadas alimentan un resumen operativo por agente (corridas, tasa de
éxito, tokens y tiempo) reutilizando `RunHistoryStore`, incluidos los resultados
manuales y programados existentes.

`TriggerManager` persiste triggers normalizados en
`AppLocalData/LlamaCode/agents/triggers.json`. `filesystem` usa
`QFileSystemWatcher` con debounce; `webhook` y `appEvent` se despachan con el mismo
contrato `{type,event}` mediante `dispatchEvent`. Como `triggerManager` es un
sub-target de `ControlApi`, conectores locales pueden invocarlo por
`POST /invoke` sin agregar endpoints específicos por proveedor. Todos los tipos
terminan solicitando una Task existente, conservando sus permisos, aprobaciones,
traza y validación final.

Cuando un turno encuentra fallos de herramientas, cambia de estrategia y finalmente
progresa con éxito, el agente ejecuta una reflexión breve en segundo plano y conserva
la técnica generalizable como memoria de tipo `skill`. La habilidad incluye el
síntoma o precondición, la estrategia útil y su verificación; evita guardar intentos
fallidos como receta, secretos, rutas absolutas o detalles efímeros. Estas habilidades
quedan en la memoria estructurada del proyecto y se recuperan en sesiones futuras.

Las integraciones MCP usan descubrimiento lazy: el catálogo completo permanece en
el worker y el modelo recibe sólo `mcp_search_tools` y `mcp_call_tool`. La búsqueda
devuelve bajo demanda los schemas relevantes, evitando reenviar todas las
definiciones en cada turno y manteniendo plano el costo de contexto al sumar servers.

Las tools MCP externas aplican además un contrato transaccional uniforme. LlamaCode
lee las `annotations` estándar (`readOnlyHint`, `destructiveHint`,
`idempotentHint`, `openWorldHint`) y admite una extensión opcional
`annotations.llamacode`; si faltan metadatos, la tool se considera escritura
externa y exige aprobación. La aprobación queda ligada al SHA-256 del payload
exacto, cada turno propaga un `correlationId` y cada llamada recibe una clave de
idempotencia por `_meta`. Los resultados generan recibos persistentes con hashes,
estado `executed`/`verified`, deduplicación dentro de la misma correlación y, cuando
el server devuelve `structuredContent.receipt`, evidencia como `externalId`,
`before`, `after`, `verification` y `rollbackToken`. Tasks conserva esos recibos
en su historial de corridas.

La delegación multi-agente ajusta automáticamente su concurrencia al perfil activo:
respeta los slots de `llama-server`, reduce el fan-out con contextos largos y aplica
límites conservadores según la VRAM detectada. Un perfil de un solo slot conserva
la delegación, pero ejecuta los sub-agentes secuencialmente.

En modo Agente, la consigna se clasifica localmente antes del envío. Cuando otra
configuración resulta materialmente más adecuada (código preciso, investigación,
planificación, creatividad o tarea rápida), la UI ofrece crear y activar una copia
del perfil actual con temperatura, razonamiento, directivas y tools ajustados. La
sugerencia es explicable y opcional: nunca modifica el perfil original ni cambia
la configuración sin confirmación, y al rechazarla el mensaje se envía normalmente.

## Objetivo

Launcher serio para `llama-server`, evolucionado a centro de mando de agentes de código con chat integrado e historial persistente.

## Foco diferencial

- **Multi-llama.cpp**: convivir con varias builds/forks sin fricción.
- **Multi-GGUF roots**: indexar varias carpetas/discos de modelos.
- **Multi-perfiles compuestos**: mezclar `Backend + Model + Runtime + Harness + Workspace`.
- **Perfiles híbridos**: un `LaunchProfile` puede vincular un perfil planificador y
  otro ejecutor. El modo secuencial está pensado para modelos locales que comparten
  GPU/puerto; el concurrente, para endpoints independientes. El repo incluye la
  prueba `111_HYBRID MAX-Q planner + KAT-Coder executor`.
  En modo secuencial, cada envío de Agente detiene el ejecutor, carga el
  planificador y le pide un plan sin tools mediante `/v1/chat/completions`; luego
  descarga el planificador, restaura el servidor y agente ejecutores, y entrega el
  request original junto con el plan. La planificación usa streaming y un watchdog
  de progreso: no existe un límite total mientras sigan llegando deltas; sólo se
  aborta ante ausencia inicial prolongada o inactividad sostenida del stream. La
  vista Agente permanece activa y habilitada durante todo el hot-swap, incluso en
  el intervalo sin backend, y muestra el pipeline como inicio en curso sin
  confundir el apagado transitorio con una detención. El transcript y el título de
  sesión muestran únicamente el request original del usuario; el plan y las
  instrucciones de coordinación se entregan a la API como contexto interno. Los
  adjuntos se conservan para la fase de ejecución. Si el planificador falla o
  responde vacío, el ejecutor se restaura pero el request se cancela para no
  ejecutar a ciegas.
  El preset `[experimental hybrid] ULTRA-Q planner → MAX-Q executor` amplía ese
  pipeline con un contexto de workspace acotado (reglas, README, árbol y Git), un
  contrato `HybridPlan v1` validado y cacheado por SHA-256, y un journal de fases.
  Un retry idéntico reutiliza el plan; cambios en request/contexto/modelo lo
  invalidan. Tras un cierre durante el swap, el siguiente arranque restaura MAX-Q
  como perfil seleccionado. La UI muestra cada fase del intercambio usando los
  nombres reales del planificador y del ejecutor seleccionados.
  El benchmark `Ling Tiny planifica → Qwen3.8 ejecuta` usa esa misma ruta para
  medir si un auxiliar pequeño reduce el tiempo total sin degradar el resultado;
  todavía no cambia el routing de compresión/resumen del harness.
- **Chat persistente**: historial de conversaciones agrupado por proyecto/perfil.
- **Workspaces portables**: los proyectos también pueden asociar investigaciones y
  exportarse desde Deep Research como un paquete JSON autocontenido con manifiesto,
  chats y reportes. Secretos y embeddings regenerables quedan excluidos.
- **Agente integrado**: opencode via HTTP API sin subproceso por mensaje, con sesiones y proyectos.

## Arquitectura

La eficiencia del agente incluye telemetría por fase, prefijo estable para
reutilizar la caché KV, checkpoints versionados, vistas estructuradas seguras,
un índice persistente incremental del workspace (`project_brain`) y workflows
reanudables. El diseño, esquema y protocolo de benchmark están en
[`docs/agent-efficiency.md`](docs/agent-efficiency.md).

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
- `id`, `name`, `path`, `flavor` (`official`, `mtp-fork`, `ninfer-3090`, `custom`)
- `backend` (`cuda`, `vulkan`, `cpu`, `metal`)
- `versionHint` (texto libre)
- `supportedFlags`, `kvTypes`, `conflictingFlags`, `flagAliases`
- `envDefaults`, `workingDirectory`, `binaryHash` (SHA256 primer 1MB)
- `pathValid` (validado en runtime)

### Engine Catalog

La página **Binarios** incluye un catálogo curado de motores y forks:

- `llama.cpp` oficial y `beellama`/MTP mantienen instalación automática desde
  releases cuando hay prebuilt compatible.
- Forks como `ik_llama.cpp` o `TurboQuant` se muestran con compatibilidad por
  plataforma/GPU y, cuando no publican prebuilts útiles, ofrecen build-from-source
  guiado para producir `llama-server` y registrarlo en `BinaryRegistry`.
- `Nanbeige llama.cpp` compila la rama `nanbeige42` con CUDA para ejecutar los
  GGUF Looped Transformer de Nanbeige4.2. Se mantiene experimental y separado del
  motor oficial; en Windows el build guiado desactiva `ccache`, que no es fiable al
  interceptar `cl.exe` en este fork.
- Motores con contrato distinto (`KoboldCpp`, `llamafile`) quedan catalogados como
  opciones experimentales/manuales hasta que el launcher soporte su ciclo completo.
- **NInfer-3090** queda soportado como backend manual/experimental para los tres
  artefactos nativos `qwen3_6_27b.ninfer`, `qwen3_6_35b_a3b.ninfer` y
  `qwen3_8_27b.ninfer`. Se registra `ninfer-serve` en **Binarios** con flavor
  `ninfer-3090`; los perfiles bundled `[experimental 24GB] NInfer-3090` usan la
  CLI nativa y aparecen como candidatos de benchmark HE0 → HE20 → BCB. El
  artefacto Qwen3.8 requiere NInfer revision `5232055+` y su model card declara
  que no ejecuta tool calls generadas, por lo que BCB debe validarse como
  transporte antes de interpretar el resultado. NInfer no acepta GGUF
  arbitrarios, visión externa ni draft externo: el tokenizer, template, MTP y
  recursos compatibles deben estar dentro del artefacto `.ninfer`.

### Capabilities Matrix

Cada binario mantiene flags soportados, aliases, tipos KV detectados y conflictos.
El probe ejecuta `--version` y `--help`, extrae valores de `--spec-type` como
pseudo-flags (`spec-type:nextn`, etc.) y persiste la versión real. `EffectiveProfileBuilder.addFlag()` degrada con `warning` o emite `blockingError` según criticidad.

## Diseño Multi-GGUF roots

### Model Root Registry

Entidad `ModelRoot`: `id`, `path`, `label`, `scanMode` (manual/startup/watch), `enabled`, `priority`, `tags`, `isOnline`.

### Catálogo de modelos (SQLite)

Entidad `CatalogModel`: `id`, `rootId`, `absolutePath`, `fileName`, `sizeBytes`, `mtime`, `familyHint`, `quantHint`, `architecture`, `parameterCount`, `trainedContext`, `isVisionCandidate`, `isDraftCandidate`, `isAvailable`, `sha256`. Arquitectura, parámetros y contexto máximo entrenado se leen directamente del header GGUF durante el escaneo y quedan disponibles en el catálogo/UI para recomendaciones seguras sin alterar perfiles configurados manualmente.

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

El cookbook incluye `Nanbeige4.2-3B` Q4_K_M como candidato experimental para código
y tools. La entrada declara `required_engine: nanbeige42`: descargar el GGUF no
implica compatibilidad con un binario oficial y el usuario debe instalar el fork
correspondiente desde **Binarios**.
Validación local en RTX 3090 (Q4_K_M verificado por SHA-256): ~103 tok/s de
generación y tool-call nativa correcta en el primer turno. Sigue experimental porque
puede sobre-generar, desobedecer formatos breves y repetir contenido tras devolver
el resultado de una tool; por eso no se instala como perfil de sistema predeterminado.
El harness mitiga ese comportamiento sin acoplarse al modelo: no preserva bloques de
thinking en el historial wire y corta colas posteriores a un `</think>` huérfano
cuando pensar está desactivado, conservando la respuesta válida anterior.

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

El botón **Instalar y usar** de los perfiles recomendados por hardware selecciona
el perfil correspondiente en **Lanzar**. Si sus dependencias ya están presentes,
inicia directamente **Iniciar servidor + agente**; si falta modelo o binario, abre
**Descargas**, espera el escaneo de catálogo/binarios y arranca al quedar listo.

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

Un `LaunchProfile` también puede marcarse como `deprecated`. Estos perfiles
siguen visibles y editables en **Configuración > Perfiles** para poder
reactivarlos o migrarlos, pero se excluyen de los selectores operativos de
**Lanzar**, **Agente**, **Benchmark** y de cualquier otra acción que pueda
iniciar una ejecución.

Los distintivos visuales son independientes: ⚙ identifica sólo los perfiles
base recomendados para usuarios nuevos, mientras que 🏆 identifica únicamente
los seis perfiles FAST/BALANCE/QUALITY seleccionados para comparación. La
bandera interna de sistema puede seguir protegiendo perfiles bundled aunque no
se muestre el distintivo ⚙.

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
  Los perfiles de Chat también pueden fijar `reasoningEffort` (`low`, `medium`,
  `high`, `xhigh` o `max`) y se reenvía al template sólo con thinking activo.
  Si el modelo emite `<think>` igualmente, Chat descarta ese bloque en streaming
  y no lo guarda en el historial.
- **Indicador de fase** mientras espera (`Pensando...`, ejecución de tools,
  escritura/lectura de archivos, aprobación pendiente), cursor `▌` durante generación
- **Streaming estable**: durante la generación se actualiza sólo la burbuja activa,
  sin reconstruir toda la lista de mensajes, para evitar saltos verticales.
- **Stop de generación** con guardado de lo recibido
- **Cola administrable durante la generación**: los mensajes pendientes se ven
  encima del compositor, numerados y con dos líneas de vista previa; cada uno se
  puede previsualizar, editar o eliminar, y la cola completa puede vaciarse.

## Harness de Agente (opencode + LlamaAgent modular)

El harness actual se conserva como perfil legacy. El perfil experimental
agent-intermedio-next activa el contrato Next sin migrar ni compartir sesiones
con legacy: cambia de backend al seleccionar el motor, guarda los resultados con
engine/version/fingerprint y permite volver a Legacy seleccionando el perfil
histórico. La comparación A/B usa el mismo launch y separa el namespace de
persistencia para que probar Next no altere el historial existente.

- Catálogo de motores legacy/next desde el editor de perfiles.
- Sesiones Next aisladas, event log por sesión y ledger de efectos inciertos.
- Snapshot de capacidades fail-closed y protocolo de workers versionado con
  framing acotado, nonce de autenticación, timeout y cancelación.
- SDK Node/Python implementadas en `sdk/node` y `sdk/python`, con smoke cruzado;
  el sandbox OS opt-in usa Job Objects/grupos de procesos y bubblewrap cuando
  está disponible. El detalle operativo está en `docs/harness.md` y `sdk/README.md`.
- Los perfiles `worker.lane=node|python` ya conectan esa frontera al loop nativo:
  `worker_call` aparece sólo después de autenticar el proceso, atraviesa la
  aprobación existente y devuelve errores/`tool_result` al mismo turno; sin ese
  módulo el perfil legacy no crea procesos ni cambia sus schemas.
- El inventario actualizado de perfiles de agente, roles de salas, motores,
  adapters y skills portables está en [`docs/agent-harness-inventory.md`](docs/agent-harness-inventory.md).
  Las skills se pueden activar o desactivar por harness desde el editor del
  perfil; la política usa `include`/`exclude` y se hereda por fase.

- **Integración HTTP nativa**: comunica con opencode server vía REST + SSE, sin subproceso `opencode run` (elimina conflicto de DB SQLite en Windows)
- **Vista Agente**: chat bubbles con streaming en tiempo real
- **Cola administrable**: mientras el agente trabaja, `Cola (N)` abre los
  mensajes pendientes para previsualizarlos, editarlos, eliminarlos o vaciarlos.
- **Sesiones seguras**: crear una sesión desde el botón `+` de una carpeta se
  difiere fuera del click del listado, evitando reconstruir el delegate QML
  mientras todavía se lo está procesando. Durante un turno nativo en curso se
  puede navegar y revisar otra sesión: el turno sigue en su sesión de origen y
  sus deltas no contaminan el historial que se está viendo.
- **Turnos simultáneos entre proyectos**: el agente nativo crea un runtime aislado
  por conversación activa (request, stream, contexto, compactación, tools,
  aprobaciones y subagentes). Con `parallelSlots >= 2`, una tarea de un proyecto
  puede continuar mientras se inicia otra en un proyecto distinto. Si todos los
  slots están ocupados, el nuevo turno queda en cola; dentro de una misma
  conversación los turnos conservan orden estricto. La lista de sesiones muestra
  `Trabajando` o `En cola` por conversación.
- **Perfil efectivo visible**: con un servidor local activo, Agente sincroniza su
  selector con el perfil realmente cargado y usa sus parámetros, evitando mostrar
  o aplicar otro launch guardado. Los perfiles cloud conservan una selección
  independiente; sin servidor activo se restaura el último perfil de Agente.
- **Títulos automáticos**: el primer prompt asigna un título de hasta tres palabras;
  al iniciar también se reparan sesiones antiguas que ya tienen prompt pero todavía
  figuran como `Sesión`.
- **Modo por sesión**: cada chat del Agente recuerda por separado su política de
  aprobación y su nivel de capacidades; al cambiar de sesión se restauran ambos,
  incluso después de reiniciar la aplicación.
- **Viewport estable**: las actualizaciones del modelo y las mediciones transitorias
  de mensajes altos no reinician el chat al comienzo; el auto-scroll sólo avanza
  hacia el final cuando el usuario ya estaba siguiendo la respuesta. El compositor
  inferior conserva la altura de sus controles y no queda recortado al maximizar;
  además, crece con mensajes multilínea hasta ocupar como máximo el 50% del alto
  visible y, desde allí, conserva scroll interno para mantener todo el texto legible.
  Los movimientos del viewport del Agente se registran como `agent/ui/scroll` en
  `runtime/agent.log`, con la acción, posición, límites, altura y estado de seguimiento.
  Al reemplazar la lista de mensajes, el viewport conserva el seguimiento inferior
  mediante un modelo visual incremental estable: actualizar o agregar una burbuja no
  vacía el `ListView`, por lo que no existe un frame intermedio visible en el inicio.
- **Estado visible del turno**: la burbuja activa muestra si el agente está
  pensando, ejecutando una herramienta, escribiendo/leyendo archivos o esperando
  aprobación, para que las acciones largas no parezcan un bloqueo silencioso.
- **Thinking real por servidor**: el toggle `Pensar` del agente se aplica al
  arranque de `llama-server` con la mejor estrategia compatible con el binario y
  el modelo: `--reasoning on/off` en builds actuales, `--reasoning-budget` como
  fallback, o `--chat-template-kwargs {"enable_thinking":...}` en templates Qwen
  antiguos. Cambiarlo con el servidor ya iniciado requiere reiniciar el servidor
  para que el modelo deje de generar tokens de razonamiento. Los perfiles de
  agente, incluido **Máximo**, no activan `Pensar` si el checkbox está apagado.
- **Vista terminal**: log raw para debug
- **Sesiones opencode**: historial persistido en opencode DB, agrupado por directorio/proyecto
- **Sesiones concurrentes**: crear o abrir otra sesión no cancela una respuesta
  ya iniciada; el stream SSE se conserva y se aplica a su propia sesión aunque
  el usuario esté mirando otra.
- **Resume automático**: retoma la última sesión al reiniciar el agente
- **Títulos auto-generados**: actualización en tiempo real vía `session.updated` SSE

## Corridas administradas de Claude Code y Codex

Desde **Agente → 🚀 Corridas**, LlamaCode puede iniciar revisiones o
implementaciones largas en Claude Code o Codex y seguirlas desde la app. Cada
corrida conserva prompt, manifiesto, stdout/stderr y, si corresponde,
`AgentDeliverableStore`; al finalizar se agrega a `RunHistoryStore` con metadata
y rutas de evidencia. El prompt viaja por stdin, las corridas con ediciones
reservan el workspace y tienen watchdog de tiempo, inactividad y tamaño de logs.
Un cierre con código cero queda distinguido como no verificado, con artefactos
capturados o verificado por un comando explícito. `ask_teacher` usa el mismo
supervisor durable para delegar a Claude/Codex sin bloquear la interfaz. Si la
app se cierra, la corrida se recupera como `stale` y puede reintentarse. El
contrato completo está en [`docs/managed-agent-runs.md`](docs/managed-agent-runs.md).

## Backends cloud + secretos cifrados

Aunque el foco es 100% local, cada perfil puede apuntar a un **endpoint OpenAI-compat
externo** (OpenAI, OpenRouter, Groq, DeepSeek, etc.) en vez de a un `llama-server`
propio. `BackendProfile.kind = "cloud"` no lanza proceso ni binario: el chat/agente
pegan directo al `cloudBaseUrl` con el modelo configurado.

Esto también permite registrar servidores locales externos como vLLM sin
confundirlos con un GGUF administrado por LlamaCode. La familia benchmark
`sys-bench-qwen38-dflash2-vllm-*` deja declarados el target INT8 W8A16 de Qwen3.8,
el drafter DFlash2, el contexto 262k, KV FP8 y TP=2; el servidor y sus parches se
levantan fuera de la app. El procedimiento está en
[`docs/benchmark-vllm-dflash2.md`](docs/benchmark-vllm-dflash2.md).

- **SecretStore**: las API keys **nunca** se serializan en los JSON del repo. El
  perfil guarda una **referencia** (`cloudKeyRef`) y el valor se resuelve en runtime
  vía variable de entorno o store cifrado en disco — **QtKeychain** (Secret Service /
  WinCred / macOS Keychain) y, si no está disponible, fallback **DPAPI** en Windows.
- Aplica igual a los maestros HTTP, cuentas de correo y proveedores de voz.

## Modo Ingi Charla (voz-a-voz + agente)

Ingi, tu ingeniero asistente: hablá y él se encarga de usar tu computadora por vos.
Sección **🎙 Ingi Charla** en la NavBar. Si hay un **agente corriendo** (con visión
de las pantallas y computer-use), el turno de voz va al agente, que opera la PC
—clic, teclado, instalar programas, etc.— y te contesta hablando. Si no hay agente,
hace fallback a voz-a-voz simple sobre el backend de chat (sesiones e historial
incluidos).

- **STT y TTS** van por endpoints **OpenAI-compat** (`/v1/audio/transcriptions`,
  `/v1/audio/speech`). Una sola ruta de código: **local** (whisper.cpp server,
  openedai-speech, piper-http en localhost, sin key) o **cloud** (URL remota +
  keyRef). Configurable por separado para STT y TTS.
- **STT multimotor local**: Charla permite elegir Whisper base (`http_batch`) o
  Parakeet TDT v3 (`process_batch`) desde el mismo selector. Parakeet descarga el
  GGUF Q4_0 y usa el `parakeet-cli` nativo administrado por LlamaCode, sin NeMo ni
  configuración manual. Además se conservan sesiones `stream_process` persistentes
  con protocolo NDJSON v1 para sidecars externos, parciales revisables y cancelación;
  el detalle está en [`docs/voice-streaming.md`](docs/voice-streaming.md).
- **TTS multimotor**: cada perfil puede fijar HTTP/Kokoro, Pocket TTS, Piper o `qwen3-tts.cpp`, o
  dejarlo en `auto`. La selección automática considera RAM, VRAM total/libre y
  motores instalados: prioriza Qwen3-TTS 1.7B/0.6B cuando hay margen y conserva
  Piper para equipos chicos o cuando conviene reservar VRAM para el LLM. Qwen3
  admite GGUF, embedding de hablante, WAV+transcripción de referencia y una
  instrucción de estilo; si falla puede caer a Piper sin perder el turno.
- **Pocket TTS local**: el modo `pocket` instala `pocket-tts` en un venv propio,
  carga el modelo una sola vez en un sidecar local y transmite WAV PCM16 a Charla.
  Funciona sobre CPU, no reserva VRAM, soporta español/inglés/francés/alemán/
  portugués/italiano y permite usar una voz incorporada o una muestra WAV/MP3 o
  embedding `.safetensors` local. La instalación precarga la caché y las sesiones
  se ejecutan offline; `pocketAutoEnable` queda apagado por defecto hasta medir
  latencia y calidad en el equipo. El procedimiento completo está en
  [`docs/pocket-tts.md`](docs/pocket-tts.md).
- **Charla multi-GPU**: con dos o más GPU NVIDIA, la app reserva automáticamente
  la de menor VRAM para STT, TTS local y auxiliares, y relanza el perfil normal con
  un `--tensor-split` proporcional a la VRAM libre que queda en esa GPU más la de
  las demás. La selección automática también comprueba que el modelo, contexto y
  draft/mmproj entren en la capacidad combinada; si un perfil de sistema no entra,
  elige el mayor perfil normal instalado que sí entra; un perfil de usuario se deja
  intacto y muestra el ajuste necesario en vez de arriesgar un OOM. La selección
  manual y los perfiles con reparto explícito tienen prioridad; el detalle queda
  disponible en `App.voiceGpuPlan()` y en el diagnóstico de hardware.
- **Inflect v2 ONNX experimental**: puede seleccionarse manualmente como TTS local
  ultraliviano con el runner Python oficial y proveedor CPU, DirectML o CUDA.
  Admite las variantes Nano/Micro descargadas por el usuario, pero la versión
  publicada es exclusivamente inglesa, de voz masculina fija y sin clonación.
  LlamaCode exige que la aplicación y Charla usen inglés (`en`), nunca lo elige en
  modo `auto` y conserva Piper como fallback.
- **Kokoro y audio incremental**: `kokoro` usa la misma interfaz HTTP configurable
  que los demás servidores TTS. Cuando el endpoint entrega PCM16 chunked, la app
  escribe cada bloque directamente a `QAudioSink` y empieza a reproducir antes de
  que termine la síntesis. El sample rate y los canales se declaran en el perfil.
- **Presupuesto de latencia observable**: cada turno mide desde el endpointing
  (antes del STT) hasta el primer bloque enviado al dispositivo de audio, separando
  STT, primer texto del LLM, primer texto útil, solicitud/generación TTS y arranque
  de playback. Se
  conservan hasta 500 muestras locales en `voice/latency.jsonl` y la pantalla de
  Charla muestra p50/p90/p95 para comparar motores y perfiles con datos reales.
- **Guarda de capacidad agentic**: Charla clasifica el modelo activo por tamaño y
  arquitectura. Los dense menores de 4B se reservan para conversación/comandos
  acotados; 4B–7B se consideran agentes básicos y 7B+ el piso conservador para
  tools. Los MoE se muestran por parámetros totales/activos y se recomienda medir
  el costo de expertos en RAM. Si hay maestro configurado, los niveles no confiables
  indican escalado para tareas complejas en vez de ocultar el modelo al usuario.
- **Captura** PCM16 mono 16 kHz (`QAudioSource`) con **VAD por energía RMS** (fin de
  turno por silencio configurable), **selección de micrófono** y **medidor de nivel**
  en vivo. Botón *Probar micrófono* para validar entrada sin servidor.
- **Turn-taking configurable**: `Manos libres (VAD)` conserva la escucha automática;
  `Pulsar para hablar` deja el micrófono cerrado mientras Charla está en espera y
  sólo captura mientras se mantiene pulsado el botón. En este modo las pausas no
  cierran el turno: al soltar se entrega el audio acumulado al STT.
- **Barge-in**: interrumpir el TTS al detectar voz nueva o al pulsar PTT durante una
  respuesta, cancelando también la generación del chat/agente para no seguir
  consumiendo tokens de una respuesta descartada. Máquina de estados
  `listo → escuchando → transcribiendo → pensando → hablando` con auto-escucha opcional.
- **Prefill + streaming**: en PTT el estado `listo` precalienta el prefijo estable del
  backend antes de hablar; la transcripción parcial y el TTS por oraciones siguen
  fluyendo en paralelo con la generación del modelo.
- **Dictado literal**: reutiliza el STT configurado sin enviar el texto al LLM ni
  reescribir la intención; al detenerlo deja la transcripción en el portapapeles
  para pegarla en terminales, editores o cualquier otra aplicación.
- Al iniciar con STT gestionado, si falta el modelo o `whisper-server`, Charla
  ofrece instalar en secuencia el modelo, `whisper-server`, Piper y una voz en
  español, guardando automáticamente las rutas. Los perfiles sin configuración
  TTS explícita usan Piper gestionado en lugar de asumir un servidor HTTP en 8082.
  La escucha no comienza hasta que los prerrequisitos estén listos.
  En Windows, el binario se obtiene desde el asset x64 del último release oficial
  de `ggml-org/whisper.cpp`, evitando depender de una versión retirada.

## Memoria, RAG y verificación

El agente nativo no solo lee archivos: mantiene memoria y conocimiento estructurado.

- **MemoryStore por capas**: hechos durables extraídos de las conversaciones
  (consolidación en background al terminar una fase recuperada) + memoria por proyecto en archivo.
  Navegar, crear o abrir sesiones nunca dispara esta tarea pesada.
  Los hechos estructurados vigentes se inyectan de forma acotada al iniciar el
  agente y pueden registrar importancia, sorpresa, verificación y supersesión. El
   ranking y la poda priorizan correcciones, reglas y decisiones verificadas sin
   romper memorias JSONL creadas por versiones anteriores.
   El mantenimiento automático se ejecuta como máximo una vez por día y deja un
   recibo en `.llamacode/memory_maintenance.json`; `action=decay` admite `dry_run`.
- **GraphStore**: grafo de entidades/relaciones para conocimiento estructurado.
- **Evidencia del grafo**: `CodeGraphIndexer` adjunta citas `ruta:Línea-Línea` y
  SHA-256; `graph doctor` informa fuentes obsoletas y relaciones huérfanas sin
  ocultar inferencias no revisadas.
- **KnowledgePacket**: paquete acotado que une memoria + grafo, con recibo de
  nodos, relaciones y fuentes. Separa decisiones vigentes de hechos de apoyo y
  explicita una política de autoridad. Se habilita por perfil mediante
  `HarnessSpec.knowledge`.
- **WorkRegistry**: claims efímeras por proyecto con rutas, agente, sesión,
  heartbeat y expiración. `work_status` permite inspeccionarlas y una escritura
  solapada se rechaza antes de ejecutar la tool. Shell/MCP también deben declarar
  sus rutas afectadas para entrar en el mismo gate.
- **Gate de consolidación**: `verify_claims` se comparte con la persistencia de
  memoria; los hechos no respaldados no entran y los parcialmente respaldados
  quedan con confianza limitada.
- **Edges inferidos**: las escrituras exitosas dejan trazabilidad módulo→archivo,
  y la consolidación puede relacionar decisiones con bugs por vocabulario común;
  ambos casos conservan sesión/correlación y requieren revisión explícita.
- **Repo slice previo a edición**: `repo_slice` combina el ranking híbrido local
  con citas `archivo:Lini-Lfin`, previews y vecinos por imports/includes. El agente
  obtiene evidencia compacta antes de abrir cuerpos completos; funciona con BM25
  sin servidor de embeddings y acepta presupuesto de tokens.
- **AgentEventLog**: bitácora append-only por proyecto (`.llamacode/agent_events.jsonl`)
  con eventos tipados de turnos, tool calls, resultados, fallos y alternativas
  rechazadas. Sirve como evidencia operacional: no reemplaza memoria ni grafo, los
  alimenta con un rastro auditable de qué intentó el agente y por qué algo se
  aceptó, falló o se descartó.
- **Tools**: `hybrid_search` (búsqueda híbrida léxica+semántica), `verify_claims`
  (chequeo de afirmaciones), memoria por capas. RAG sobre el material del proyecto.

### Asistente continuo

`AssistantRuntime` ofrece un canal HTTP estrecho y autenticado para un agente
siempre disponible: mensajes con deduplicación, cola acotada, respuestas y un
outbox persistente con cursor. El bind es loopback por defecto; LAN requiere un
token obligatorio. Los adaptadores de Telegram, Discord o voz se conectan a
este contrato sin obtener acceso a `ControlApi`. Ver
[`docs/assistant-runtime.md`](docs/assistant-runtime.md).

Los perfiles avanzados pueden usar routing adaptativo de tools: recorta la
superficie del primer turno por intención semántica y vuelve a abrirla luego;
las intenciones ambiguas fallan abierto. `ModelRoleRegistry` agrega roles de
modelo (`planner`, `verifier`, `embedding`, `reranker`, `stt`, `tts`, `vision`)
con modelo preferido, fallback y límites de scheduler persistidos fuera del
repo.
## StudIA (asistente de estudio sobre corpus académico)

Sección **🎓 StudIA** en la NavBar: chat que responde **sólo con documentación
académica indexada** — apuntes, libros y trabajos prácticos de una carrera — en vez
de con el conocimiento general del modelo. Desarrollado como PPS de la Facultad
(Ing. Mecatrónica, UNLZ).

Es un módulo cerrado: `src/core/studia/` + `qml/pages/StudiaPage.qml` +
`tools/studia/`. No modifica el agente, el chat ni los perfiles; sólo consume el
`llama-server` que ya levantó LlamaCode (la URL se la pasa QML desde
`App.serverBaseUrl`). Cuatro piezas con una responsabilidad cada una:

| Clase | De qué se ocupa |
|---|---|
| `StudiaIndex` | De dónde sale la documentación: búsqueda BM25 sobre el índice y regla de abstención. |
| `StudiaPrompt` | Qué se le dice al modelo: reglas, modos de tutor, contexto. Sin estado, todo estático. |
| `StudiaSessionStore` | Una conversación por materia, persistida en `AppLocalData/LlamaCode/studia/`. |
| `StudiaController` | Fachada hacia QML: orquesta las tres y mantiene el streaming SSE. |

### Materia obligatoria, una conversación por materia

No hay modo "todas las materias": estudiar es siempre sobre una asignatura. Elegir
una en el selector abre (o retoma) la conversación **`StudIA: <materia>`**, y cambiar
de materia conmuta de conversación. Así no se mezclan conceptos ni contexto entre
asignaturas, y cada una conserva su historial entre sesiones. El selector las lista
en orden de cursada.

### Modos de tutor

Además de la conversación libre, el panel ofrece **Resumen**, **Explicación**,
**Autoevaluación**, **Flashcards** y **Plan de estudio**. Elegir uno prefija la barra
de entrada con `/modo/ ` y cambia la consigna del prompt — **no** la recuperación:
se buscan los mismos fragmentos y sobre ellos se pide resumir, explicar, tomar examen
o armar tarjetas. El prefijo también se puede tipear a mano.

### Bibliografía propia (índice aparte)

El botón 📎 junto a la barra de entrada suma un documento del estudiante. Va a un
**índice separado** (`AppLocalData/LlamaCode/studia/mi_biblioteca.db`), nunca al de
la cátedra: regenerar uno no pisa al otro, y en las citas se distingue el origen. La
extracción la hace el mismo ingestor (`ingest.py --archivo <ruta> --materia <m>`), así
que no hay dos implementaciones del chunking que puedan divergir.

Al recuperar, los dos índices se consultan **por separado** y la bibliografía propia
recibe un cupo de los `k` fragmentos. No se fusionan por score: BM25 depende del
tamaño del corpus, así que los puntajes de dos índices distintos no son comparables.

Por la misma razón, **el índice propio corre con `setExigirEvidencia(false)`**. BM25
pesa cada término por lo raro que es en el corpus; en un índice de 3 fragmentos todos
los términos aparecen en todos y el score da `-0.00`, con lo cual el umbral calibrado
para 139.000 fragmentos dejaría afuera absolutamente todo lo adjuntado. La garantía
anti-alucinación no se pierde: la aporta el índice de cátedra, que sí mantiene el gate.

Desde la UI se puede **listar y quitar** lo adjuntado (`ingest.py --quitar <ruta>`).
El índice de la carpeta DATA nunca se modifica.

### Fórmulas

El prompt le prohíbe LaTeX al modelo, pero igual lo emite. `StudiaTexto::latexALegible`
convierte lo que llega a notación legible antes de mostrarlo y de guardarlo:
`\frac{d^n y(t)}{dt^n}` → `(dⁿ y(t))/(dtⁿ)`, `a_{n-1}` → `aₙ₋₁`, `\alpha` → `α`,
`\ldots` → `…`. No es un motor de LaTeX: cubre fracciones, índices, griegas y
operadores, y lo que no reconoce lo deja intacto en vez de romperlo.

`StudiaTexto::enBloques` separa además las **ecuaciones de display** (`\[…\]`, `$$…$$`
o un renglón entre corchetes con comandos LaTeX) del texto que las rodea, y la UI las
muestra centradas y en cuerpo mayor, como una ecuación insertada en Word. Un renglón
entre corchetes *sin* LaTeX (una cita, una lista) no se toma como ecuación.

### Búsqueda semántica (embeddings)

BM25 compara **palabras**, no significado: *«¿qué pasa si…?»* y *«¿qué sucede si…?»*
dan resultados distintos sólo porque una de las dos palabras es más frecuente en el
corpus. Ampliar la lista de palabras vacías tapa casos puntuales; la solución de
fondo es comparar por sentido.

`StudiaIndex::buscarHibrido` fusiona el ranking **léxico** (BM25) con el **semántico**
(coseno contra el vector de la pregunta) por **Reciprocal Rank Fusion**. Se fusionan
rankings y no puntajes porque BM25 y el coseno viven en escalas distintas y no son
comparables — RRF sólo mira la posición en cada lista.

Hacen falta dos cosas, y **si falta alguna todo sigue funcionando con búsqueda léxica**:

1. **Un servidor de embeddings** — otro `llama-server`, en otro puerto, con un modelo
   de embeddings chico:
   ```
   llama-server -m nomic-embed-text-v1.5.Q8_0.gguf --embeddings --port 8081
   ```
   Se configura desde el panel de StudIA (click en el cartel de búsqueda semántica).
2. **Vectorizar el índice**, una vez:
   ```
   python tools/studia/vectorizar.py --db <ruta a studia.db> --url http://127.0.0.1:8081
   ```
   Es incremental y re-ejecutable: sólo procesa lo que falta. Los vectores se guardan
   normalizados en la tabla `vectores` de la misma base, así el coseno es un producto
   escalar. `vectores_info` registra dimensión y modelo, y los vectores de otra
   dimensión se ignoran en vez de dar resultados sin sentido.

La pregunta se vectoriza **en el momento** (`StudiaEmbed`, async para no congelar la
ventana). Si esa llamada falla, se avisa y se responde igual con búsqueda léxica.

### Figuras: gráficos de funciones y diagramas

El prompt le enseña al modelo dos formatos de figura y la UI los renderiza a PNG:

| Bloque | Motor | Para qué |
|---|---|---|
| ```` ```grafico ```` | `tools/studia/graficar.py` (matplotlib) | curvas, comparar funciones, sombrear el área de una integral |
| ```` ```mermaid ```` | `MermaidRenderer` (ya existente en el repo) | procesos, clasificaciones, relaciones entre conceptos |

El spec de `grafico` es **declarativo**, no código:

```grafico
funcion: x**2 - 3*x + 2
rango: -2, 5
area: 1, 2
titulo: Área bajo la parábola entre 1 y 2
```

El sidecar **no ejecuta código Python del modelo**: evalúa la expresión en un
espacio de nombres cerrado (sin `__builtins__`, sólo funciones de numpy). Mientras
se genera —o si falla— se muestra el fuente del bloque, así nunca se pierde
información. `StudiaPlot` cachea por md5 del source, igual que `MermaidRenderer`.

Requisitos: `pip install matplotlib` para los gráficos;
`npm install -g @mermaid-js/mermaid-cli` para los diagramas. Si falta alguno, esa
figura se muestra como texto y el resto de la respuesta funciona igual.

### Flashcards → Anki

Las respuestas del modo Flashcards se exportan a un `.txt` separado por
tabulaciones, que Anki importa de fábrica (Archivo → Importar). Se eligió ese
formato en vez de generar un `.apkg` para no sumar dependencias: el parser tolera
las variantes de rótulo que emite el modelo y convierte los saltos de línea
internos a `<br>`, que Anki interpreta.

### Rigor por modo

Los modos se dividen en **flexibles** (Conversación, Explicación, Ejercicio) y
**exigentes** (Resumen, Autoevaluación, Flashcards, Plan de estudio). Los exigentes
generan material que después se estudia como si fuera fiel al apunte: ahí conviene
abstenerse antes que arriesgar. Los flexibles son conversación, y responder «no tengo
información» ante una repregunta no le sirve a nadie.

En los modos flexibles:

- el umbral de abstención se afloja 2 puntos,
- el prompt pide **agotar** lo disponible antes de abstenerse (usar fragmentos que
  hablen del tema aunque no respondan textualmente, responder parcialmente, deducir
  marcándolo),
- y si la recuperación no trae **nada**, se intenta responder desde la conversación
  antes de abstenerse (ver abajo).

### Repreguntas que no tocan el corpus

«Repetí la ecuación anterior», «explicalo más simple», «no entendí el paso 2»: la
respuesta está en la conversación, no en la documentación, así que buscar en el
índice no sirve. Si la recuperación vuelve vacía, hay historial útil y el modo es
flexible, se genera con `StudiaPrompt::usuarioSoloConversacion` — un prompt que pasa
la conversación, prohíbe inventar y aclara que en ese turno no hay `[n]` que citar.

La decisión de **expandir la consulta** con el tema anterior se toma sobre los
términos **discriminantes**, no sobre cuántas palabras tiene la pregunta: *«¿qué pasa
si aumento la frecuencia?»* tiene tres términos y ninguno ubica el tema.

### Qué puede razonar y qué no

La regla de grounding distingue **hechos** de **razonamiento**. Fórmulas, datos y
definiciones tienen que estar en los fragmentos. Aplicar un método a un caso nuevo,
hacer las cuentas, relacionar dos conceptos que aparecen por separado o sacar una
conclusión que se deduce del material **sí** está permitido, con la obligación de
marcarlo (*«esto no está explícito en el apunte, se deduce de [n]»*). El modo
**Ejercicio** lleva eso a un formato de resolución paso a paso: datos → método citado
→ desarrollo → resultado → verificación.

### Repreguntas

Una repregunta corta (*"¿y cómo funciona?"*) no tiene términos propios para buscar.
`StudiaPrompt::consultaConContexto` completa la consulta con el tema de las preguntas
anteriores de esa materia, y el prompt incluye los últimos turnos etiquetados como
contexto — explícitamente **no** como documentación, para que el modelo no los cite
como fuente.

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

## Automatizaciones Teach: escritorio y browser

La sección **Automatizaciones** incorpora un modo Teach multimodal con dos destinos:

El motor se diseña como una capacidad **general de control de la PC**, no como una
colección de macros o excepciones por aplicación. Las mejoras deben funcionar en
cualquier app mediante intención, contexto de la superficie, UI Automation,
targets semánticos, visión y evidencia verificable. No se deben hardcodear nombres
de aplicaciones, colores, botones, textos, layouts o coordenadas de un ejemplo
particular dentro del comportamiento general. Paint, Calculadora u otros casos
concretos sirven como pruebas de regresión; nunca como supuestos arquitectónicos.
Una solución se considera generalizable si conserva su comportamiento al cambiar
de aplicación, resolución, idioma, tema o ubicación de controles.

- **Escritorio foreground (Windows):** el usuario elige una pantalla o ventana,
  demuestra el flujo y agrega notas. Se guardan eventos, `pointer` (posición
  absoluta y normalizada, botón, cantidad de clicks), `target` (alcance/ventana o
  control cuando está disponible), capturas y verificaciones como una receta
  semántica. Al ejecutar, el agente prioriza controles/targets semánticos,
  usa coordenadas sólo como respaldo y valida cada acción con la salida `trace`.
  Durante la demostración hay un botón flotante **Detener grabación** siempre
  visible: sus clicks se excluyen de la receta y se oculta antes de tomar la
  captura final limpia. El mouse se muestrea a frecuencia de pantalla para no
  perder selecciones rápidas. En reproducción literal, LlamaCode captura el
  estado de ventana de cada gesto (incluido maximizado/restaurado) y lo repone
  antes de transformar coordenadas; las recetas anteriores infieren únicamente
  el caso maximizado cuando la geometría registrada cubría casi todo el alcance.
  Así el replay conserva el contexto espacial enseñado en cualquier aplicación.
  Luego LlamaCode captura el
  resultado y entrega ambas imágenes al modelo con visión junto con el objetivo y
  la aplicación usada. El agente compara su significado, ignora diferencias
  transitorias, y si el objetivo todavía no se cumple usa `desktop_*` para corregir
  y volver a observar antes de finalizar, con un presupuesto finito de corrección
  para terminar con error verificable en vez de iterar indefinidamente. No se aplican reglas visuales específicas
  de una aplicación ni se exige igualdad exacta de píxeles. Cada observación
  puede producir un snapshot estructurado con fingerprint, controles UIA,
  `automationId` y patrones disponibles. Las acciones que reciben `snapshot_id`
  se rechazan si el árbol quedó obsoleto, y cada `desktop_*` deja un receipt con
  estrategia, hashes, target, correlación y estado. Los controles con patrones
  UIA se operan semánticamente mediante `desktop_control_action` sin robar foco;
  el mouse físico continúa limitado a sesiones foreground.
  Los pasos exitosos ejecutados por el reproductor nativo cuentan como evidencia
  de herramientas: si la comparación visual confirma el objetivo, no se exige una
  llamada redundante a `desktop_*` durante el turno del modelo ni una segunda
  validación estructural incompatible con esa evidencia. Un veredicto visual
  positivo cierra la corrida sin reintentar sobre un estado ya completado.
- **Browser background:** el modo Teach abre Playwright/codegen en **foreground**
  para que el usuario muestre el flujo real. Además del script, selectores y
  metadatos `target` de Playwright, LlamaCode toma evidencia visual del escritorio
  durante clicks, teclas y notas, de modo que el agente entienda la intención y el
  estado de pantalla, no sólo una lista de eventos. La Task se ejecuta luego con
  las tools de navegador, reinterpreta la intención cuando cambia la interfaz y
  verifica el resultado. El destino normal es headless, con fallback a navegador
  oculto cuando el sitio lo requiere.

Para diagnosticar una corrida sin perder el aislamiento headless, Tasks ofrece
**Vista en vivo** (opt-in). El Inspector muestra la captura más reciente de la
superficie, las acciones en orden y su resultado; en browser solicita una
captura MCP sólo cuando el servidor la expone sin parámetros, y en escritorio
captura antes y después de cada acción. Cuando el MCP lo permite, también guarda
un snapshot DOM/accessibility redacted. La traza conserva argumentos seguros,
capturas, snapshots y recibos; el Inspector permite pausar, continuar, ejecutar
un paso, capturar manualmente y limpiar las observaciones. Los artefactos se
limitan a 120 archivos o 96 MB y se eliminan los más antiguos automáticamente.
Apagada, no agrega screenshots ni cambia el comportamiento normal.

Los artefactos Teach son auto-actualizables: si durante una ejecución la interfaz
cambió y el agente igual logra completar el objetivo, registra un aprendizaje en
el `recipe.json` del proceso con el resumen de la adaptación y señales de tools
usadas. Las corridas siguientes reciben esos aprendizajes como contexto semántico
para mejorar la adaptación, tanto en escritorio foreground como en navegador
background.

Los artefactos nuevos usan receta Teach v3, con precondición, postcondición y
estrategias de reparación por paso. Las recetas v2 existentes se leen sin
conversión destructiva. La arquitectura detallada está en
[`docs/computer-use.md`](docs/computer-use.md).

Cada proceso tiene un **Tipo de proceso**: *Escritorio foreground*, *Navegador
background* o **Auto**. En *Auto* el sistema decide la superficie al ejecutar de
forma determinista: si la automatización tiene algún paso de escritorio corre como
foreground; si no, como navegador background (headless, sin robar el foco). El MCP
de Playwright se fuerza a `--headless` por defecto en navegador background; en
escritorio foreground se inyecta `--headed` para que el browser sea visible y
controlable junto con el resto de la pantalla.

Los perfiles de sistema priorizan automatizaciones robustas por texto/tools:
no cargan `mmproj` salvo que el perfil sea explícitamente de visión. Para flujos
como Calculadora, archivos, shell, desarrollo o extracción web, el agente valida
con `desktop_controls`, Playwright, filesystem o comandos en vez de depender de
capturas visuales.

Todos los perfiles de sistema Gemma 4 fuerzan mediante `--chat-template-file` la
plantilla canónica corregida de Google incluida con LlamaCode. Así, los GGUF ya
descargados también reciben las correcciones de historial, razonamiento y
tool-calling sin tener que volver a descargar los pesos.

El perfil general alternativo de 4 GB es **Gemma 4 E4B Heretic QAT**. Reemplaza
al E4B QAT base después de obtener 5/11 checks contra 2/11, mayor throughput y
menor TTFT en `Agent efficiency E2E v1`. Conserva su runtime conservador y la
plantilla canónica de tools. Como sus pesos tienen removida la alineación de
seguridad, las aprobaciones del agente siguen siendo la barrera para acciones
sensibles.

La selección de perfil que se restaura al abrir la aplicación representa la
última elección explícita del usuario en Lanzar o Agente. Los cambios temporales
de modelo realizados por Tasks, verificación, benchmarks, Charla o el watchdog no
sobrescriben esa preferencia.

El perfil de sistema **0GB CPU** es un fallback operativo para automatizaciones,
no un showcase de calidad máxima: usa Qwen3.5 4B Q4, contexto 8k y batches bajos
sin capas GPU. Además requiere un binario registrado como backend `cpu`: si sólo
hay builds CUDA instaladas, la app debe pedir instalar un binario compatible en
vez de lanzar CUDA con `--n-gpu-layers 0`. Perfiles más grandes en CPU pueden
tardar demasiado en emitir la primera tool y dejar una Task sin progreso.

Cuando un perfil declara speculative decoding con `draft-mtp`, puede usar un
`draftModel` separado o un cabezal MTP autocontenido. Este último se detecta de
forma conservadora por el marcador `MTP` del GGUF principal y se lanza con
`--spec-type draft-mtp`; si no se cumple ninguna de las dos condiciones, el
launcher bloquea el arranque. El instalador encola los drafts separados junto con
el modelo principal.

Teach vive en **Automatizaciones**. Configuración conserva únicamente el toggle y
comando técnico del MCP Playwright. Los skills Playwright anteriores se pueden
importar sin modificarlos.

Los artefactos se guardan versionados en
`AppLocalData/LlamaCode/automations/<id>/` (`manifest.json`, `recipe.json`,
`evidence/` y `browser.mjs` opcional). Las Tasks desktop requieren una sesión
Windows interactiva y un artefacto enseñado; si la sesión está bloqueada, la
ejecución queda esperando. UAC, pantalla de bloqueo y escritorio seguro nunca se
controlan. Las notas y logs redactan patrones de password/token/API key.

Cada Task define política de aprobación (`always`, `sensitive`, `autonomous`) y
límites de tiempo, acciones y reintentos. El default es confirmar acciones
sensibles.

## Automatización de browser (Playwright)

Toggle global + override por perfil (`browserAutomation` inherit/on/off) que inyecta
el **MCP de Playwright** en el set de tools del agente. El Teach de browser se
gestiona desde Automatizaciones y guarda **recetas reproducibles** que
las Tasks pueden reejecutar.

Cuando una interfaz web no tiene documentación suficiente, la tool
`browser_network_discover` resume los requests ya observados por el Playwright MCP
activo y permite investigar el contrato que produjo una acción autorizada antes de
declarar un bloqueo. La evidencia se agrupa por método, origen y path; LlamaCode no
conserva query strings, headers, cookies ni bodies, normaliza identificadores
volátiles y excluye assets estáticos por defecto. La inspección es pasiva: no
reproduce requests ni evita autenticación, permisos o aprobaciones.
En **Enseñar tarea → Navegador background** se puede activar esta observación. Tras
ejecutar el flujo, el agente correlaciona el resumen con la acción principal y lo
persiste en `recipe.json` como evidencia pendiente de revisión. El botón **Red** de
cada proceso permite revisar o limpiar los contratos observados; las ejecuciones
siguientes reciben como contexto sólo los últimos resúmenes acotados. Cada
descubrimiento puede aprobarse o rechazarse: los rechazados no vuelven a entrar al
prompt adaptativo.

La lectura web usa proveedores tipados. `web_search` consulta SearXNG —incluido un
endpoint local configurado explícitamente— o DuckDuckGo. `web_fetch` ejecuta el
pipeline `direct → Playwright MCP → Camofox`: el camino directo resuelve DNS,
bloquea localhost/redes privadas/metadata cloud, revalida cada redirección, limita
la descarga a 2 MB y extrae primero `article`/`main`. Los proveedores de navegador
reciben únicamente la URL pública final resuelta por ese preflight y deben informar
una URL final pública verificable. Cada host queda limitado a 30 lecturas/minuto.
Sólo escala cuando hay
evidencia verificable (`transport_error`, challenge conocido, shell que requiere
JavaScript, contenido vacío o demasiado corto). Playwright y Camofox leen el DOM
renderizado con una extracción determinista que elimina chrome de navegación y
elige el contenedor principal por texto, párrafos y densidad de enlaces; la
respuesta informa proveedor, intentos y evidencia. El parámetro
`provider=direct|playwright|camofox` permite diagnóstico determinista.

Camofox se agrega en **Configuración → Integrations → API Service**, eligiendo
`Camofox`, normalmente con `http://127.0.0.1:9377`. Es opt-in, diagnosticable con
`/health` más una apertura/cierre real de pestaña, y LlamaCode no instala ni inicia
su contenedor. La API key se guarda en `SecretStore`; `integrations.json` conserva
sólo una referencia y la clave se envía como Bearer cuando corresponde. Las
instalaciones anteriores que tenían la clave en JSON se migran automáticamente.
CloakBrowser sólo
puede registrarse como integración externa/manual: se guarda desactivado, no forma
parte del pipeline automático, no se descarga ni se redistribuye. Un operador que
decida usarlo debe revisar por separado su binario, licencia y riesgos.

El ejecutable de QA `qa_web_providers` permite probar servicios reales fuera de
`ctest`, sin convertir dependencias externas en requisito del build:
`qa_web_providers camofox https://example.com` (URL configurable con
`LLAMACODE_QA_CAMOFOX_URL`) o `qa_web_providers playwright https://example.com`
con `LLAMACODE_QA_PLAYWRIGHT_CMD` definido.

## Data Lab

Data Lab agrega un flujo local para convertir documentos en registros
estructurados. Define un esquema JSON, procesa una carpeta mediante
`DocumentExtractor`, genera prompts de extracción estrictos, valida tipos y
campos obligatorios de forma determinística y exporta JSON/CSV/SQLite desde la
UI. Los jobs quedan
persistidos en `AppLocalData/LlamaCode/data-lab/jobs/` y los documentos con
errores se mantienen en estado `needs_review`. El detalle del contrato está en
[`docs/data-lab.md`](docs/data-lab.md).

## Adjuntos (documentos + visión)

`DocumentExtractor` convierte adjuntos **pdf/office → markdown** vía sidecar
**markitdown** (con caché por md5), para inyectarlos al contexto del chat/agente. Con
un modelo de visión (server lanzado con `--mmproj`) también acepta **imágenes**.

## Robustez del server (watchdog + VRAM)

- **Watchdog**: auto-restart de `llama-server` ante crash (con tope de reintentos);
  `serverState` = `stopped|running|restarting|failed`.
- **Medidor de VRAM/stats en vivo**: poll async de `nvidia-smi` mientras el server
  corre (`serverStats`), para ver el consumo real.
- **Selección de GPUs**: en Configuración → GPU · Inferencia se pueden ver las
  GPUs NVIDIA detectadas, elegir la GPU de procesamiento (`--main-gpu`) y marcar
  qué GPUs reciben el modelo en VRAM (`--tensor-split`). La selección se guarda
  globalmente y se aplica al próximo inicio del servidor.
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
- **Deep Research**: investigación multi-consulta y multi-página con reportes
  persistidos; al finalizar actualiza la lista, selecciona el reporte nuevo y
  muestra una notificación automáticamente. La lista y el contenido muestran la
  fecha local del reporte. La consulta original queda visible y persistida como
  encabezado antes del reporte. El visor ajusta el texto al
  ancho disponible y reserva una columna propia a la derecha para la scrollbar. Antes de
  buscar, el modelo genera subconsultas concretas para fuentes primarias,
  productos, comparaciones y precios; luego se priorizan fuentes técnicas y se
  descartan portadas/categorías sin evidencia antes de consumir el cupo. Los
  extractos conservan snippets y ventanas alrededor de precios, stock y datos
  PCIe aunque aparezcan lejos del inicio del HTML; para compras en Argentina se
  priorizan además páginas de producto locales, precios en ARS y stock actual.
  Las fuentes comerciales y oficiales se intercalan para que ninguna categoría
  consuma por sí sola el cupo de páginas. La profundidad automática usa diez
  fuentes útiles y puede ampliarse hasta dieciséis; la síntesis exige tablas de
  alternativas comprables, precio, stock, tienda y limitaciones verificadas. El
  botón de inicio permanece deshabilitado hasta que modelo y agente estén listos.
  La búsqueda se ejecuta en dos rondas: tras la primera, el modelo analiza vacíos,
  candidatos omitidos y conclusiones débiles, y genera consultas de seguimiento
  por producto para tiendas, comparadores, manuales y foros técnicos. Se usa
  SearxNG cuando `LLAMACODE_SEARXNG_URL` está configurado. Sin SearxNG, las
  consultas se distribuyen explícitamente entre DuckDuckGo, Bing y Google HTML
  (Google puede aplicar CAPTCHA/bloqueo). Para pedidos de compra no se genera un
  veredicto final hasta reunir al menos dos ofertas comerciales que contengan,
  cada una, precio numérico y disponibilidad positiva explícita; una publicación activa
  no se interpreta como disponibilidad. Se permiten hasta tres rondas antes de fallar
  explícitamente en lugar de presentar una recomendación incompleta.
  Cada ronda produce learnings compactos con entidades, cifras, fechas y
  contradicciones; esos learnings alimentan la siguiente planificación y el
  informe final. Una reflexión supervisora decide si investigar más según las
  preguntas pendientes. El borrador final pasa por una auditoría independiente que lo
  corrige y vuelve a comprobar antes de guardarlo; si persisten errores técnicos
  conocidos o afirmaciones sin respaldo, el reporte falla en vez de publicarse.
  La síntesis exige trazabilidad afirmación/fuente/extracto, compatibilidad física
  real, costo total de plataforma y separación entre opciones nuevas y usadas.
  Las reglas determinísticas bloquean, entre otros casos, confundir publicación
  con stock, inventar precios, atribuir la alimentación de las GPU al VRM, negar
  NVLink en RTX 3090 o informar x16+x8 donde el manual especifica x8/x8.
  Los informes deben alcanzar profundidad mínima, incluir
  todos los hallazgos relevantes y cerrar con un apéndice de URLs consultadas.
  Las especificaciones exactas del modelo prevalecen sobre heurísticas generales
  por chipset.
- **Integrations**: registro unificado de **MCP Tool Servers** + **API services**
  (endpoint + key), con test de conexión.
- **ControlApi / headless**: toda feature es controlable por API local (target
  traversal), con variantes sin diálogo para automatización.
- **EvalSuite**: evaluación reproducible de modelos (importable como benchmark custom).
- **Mermaid**: render de diagramas en el chat (sidecar mermaid-cli).
- **Multi-idioma**: UI en español, inglés, chino, francés, italiano y alemán.
- **Inicio con Windows**: toggle en Configuración que registra el autoarranque por
  usuario; si también está activo **Minimizar a la bandeja**, el inicio automático
  abre la app oculta en el área de notificación.
- **Export/Import/Wipe** de datos de usuario por categorías.

## Lanzamiento del servidor (`LaunchPage`)

- **Vista previa del comando** con botón *Copiar*.
- **Iniciar servidor + agente** — levanta `llama-server` y el harness de agente.
- **Iniciar solo servidor** — solo `llama-server`, sin agente.
- **Puerto ocupado** — antes de iniciar, detecta si el puerto del perfil está en
  uso; si hay otro libre cercano, pregunta si se desea cambiar el perfil a ese
  puerto y recién después lanza.
- **VRAM insuficiente** — antes de iniciar un perfil GPU, estima si el GGUF +
  contexto entran en la VRAM libre actual. Si no entra limpio, muestra una alerta
  porque Windows puede usar memoria compartida y degradar fuertemente los TPS.
- **Endpoint OpenAI** — con el server corriendo muestra `http://<host>:<port>/v1` (read-only, seleccionable) + botón *Copiar*, para apuntar agentes externos (opencode, aider, etc.) al backend local.

## Gateway local para OpenCode, Claude Code y Claude Desktop

En **Configuración > Gateway · API**, LlamaCode puede exponer los perfiles de
lanzamiento locales mediante una API en `http://127.0.0.1:8088` (puerto
configurable):

- `GET /v1/models` lista IDs estables de perfiles.
- `POST /v1/chat/completions` ofrece la API OpenAI-compatible usada por OpenCode.
- `POST /v1/messages` adapta el protocolo Anthropic para Claude Code.
- En Configuración, **Configurar Claude Desktop** crea el perfil local de
  **Third-Party Inference**, con aliases `claude-llamacode-*` y todos los perfiles
  de lanzamiento disponibles. El alias se traduce de nuevo al ID estable de
  LlamaCode antes de cargar el modelo.
- Si un request pide otro perfil y auto-load está activo, LlamaCode hace el swap,
  espera que el modelo correcto quede listo y recién entonces reenvía el request.

Claude Desktop debe estar completamente cerrado al aplicar el cambio y volver a
abrirse después. La primera vez puede ser necesario habilitar **Developer Mode →
Configure Third-Party Inference**. El modo 3P es una configuración local no
oficial de Claude Desktop: Cowork/Code usan el gateway, mientras que funciones
dependientes de la nube de Anthropic pueden no estar disponibles. LlamaCode deja
un respaldo de cada JSON modificado en
`AppLocalData/LlamaCode/claude-desktop/backups/`.

El switch **Compartir server en la red local (LAN)** cambia el bind de loopback a
`0.0.0.0` y muestra la URL IPv4 privada anunciable (por ejemplo,
`http://192.168.1.20:8088`). Si todavía no hay una API key, LlamaCode genera una
automáticamente. La pantalla ofrece dos flujos:

1. **Otro LlamaCode en LAN**: crear allí un backend Cloud/OpenAI-compatible usando
   la URL LAN como Base URL, el ID del perfil remoto como modelo y la API key
   compartida mediante `LLAMACODE_GATEWAY_API_KEY`.
2. **OpenCode en LAN**: copiar desde LlamaCode un `opencode.json` completo con la
   URL `/v1`, catálogo de perfiles y credencial.

En Windows se debe permitir el puerto sólo para redes privadas cuando el firewall
lo solicite. Desactivar el switch vuelve a limitar el gateway a esta PC.

Desde otro equipo no hace falta crear el backend manualmente: en **Lanzar**,
**Usar un servidor LAN** envía un discovery broadcast, lista los LlamaCode que
comparten gateway y permite elegir uno de sus perfiles. Al confirmar, el cliente
crea o reutiliza un perfil remoto, guarda la credencial en `SecretStore` y pide al
servidor que cargue —o vuelva a iniciar— el perfil elegido antes de conectar el
agente local. Una vez aceptada la conexión, Chat, Agente, Investigación, Tasks y
Charla se habilitan también en el cliente LAN: su disponibilidad depende del
backend remoto activo y no de que exista un proceso `llama-server` local.

El botón **Abrir OpenCode GUI en mi GPU** permite elegir perfil y proyecto. LlamaCode
inyecta una configuración runtime mediante `OPENCODE_CONFIG_CONTENT`, selecciona
`llamacode/<launch-profile-id>` y pasa la API key por una variable de entorno. No
modifica el `opencode.json` global ni el del proyecto. OpenCode debe estar
instalado como aplicación Desktop. Si Desktop ya estaba abierto, hay que cerrarlo
antes de relanzarlo para que el nuevo proceso herede la configuración del gateway.

Configuración manual equivalente:

```json
{
  "$schema": "https://opencode.ai/config.json",
  "provider": {
    "llamacode": {
      "npm": "@ai-sdk/openai-compatible",
      "name": "LlamaCode local",
      "options": {
        "baseURL": "http://127.0.0.1:8088/v1",
        "apiKey": "local"
      },
      "models": {
        "<launch-profile-id>": {
          "name": "Modelo local",
          "tool_call": true
        }
      }
    }
  },
  "model": "llamacode/<launch-profile-id>"
}
```

## Process Lifecycle

El arranque por fases, la instrumentación Normal/Debug y el escaneo incremental
del catálogo están documentados en [`docs/startup-performance.md`](docs/startup-performance.md).

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

`build.bat` conserva la caché y los tracking logs para que los builds siguientes
sean incrementales, compila, despliega el runtime Qt (`windeployqt`) y regenera
los accesos directos. Sólo cierra `LlamaCode.exe` si bloquea el enlace; no mata
compiladores ni servidores de otras sesiones. Acepta config y la opción `NOPAUSE`:

```bat
build.bat NOPAUSE          REM Debug: release candidate (predeterminado)
build.bat Release NOPAUSE  REM Release: promoción estable explícita
```

Para subir la versión de la app y del flag de actualización:

```bat
bump-version.bat 0.1.2
bump-version.bat 0.1.2 --summary "Resumen corto" --changelog "Cambio A|Cambio B"
```

Compilar ya no incrementa la versión automáticamente. El versionado es una acción
explícita de release mediante `bump-version.bat`, para no invalidar CMake, recursos
y unidades C++ en cada build local.

Salidas:

| Config | Binario | Acceso directo | Icono |
|--------|---------|----------------|-------|
| Debug | `build\Debug\LlamaCode.exe` (release candidate, optimizado + símbolos + asserts) | `LlamaCode-Debug.lnk` | `assets\debug_icon.ico` (llama **roja**) |
| Release | `build\Release\LlamaCode.exe` (estable, optimizado, `NDEBUG`) | `LlamaCode.lnk` | `assets\app_icon.ico` (llama normal) |

El build predeterminado es Debug para probar y acumular varias versiones candidatas.
Release se reserva para una promoción estable explícita, una vez integradas y
validadas varias versiones de Debug. `build.bat Both` sigue disponible cuando se
necesitan ambos artefactos.

Debug conserva los símbolos PDB y las aserciones, pero usa optimización MSVC `/O2`
para que el candidato de uso diario no tenga la penalización de rendimiento de
un Debug tradicional (`/Od`). También enlaza contra el runtime optimizado de Qt
y MSVC (`/MD`); así mantiene el diagnóstico del código propio sin el arranque
lento de las DLL de instrumentación de Qt Debug.

El icono rojo del Debug va embebido en el `.exe` (taskbar/explorer) vía
`app_icon.rc` + `#ifdef LC_DEBUG_ICON` (CMake define `LC_DEBUG_ICON` solo en
config Debug), y también se usa en el `.lnk`, la ventana principal y el splash.
Esta selección debe depender de la configuración de LlamaCode mediante
`LC_DEBUG_ICON`, no de `QT_DEBUG`: Qt puede ser una build Release aunque la app
se compile en Debug.

El área de notificación usa `assets/tray_icon.png` únicamente en Release. Debug
conserva `assets/debug_icon.ico` también en el tray para mantener su identidad
visual diferenciada.

Los accesos directos generados por `update-shortcut.ps1` escriben
`AppUserModelID = LlamaCode.Desktop.App`, el mismo que fija el proceso en
Windows. Si ya existe un acceso pineado en la taskbar apuntando al mismo
`build\<Config>\LlamaCode.exe`, el script también lo actualiza para que Windows
agrupe la ventana abierta con el icono pineado.

> Tras tocar código siempre recompilar — el QML va embebido en el binario vía `qt_add_qml_module`.

### Modos de ejecución y arranque

La aplicación inicia por defecto en **Normal**: no activa muestreo periódico de
memoria/CPU ni escritura de telemetría de diagnóstico. El splash nativo cubre la
carga de QML y el escaneo inicial; después del primer arranque las páginas se
precalientan gradualmente en memoria para que cambiar de sección no tenga que
crear el árbol QML por primera vez.

En Configuración → Rendimiento y diagnóstico se puede activar **Dev**. Ese modo
registra fases de arranque, RSS/memoria privada, CPU del proceso, intervalo del
event loop y pausas detectadas en:
`%LOCALAPPDATA%\LlamaCode\performance.jsonl`.
También se puede iniciar explícitamente con `--dev-mode` o volver al modo normal
con `--normal-mode`.

### Manual

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\msvc2022_64"
cmake --build build --config Release --parallel
```

### Calidad de código

- `tests.bat Debug` reutiliza `build_tests`, compila incrementalmente y corre toda la suite Qt Test; pasar `Release` sólo para validar la promoción estable.
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
│   ├── app_icon.ico / debug_icon.ico / app_icon.png / tray_icon.png
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

## Workflows de ingeniería

Los presets `Investigar bug`, `QA con regresión`, `Auditar documentación`,
`Revisar cambios`, `Autoprompt: planificar, construir y verificar` y `Preparar
release Debug` se instalan desde la sección Tasks.
Son definiciones declarativas sobre el mismo motor de workflows que ya soporta
aprobaciones, snapshots, pasos paralelos, gates estructurados, reparaciones
acotadas, reanudación y rollback. No conocen
aplicaciones concretas ni coordenadas: el agente resuelve cada paso usando las
tools y permisos del workspace.

La especificación y los perfiles de seguridad están en
[`docs/agent-workflows.md`](docs/agent-workflows.md).

## Tasks (macros configurables + scheduler cron)

Sección **Tasks** (en la NavBar, arriba de Benchmark): macros que el usuario
configura, guarda y ejecuta. **No son macros tontas** — no graban coordenadas
crudas estilo TinyTask, sino que delegan en el agente IA: cada Task guarda un
**objetivo en lenguaje natural** + **pasos de referencia**, y en la ejecución el
agente re-deriva las acciones con sus tools (browser MCP, shell, mail, etc.) y
**se adapta** si un botón, elemento o archivo cambió de lugar o de nombre.

### Modelo de datos (`TaskStore`)

- `id`, `name`, `description` (el objetivo), `profileId` (perfil de agente opcional).
- `prePrompt` y `postPrompt` opcionales: instrucciones agénticas antes de ejecutar
  la Task y una verificación posterior (por ejemplo, chequear que la salida tenga
  evidencia suficiente o pedir una validación del resultado).
- `verifyProfileId` permite usar otro LaunchProfile como revisor. Con
  `autoDifficultyRouting`, el ejecutor conserva las verificaciones simples y se
  escala al revisor cuando el contexto activo, la cantidad de archivos editados,
  los fallos consecutivos o los ciclos indican dificultad alta; dos señales medias
  combinadas también disparan el escalado.
- `steps[]`: cada paso `{kind, intent, ref}` con `kind` ∈
  `instruction|browser|shell|mail|desktop`. Los pasos `browser` graban un skill
  reproducible vía Playwright codegen (reusa el modo *teach* del browser).
- `silentUnlessError`: ejecuta sin popup cuando termina bien; si falla, muestra el
  error. Con el modo desactivado, toda ejecución manual muestra un resumen final.
- `scheduleEnabled` / `scheduleCron`, `lastRunAt` / `lastRunStatus` /
  `lastRunSummary`.
- Persistencia JSON en `AppLocalData/LlamaCode/tasks/tasks.json`.
- `composePrompt()` arma el prompt-objetivo con la consigna explícita de que los
  pasos son **guía, no guion literal** (replay adaptativo), incluyendo el
  `prePrompt` cuando existe.

### Ejecución (manual o programada)

`runTask()` unifica el botón ▶ y el scheduler con auto ciclo de vida del agente:

- Si el **agente ya corre**, lo usa tal cual (no lo apaga).
- Si **no hay agente**, auto-inicia servidor + agente (perfil de la Task o el
  activo), ejecuta al quedar listo y **lo apaga** al terminar el turno.
- El botón de ejecutar queda deshabilitado mientras servidor/agente están
  cargando, el servidor aún no está `ready`, el agente está ocupado o ya hay una
  Task en curso.
- Cada ejecución prepara una sesión limpia del agente antes de enviar el prompt,
  para no heredar historial previo ni disparar compactaciones por conversaciones
  ajenas a la automatización.
- En modo **Escritorio foreground**, la corrida opera sobre la pantalla real con
  las tools nativas `desktop_*` (ventanas, controles UIA, captura, mouse y
  teclado) y también mantiene Playwright disponible en foreground/headed para
  flujos web que formen parte de la misma automatización. Playwright no reemplaza
  `desktop_*` para aplicaciones nativas de Windows. Las Automatizaciones de
  escritorio puro recortan el catálogo al set necesario (`desktop_*`,
  `recent_actions`, `ask_teacher`) para caber en perfiles 8k y evitar fallback
  textual innecesario. Las tools de click devuelven `trace` con `pointer` y
  `target` para que el agente pueda validar qué accionó.
  Si una entrada de teclado ya incluye la acción final (por ejemplo
  `desktop_type "2+2="`), el runner bloquea una tecla de confirmación redundante
  (`ENTER`/`=`) para evitar que Calculadora repita la última operación.
- En modo **Navegador background**, el Teach se graba con browser foreground de
  Playwright y evidencia visual por acción; la ejecución posterior usa esa receta
  como guía semántica junto con las tools de navegador. Si la página cambia y el
  agente logra resolverlo, el artefacto Teach guarda el aprendizaje para próximas
  corridas.
- Si `llama-server` rechaza el primer request OpenAI-compatible con HTTP 400, el
  agente reintenta una vez en modo compatible sin campos opcionales del payload,
  conservando mensajes y tools para no marcar la Task como fallida por diferencias
  de soporte entre builds. Si esa variante también falla porque la build no
  acepta `tools` nativo, cambia automáticamente a un protocolo textual headless:
  el modelo pide `TOOL_CALL {...}`, la app ejecuta la misma tool interna y devuelve
  `TOOL_RESULT` para continuar el loop sin depender del soporte OpenAI tools del
  servidor. En ese modo, los resultados de tools se compactan antes de reenviarse
  al modelo para no provocar HTTP 400 por contexto/payload excesivo.
- Mientras corre, la UI muestra la fase (`ejecutando` o `verificando`). Si hay
  `postPrompt`, se envía como segundo turno al terminar la ejecución principal y
  la Task no se marca como finalizada hasta completar esa verificación. El editor
  permite elegir un perfil revisor distinto y decidir entre usarlo siempre o sólo
  cuando el router de dificultad pide escalado. El mismo routing se aplica al
  chequeo de objetivo de los bucles.
- Para cualquier automatización de escritorio enseñada, el prefijo seguro de
  teclado de la receta (por ejemplo `WIN → nombre de app → ENTER`) se
  reproduce en paralelo al primer prefill: no hay nombres de aplicaciones
  hardcodeados ni clasificación del contenido como sensible: se respeta la
  secuencia que el usuario decidió enseñar. El replay rápido corta sólo al llegar
  a una acción todavía no soportada por este prefijo estructurado (por ejemplo un
  click); desde allí el agente continúa, verifica y se adapta.
- La finalización del turno no equivale por sí sola a éxito: si el objetivo
  requiere una fuente externa (web, browser, archivos, comandos, etc.) y no hubo
  uso de herramientas, o si la respuesta final declara que no pudo acceder/usar
  herramientas/completar o contiene un error de transporte del server, la Task se
  marca como `error`.
- Al terminar, la UI muestra popup de resumen salvo que `silentUnlessError` esté
  activo y el resultado sea correcto. En errores siempre muestra popup. El popup
  incluye **Ver trabajo**, que abre la traza de esa corrida: prompts enviados,
  eventos del agente, tool calls/resultados, errores y respuesta final cuando el
  backend los emite.
- La opción **Reintentar** relanza la Task completa y luego vuelve a ejecutar el
  postprompt si estaba configurado.
- Al iniciar, cualquier estado `running` persistido por un cierre, crash o rebuild
  anterior se recupera como ejecución interrumpida; ninguna Task queda mostrando
  `Ejecutando...` de forma permanente tras reiniciar la aplicación.
- Sin perfil asignable → marca `lastRun = "error"`.

El cierre del ciclo se apoya en la señal `IAgentBackend::turnFinished` (emitida al
completar cada turno), que marca `lastRun = "ok"` al completar la fase final y
apaga el agente auto-iniciado.

### Scheduler cron (`CronSchedule` + `TaskScheduler`)

- Parser cron puro de 5 campos `min hora díaMes mes díaSem`: `*`, listas `a,b`,
  rangos `a-b`, pasos `*/n` y `a-b/n`, día de semana `0`/`7` = domingo, semántica
  OR de díaMes/díaSem cuando ambos están restringidos.
- `TaskScheduler` evalúa por minuto (timer in-app, de-dup por minuto) y dispara
  `runTask` en cada Task vencida. Toggle global persistido; corre mientras la app
  esté abierta.
- Una programación fallida conserva `retryCount` y `nextAttemptAt`; reintenta con
  backoff exponencial persistente (60 s por defecto, máximo 24 h y tres intentos)
  antes de volver a depender del cron normal. Un éxito reinicia el contador.
- Ejemplos: `0 9 * * *` (9:00 diario) · `*/15 9-17 * * 1-5` (cada 15 min, 9–17h,
  lun–vie) · `0 0 1 * *` (día 1 de cada mes).

## Benchmarking

Los perfiles marcados como **BEST (⚡)** son recomendaciones curadas a partir de
benchmarks. Se muestran antes que los favoritos en todos los selectores de perfiles.

En benchmarks de agente, la reparación BCB espera el `turnFinished` del turno
correctivo y no considera estancado un backend que todavía está ocupado en
prefill, herramientas o cierre de stream. El watchdog de reparación sólo actúa
cuando el backend está libre y no hubo cambios reales en el workspace durante
180 segundos; el timeout duro de la corrida sigue siendo el límite de seguridad.

Módulo para comparar quants y perfiles de forma sistemática: mide RAM, VRAM, velocidad y calidad relativa con resultados persistidos en tabla.

Cada perfil de benchmark usa una escalera adaptativa de VRAM. Primero intenta
la mayor ocupación útil habilitando `fit` y quitando los flags explícitos de
colocación (`n-gpu-layers`, `n-gpu-layers-draft`, `tensor-split` y overrides de
expertos), para que `llama.cpp` pueda medir ambas placas y decidir el reparto
VRAM/RAM según los pesos reales, incluidos los expertos MoE; ante un OOM limpia
el servidor y reintenta con un margen ligeramente mayor. Después reduce
gradualmente contexto, batch y ubatch. La configuración efectiva y el intento
de memoria quedan guardados junto al resultado. Esta adaptación no modifica los
lanzamientos manuales.

Evaluaciones de modelos candidatas:

El procedimiento completo y reutilizable para comparar un nuevo modelo, binario, perfil o harness está en el [Manual de benchmarking](docs/benchmark-manual.md). La matriz de perfiles y sus resultados históricos se mantiene en [docs/benchmark-profile-matrix.md](docs/benchmark-profile-matrix.md). El [ranking por caso de uso y catálogo de mejoras](docs/benchmark-ranking-and-use-cases.md) resume qué perfiles sirven para calidad, velocidad, visión, contexto, VRAM y warm-cache. Para validar contexto largo, aislamiento y KV cache contra un servidor real, usar el [probe de QA de KV cache](docs/kv-cache-qa.md).

Para aislar el costo del harness existe la suite custom **Harness Context A/B v1**
(`harness_context_tools_ab_v1`, [JSON bundleado](assets/benchmarks/custom/harness_context_tools_ab_v1.json)).
Ejecutarla con target **Agent**, el mismo launch profile y tres pasadas, repitiendo
por separado con `agent-chat`, `agent-intermedio` y `agent-maximo`. Compara calidad
ejecutable, primer intento, TTFT, tokens, tool calls, corrección de la secuencia
(`toolCallQuality`), reparaciones y RAM/VRAM. El script
`tools/harness_ab.ps1` automatiza el barrido con cinco pasadas intercaladas por
defecto, semilla persistible y validación de balance; una cantidad menor sirve
para smoke/A-B rapido. El control de Chat puro debe medirse aparte porque el runner de agente exige un
artefacto de archivo por tarea.

Los perfiles externos de vLLM siguen la misma escalera HE0 → HE20 → BCB, pero
LlamaCode sólo conecta al endpoint OpenAI-compatible: no mide como `llama-server`
ni descarga el modelo. Para DFlash2, comparar la candidata de 7 tokens contra el
control autoregresivo con la misma suite y registrar aparte la versión/parches de
vLLM; ver [`docs/benchmark-vllm-dflash2.md`](docs/benchmark-vllm-dflash2.md).

- [BigBang-v1 Q4_K_M (2026-08-10)](docs/research/bigbang-v1-q4km.md): perfil
  experimental con mmproj bf16 y cuatro variantes de benchmark para comparar
  contra KAT-Coder. El MTP está embebido en el GGUF y se prueba con
  `--spec-type draft-mtp`; requiere llama.cpp b10262+.

- [Ternary Bonsai 27B (2026-07-27)](docs/research/ternary-bonsai-27b.md):
  comparación local contra MAX-Q y modelos del tier 5–8 GB; por ahora se recomienda
  seguimiento experimental, no perfil agente/coding predeterminado.

### Flujo de uso

1. En **Perfiles**, marcar cada `LaunchProfile` que se quiera dejar **Para
   benchmark**; la marca queda persistida como cola de candidatos pendientes.
   Las candidatas de la matriz de perfiles ya vienen pre-marcadas y se pueden
   desmarcar individualmente.
2. En **Benchmark**, pulsar **Seleccionar 🏆 benchmark (N)** para cargar todos
   los perfiles marcados de una vez, o seleccionar perfiles manualmente.
3. Elegir modo de prueba: **Corta** (~30 s) o **Completa** (1–5 min).
4. Ejecutar: UNLZ_Llamacode lanza cada perfil en secuencia, corre los prompts, registra métricas.
5. Ver resultados en tabla comparativa; exportar o guardar para comparaciones futuras.

Cuando una suite se repite en varias pasadas, el benchmark detiene y vuelve a
cargar el servidor entre pasadas para aislar el estado de MTP/KV-cache. Si el
backend se reinicia durante una respuesta, la corrida se clasifica como
infraestructura y no se envía a reparaciones de calidad.

En benchmarks de agente con varias tareas, cada tarea tiene un artefacto Python
propio (`solution_<task-id>.py`). El grader evalúa ese archivo exacto: así una
respuesta no puede sobrescribir `solution.py` de tareas anteriores ni recibir
crédito por el archivo de otra tarea. El sondeo de aceptación sólo vuelve a
evaluar cuando cambian los artefactos del usuario y excluye `.llamacode/`.
Además, el contador previo a herramientas reconoce snapshots acumulativos de
streaming y aplica watchdogs para que una cancelación incompleta no deje una
corrida headless bloqueada.

El informe de la repetición headless del 2026-08-15, con perfiles, comandos,
resultados y diagnóstico de infraestructura, está en
[`docs/benchmark-rerun-2026-08-15.md`](docs/benchmark-rerun-2026-08-15.md).

Para medir concurrencia real, seleccionar exactamente un perfil y usar **Medir
concurrencia**. La herramienta crea copias editables del perfil para cada valor
de `parallelSlots`, lanza varias requests simultáneas contra el mismo servidor y
persiste `aggregateTps`, TTFT, latencia máxima, requests exitosas y RAM/VRAM en
una carpeta `concurrency_<timestamp>`. El perfil original no se modifica. Esto
mide el caso de uso de subagentes; el benchmark normal, en cambio, ejecuta sus
prompts secuencialmente.

Para comparar varias suites personalizadas en una sola operación, abrir
**Pro-Benchmarks**, marcar los benchmarks deseados (o **Todos**) y pulsar
**Iniciar benchmark**. La aplicación ejecuta la matriz suites × perfiles de forma
secuencial y conserva una fila/resultados independientes por combinación; cancelar
la corrida también descarta las suites pendientes.

La opción **Escalera HE0 → HE20 → BCB (custom)** permite elegir tres suites
personalizadas importadas y ejecutarlas en ese orden sobre cualquier selección de
perfiles. HE0 funciona como compuerta: un perfil que no la supera no continúa a
HE20, y BCB sólo se habilita para perfiles con HE0 y HE20 válidos. La nueva sección
**Ranking** agrupa la última corrida de cada etapa por perfil, objetivo y nivel de
agente; sus columnas HE0, HE20, BCB, tiempos, TPS, RAM y VRAM se pueden ordenar.
En el primer arranque de esta versión, la app importa las filas tabulares con
scores desde `docs/benchmark-results.md` y `docs/benchmark-results-history.md`;
la migración es idempotente y marca esos datos como históricos importados. Las
filas `Pendiente`, narrativas o sin score no se convierten en resultados.

Las suites custom pueden declarar un timeout recomendado. La UI avisa cuando el
límite elegido es menor: un timeout durante la reparación conserva los checks ya
medidos, pero se identifica como `Timeout` y no como un fallo final de calidad. La
suite `Stress largo y difícil` recomienda al menos 900 s por corrida.

El timeout duro se controla con un watchdog periódico de pared durante toda la
pasada, incluida la generación activa. Al vencer, cancela la generación, aborta
la request HTTP, fuerza la limpieza del `llama-server` y continúa con el siguiente
perfil; el resultado queda persistido como `failureKind=timeout`. En benchmarks
de agente no se usa un corte por ausencia de tokens: una generación puede estar
evaluando prompt, ejecutando una tool/MCP o cerrando el stream. El scheduler
espera `turnFinished`, reintenta de forma segura el mensaje transitoriamente
rechazado por `Hay un turno en curso` y sólo avanza de tarea cuando el turno fue
aceptado y finalizó.

En benchmarks de agente, el checkbox **Thinking** es la configuración efectiva de
la corrida: si está apagado, se inicia o recarga el servidor con `--reasoning off`
y no se reutiliza un servidor arrancado con el estado contrario. Para perfiles
KAT-Coder, si falta el template de herramientas, el benchmark instala o actualiza
automáticamente `kat-coder-tools.jinja` antes de iniciar el servidor.

Para construir un subset corto y reproducible de HumanEval con mayor complejidad
estructural, usar `python tools/select_humaneval_hard.py <HumanEval.jsonl>
<salida.jsonl>`. El selector genera además un manifest con los ids y métricas;
emplea la solución canónica sólo para ordenar y conserva sin cambios los registros
oficiales que importa y ejecuta LlamaCode.

`tabla_best_25` es el ranking de screening rápido de perfiles de agente sobre
`HumanEval (1 ítems)`. Usa categorías exclusivas por TPS: **Fast** (>60, hasta
10 perfiles), **Balanced** (>40 y <=60, hasta 10) y **Quality** (>5 y <=40,
hasta 5). Dentro de cada grupo prioriza calidad final y luego TPS. Los perfiles
que fallan o quedan sin resultado no entran; un perfil válido con calidad completa
pero sin TPS persistido entra al final de **Quality** con la marca `TPS pendiente`,
sin inventar una velocidad ni desplazar los cinco puestos medidos.

`tabla_best_modelos_speed` es la primera etapa por GGUF: resuelve el archivo GGUF
real desde el catálogo y conserva como máximo **10 perfiles** que pasaron el
screening HumanEval/0. La segunda etapa ejecuta HumanEval/20 únicamente sobre los
**3 mejores perfiles de cada GGUF**, más todos los perfiles marcados ⚡ **BEST**
como controles; de esta forma la calidad larga compara variantes del mismo quant
sin perder las referencias globales ya promovidas.

`tabla_best_modelos_quality` toma esos mismos perfiles y sus corridas válidas de
`HumanEval (20 ítems)`, ordena por calidad final, primer intento y tiempo, y limita
a tres perfiles por GGUF. Conserva los fallos de calidad medidos (por
ejemplo 19/20), mientras que infraestructura y timeouts quedan fuera del ranking
pero siguen visibles en el historial.

La regla operativa queda fijada así: `HumanEval/0` hace el screening de hasta 10
perfiles por GGUF; sólo los 3 mejores de cada GGUF y todos los ⚡ BEST pasan a
`HumanEval/20`. El ranking final `best` compara todos esos finalistas y controles,
y prioriza, en orden, calidad final, TPS, tiempo total, score del primer intento,
TTFT y menor cantidad de reparaciones. Los empates terminan con un orden estable
por nombre de perfil.

Laguna S 2.1 usa `assets/chat-templates/laguna-tools-v24.jinja`, una versión
actualizada del template nativo con soporte de tools y loop-guard. Se aplica a
los dos perfiles locales que comparten el GGUF Laguna. La corrida inicial mostró
que el modelo podía desviarse a nombres de función y archivos inventados; el
prompt de benchmark ahora exige conservar la firma del preámbulo y usar un
archivo Python canónico. La reejecución del 12 de agosto de 2026 pasó 1/1 sin
reparaciones, con 29,9 TPS y 34,6 s hasta el primer intento.

Las rutas de las herramientas locales se normalizan antes de ejecutar y antes de
volver a enviarlas al chat-template. Esto evita que saltos de línea emitidos por
un modelo (por ejemplo `\nsolution.py\n`) creen rutas fantasma y contaminen el
historial. Si una tool queda sin actividad o repite un fallo, el gobernador corta
el turno de forma recuperable y el benchmark registra el perfil como
`infrastructure`/`timeout`, permitiendo que la cola continúe.

La tabla histórica también muestra `T No Gen.`: el tiempo total de la corrida
menos el tiempo de generación medido por el backend. Incluye carga, espera,
tool-calls, escritura, validaciones y reparaciones; sirve para distinguir TPS
alto de latencia end-to-end real.

En benchmarks de agente también registra, desde el primer prompt, `1ª Tool`,
`1ª Escritura` y `1ª Evaluable`: el instante de la primera tool-call, del primer
archivo escrito y de la primera evaluación con criterios disponibles. Son
métricas independientes de TPS y ayudan a explicar la latencia práctica.

Para DeepSeek V4 dual se mantienen en la matriz de screening tres configuraciones
comparables: la base `--tensor-split 1,0`, una variante experimental con expertos
de las capas 0–8 residentes en CUDA0 y otra que prueba el reparto `1,1` con KV
q4. Las variantes sólo avanzan a `tabla_best_modelos_speed` y HumanEval/20 si
superan HumanEval/1; una salida inválida, fallo de infraestructura o timeout
queda registrada pero fuera del ranking.

BigCodeBench-Hard se prepara con `python tools/prepare_bigcodebench_hard.py
<dataset.parquet> <salida.json>`. El pack propio resultante usa el split Instruct,
selección con seed fija, excluye tareas de red/procesos y dependencias ausentes, y
valida cada tarea ejecutando previamente su solución canónica con los tests
oficiales. La referencia no se guarda en los prompts generados. El análisis
detallado de los seis perfiles medidos está en
[`docs/informe-bigcodebench-hard-modelos.md`](docs/informe-bigcodebench-hard-modelos.md).

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

Las corridas de Tasks también guardan telemetría del harness en su historial:
tokens de prompt/generación, tiempo de pared, fases, llamadas de tools y bytes de
resultados. `read_file(compact=true)` ofrece una vista efímera compacta para
explorar lenguajes con llaves; valida balance y literales, vuelve automáticamente
al texto exacto ante cualquier duda y exige releer el rango original antes de
editar. Cuando el CLI `tree-sitter` y la gramática correspondiente están disponibles,
la vista compacta valida primero el árbol sintáctico; si no, conserva el validador
lexical seguro. `project_brain` guarda sólo metadata regenerable y SHA-256 (rutas,
tamaños, fechas y extensiones), reutiliza archivos sin cambios y reporta el delta
para evitar redescubrir la estructura del workspace sin copiar código.
Los workflows JSON disponen de runner reanudable con snapshots, condiciones,
pausas de aprobación, cancelación y presupuesto de iteraciones/tiempo.
La definición se edita desde Procesos; durante la ejecución la UI muestra el paso
activo y ofrece Aprobar/Rechazar. Los pasos `tool` se ejecutan directamente por el
runner nativo (con confinamiento y aprobación para acciones destructivas); los
pasos `parallel` lanzan subagentes reales y reúnen sus resultados antes de seguir.
El snapshot queda en la Task y el historial, y una corrida interrumpida se detecta
y reanuda al volver a estar disponible el agente. En Procesos, el botón **A/B**
ejecuta automáticamente baseline y candidato con el mismo Task; Historial conserva
los deltas de tokens, tiempo y bytes de tools. Las métricas corresponden al
intervalo de cada corrida, no al acumulado de la sesión del backend.

El editor de Procesos incluye una vista visual sincronizada y sin pérdida con el JSON:
permite crear nodos, elegir tipo, destino y prompt, conserva campos avanzados no
representados y valida todas las rutas con el mismo `WorkflowEngine` usado al ejecutar.
El scheduler usa un companion sin UI (`--scheduler-daemon`) con lock, IPC, registro de
inicio de sesión y heartbeat: sigue evaluando cron con la ventana cerrada, despierta
una única instancia y auto-inicia el perfil del proceso antes de correr.

Benchmark incluye la suite versionada **Agent efficiency E2E v1**, con tareas
Python, TypeScript/Node y C++ y aceptación por archivos/comandos. Esto permite
comparar versiones con la misma carga y guardar calidad, tiempo, tokens y tools. El
primer resultado exitoso por suite/perfil/target se adopta como baseline automático;
los siguientes guardan su referencia y deltas de tiempo y calidad.

### Persistencia y vista

- Resultados en JSON (`AppLocalData/LlamaCode/benchmarks/{timestamp}.json`).
- Vista tabla en `BenchmarkPage.qml`: columnas ordenables, filtro por perfil/quant/fecha
  y **QPM (calidad por minuto)**. QPM usa el score final relativo, el tiempo hasta el
  primer intento (`timeToFirstAttempt`) y una penalización por reparaciones; no incluye
  en la velocidad comparable el tiempo de reintentos, backend caído o grader. Una
  corrida sin tiempo o fallada no recibe un score inventado.
- Los estados de ejecución se separan en `Calidad`, `Infra` y `Timeout`. Un fallo del
  modelo/evaluador de aceptación no se mezcla con una caída de `llama-server`, un
  error de tool-call o el vencimiento del límite de pared. Los resultados nuevos
  persisten además `failureKind` (`quality`, `infrastructure`, `timeout`, `none`),
  mientras que la UI conserva compatibilidad con resultados históricos mediante
  `failureStage`.
- `elapsedSec`/`totalTime` es el tiempo de pared completo, incluyendo setup y carga;
  `setupSec` mide hasta el primer prompt. `timeToFirstAttempt` mide desde ese prompt
  hasta el primer resultado aceptado. `measurementPhase` distingue la primera pasada
  fría (`cold`) de las pasadas calientes (`warm`).
- `comparison.json` conserva medianas frías y calientes, y compara perfiles por la
  mediana caliente cuando hay pasadas 2+ (`comparisonTimeMetric` =
  `warmTimeToFirstAttempt`); si no, usa `timeToFirstAttempt`. La primera pasada no
  desaparece: queda disponible para diagnosticar costo de arranque. `comparisonTimeChangePct`
  es el delta correspondiente y `elapsedChangePct` se conserva como alias.
- Una suite custom de un solo ítem se identifica como **smoke test** y la UI advierte
  que no sirve para rankear calidad. Para comparar perfiles usar al menos 10–20 ítems;
  las importaciones HumanEval completas o de 20 ítems quedan disponibles sin impedir
  pruebas rápidas de un caso.
- Las importaciones de packs públicos muestran la cantidad y las tareas incluidas;
  copias exactas importadas varias veces se agrupan en la lista sin borrar sus archivos.
- Exportar a CSV desde la UI.
Además, Benchmark ofrece **Server Speed v1**, una medición nativa separada de
la calidad E2E: corpus versionado por categoría, PP/TG, TTFT, ITL y sus
distribuciones, fases cold/warm, barrido de prefill 2K–64K, concurrencia y
comparación A/B intercalada con control A/A. Cada corrida guarda sus condiciones
(hash del corpus, seed, perfil efectivo, hardware y parámetros) junto con
`metadata.json` y `comparison.json`. Ver
[`docs/server-speed-benchmark.md`](docs/server-speed-benchmark.md).

### Tabla de ejemplo

| Quant | Score | Δ baseline | t/s gen | RAM | VRAM |
|-------|-------|------------|---------|-----|------|
| Q8_0 | 92/100 | base | 20 | 2 GB | 28 GB |
| Q6_K | 90/100 | −2.2% | 25 | 2 GB | 22 GB |
| Q5_K_M | 86/100 | −6.5% | 30 | 2 GB | 18 GB |
| Q4_K_M | 80/100 | −13.0% | 38 | 2 GB | 14 GB |
| IQ4_XS | 77/100 | −16.3% | 42 | 2 GB | 12 GB |
| Q3_K_M | 65/100 | −29.3% | 55 | 2 GB | 9 GB |

## Evidencia reproducible de corridas

Desde el Historial de Tasks se puede exportar un paquete JSON versionado con la
traza persistida, métricas, reportes de tools, workflow, receipts, versión del
producto y un hash SHA-256 por corrida. La exportación no reejecuta la Task ni
incluye secretos. Ver [`docs/evidence.md`](docs/evidence.md).

## Rendimiento multi-GPU

El diagnóstico de hardware conserva la topología de cada GPU (`gpus`), un
`hardwareFingerprint` y una recomendación explicable de `split-mode` y KV cache.
Para Ingi Charla también calcula una reserva de voz: la GPU más débil conserva
2 GiB de margen para STT/TTS/auxiliares (4–5 GiB si el TTS local elegido es
 Qwen3 o Inflect CUDA). Pocket TTS corre en CPU y no agrega reserva; `llama-server` recibe la capacidad restante de todas las
GPU. Esto permite mantener una conversación y usar el agente para operar la PC
sin cargar el modelo de voz sobre la GPU que sostiene la mayor parte del LLM.
En enlaces PCIe débiles prioriza `layer`; con enlaces rápidos habilita la prueba
de `tensor`. La recomendación no reemplaza una medición: los benchmarks deben
comparar prefill (`pp/s`), generación (`tg/s`), TTFT, VRAM por GPU y estabilidad
con el mismo modelo, prompt y versión de `llama.cpp`.

En benchmarks, la ocupación no se evalúa sólo por VRAM agregada: se registra la
VRAM de cada GPU, porque una distribución 10/22 GB puede ser peor que 16/16 GB
aunque ambas sumen lo mismo. La escalera adaptativa intenta primero equilibrar
los dispositivos y sólo baja la presión después de observar un OOM real.

El contrato y las reglas están documentados en
[`docs/multi-gpu-performance.md`](docs/multi-gpu-performance.md).

## Auto-tuning de parámetros

Búsqueda automática de los flags de `llama-server` (`ngl`, `batch`, `ubatch`,
`flash-attn`, `cache-type-k/v` y, en CUDA multi-GPU compatible, `split-mode`)
que maximizan **tok/s** sin degradar la
**calidad**. Optimizador TPE-lite (Parzen discreto) con **gate de calidad** y
validación PPL opcional: a diferencia de *llama-launcher v1.3*, tunear el quant
de KV cache solo por velocidad no colapsa al quant más bajo, porque la pérdida
penaliza fuerte caer bajo el umbral. Si existe `llama-perplexity` junto al
binario y hay corpus local, los trials que tocan cache K/V se validan contra la
PPL baseline con tolerancia default del 3%.

- Corre `N` trials en un puerto scratch (lanza/mide/mata el server por candidato, en un `QThread` aparte para no congelar la UI).
- Mide throughput de `timings.predicted_per_second` (`/completion`) y califica la salida con substrings estilo EvalSuite.
- Modo **Tune CPU**: fuerza `-ngl 0` y explora `threads`, `batch`, `ubatch` y cache K/V para equipos sin GPU.
- En perfiles MTP/DFlash con capacidad declarada, **Adaptive speculation** es
  opt-in: conserva `spec-draft-n-min` y auto-tunea `spec-draft-n-max` entre el
  mínimo y 9. No se habilita por defecto ni se emite contra un binario que no
  anuncie `--spec-draft-adaptive`; así una build oficial sin el parche no queda
  marcada como compatible por accidente.
- Al terminar **clona** el perfil en uno nuevo `-tuned` con la mejor config en `extraArgs`; el original queda intacto.
- UI: `ProfilesPage` → **Auto-tune**, **Tune CPU** / **Cancelar tune** + estado en vivo.

Detalle completo en [`docs/tuner.md`](docs/tuner.md).

## Habilidades portables

El agente nativo descubre bundles `SKILL.md` globales y por proyecto con carga
progresiva: mantiene sólo nombre y descripción en el catálogo y abre las
instrucciones completas cuando resultan relevantes. Las habilidades del proyecto
en `.llamacode/skills/` pueden reemplazar una global del mismo nombre, pero nunca
amplían los permisos de tools ni el confinamiento. La vista **Agente → Skills**
permite inspeccionarlas. El ejecutable incluye siete skills iniciales:
autoprompt-coding, revisión bibliográfica, lectura crítica, diseño experimental,
verificación de citas, revisión por pares y análisis reproducible. Formato,
límites y ejemplo en
[`docs/skills.md`](docs/skills.md).

Las sesiones del agente nativo forman un árbol persistente: una rama conserva
`parentSessionId`, profundidad y mensaje de origen. Se puede bifurcar la sesión
completa desde el menú o un turno concreto desde la burbuja del usuario, sin
alterar la rama original.

Al enviar el primer prompt de una sesión nueva, LlamaCode asigna automáticamente
un título breve derivado de su objetivo (hasta tres palabras). El título queda
persistido y nunca reemplaza uno renombrado manualmente por el usuario.

### Salas multiagente

La vista Agente incluye **Sala**, un timeline persistente donde el usuario, el
coordinador y los especialistas aparecen como participantes identificados. Cada
evento conserva tipo, autor, audiencia, correlación y timestamp; los eventos
dirigidos sólo entran al contexto compacto de su audiencia. Las menciones
`@id`/`@nombre` se registran como handoffs.

Los presets `/review`, `/council` y `/research` crean el roster apropiado y
despachan un contrato de coordinación al agente nativo, que reutiliza la tool
`task` y sus worktrees para el trabajo paralelo. `/review` separa implementador
con escritura y revisor de sólo lectura; ningún grant puede ampliarse después de
creado, sólo reducirse. Acciones externas y destructivas nacen deshabilitadas.
El resultado final del coordinador vuelve al timeline como `decision` o `error`.

Las salas viven en `AppLocalData/LlamaCode/agent-rooms/`: metadata en
`rooms.json` y eventos append-only en `events/<roomId>.jsonl`. El QObject
`agentRoomStore` y los métodos `createAgentRoom`, `sendAgentRoomMessage` y
`runAgentRoomPreset` también están disponibles por ControlApi para clientes
headless.

Ante `context_length_exceeded`, el agente compacta de emergencia y reintenta hasta
dos veces. Los fallos transitorios HTTP 408/425/429/5xx usan backoff exponencial
acotado. Si el proceso externo `llama-server` cae, `llama-agent` conserva el turno
y la sesión hasta cinco minutos mientras el watchdog reinicia y recarga el modelo;
errores deterministas de autenticación o schema no se reintentan. La tercera tool
idéntica ya no detiene inmediatamente el trabajo: se bloquea esa ejecución y la IA
recibe la evidencia con una orden de replantear; sólo se corta si ignora también ese
replanteo y vuelve a insistir con exactamente la misma llamada.
Si el backend se detiene explícitamente durante una respuesta, el turno se cierra
como interrumpido, libera inmediatamente el estado ocupado y queda listo para
reintentar; Tasks y workflows reciben `turnFinished` y no esperan para siempre.

El agente usa además **memoria de trabajo dinámica**: cada sesión persiste un
`transcript` completo e inmutable separado del `workingContext` que se manda al
modelo. Antes de una inferencia poda de forma determinista resultados duplicados y
argumentos voluminosos de errores antiguos, sin tocar escrituras, tests, memoria,
skills ni subagentes. Cuando la presión de contexto lo justifica, genera un
checkpoint JSON estructurado; el modelo también puede llamar
`context_checkpoint` al cerrar una fase. La compactación sólo se acepta si el
ahorro amortiza la invalidación del prompt-cache (o evita un overflow). El medidor
de Agente muestra contexto activo, tokens ahorrados y, en su tooltip, el tamaño del
transcript íntegro. Los snapshots v1 se migran al leerlos y las bifurcaciones
preservan ambas representaciones.

`LlamaCode.exe --agent-daemon` (alias `--headless`) inicia el núcleo y la
ControlApi local sin cargar QML ni crear ventanas. El puerto se controla con
`LLAMACODE_CONTROL_PORT` (8765 por defecto, sólo localhost). Mantiene la política
de instancia única: la GUI y el daemon no operan simultáneamente sobre los mismos
procesos y stores; clientes externos consumen la API. Si se ejecuta el acceso
directo mientras sólo existe una instancia headless, ésta hace un handoff limpio
a la GUI y conserva una única instancia visible. Si ya existe una GUI, el acceso
directo simplemente la restaura y la enfoca.

## Corridas durables y entregables

Cada turno de una sesión persistente del agente nativo recibe una identidad
durable y se registra en `agent_runs/`: aceptación idempotente, estados
`queued/running/waiting/completed/failed/cancelled/interrupted`, lease renovable,
journal JSONL con secuencia y recuperación conservadora. Un lease vencido pasa a
`uncertain`; no se reejecutan automáticamente efectos de red, archivos o
desktop cuyo resultado pueda ser ambiguo. El contrato completo, los límites y
las rutas de prueba están en [`docs/agent-runs.md`](docs/agent-runs.md).

Al cerrar el turno se comparan hashes del workspace y se conserva en
`agent_deliverables/<runId>/` sólo lo creado o modificado. El manifiesto incluye
hash anterior/posterior, estado, tamaño y una copia restaurable cuando entra en
los límites configurados. La pestaña Agente expone `📦 Inbox`: permite inspeccionar
el historial durable, abrir la carpeta de una corrida, revisar sus entregables y
usar Guardar como. `saveAs` y `restore` rechazan sobrescribir por defecto. Si un
lease vence, la corrida queda `uncertain` y el Inbox ofrece únicamente cerrarla
como `cancelled` o `failed`; nunca relanza sus efectos automáticamente.

La captura de snapshots y entregables se ejecuta fuera del hilo de la interfaz.
Los registros, journals, manifiestos e índices usan locks con recuperación de
locks stale para que dos instancias no mezclen eventos ni archivos. El detalle
renderer-safe no expone leases ni el snapshot inicial.

## Seguridad operativa

Los builds y tests paralelos se serializan por lane mediante `build_coord.ps1`.
El lock identifica al proceso `.bat` propietario por PID y hora de creación; si
ese proceso termina o el PID es reutilizado, la siguiente corrida roba el lock
inmediatamente. No se usan procesos `Start-Sleep` como señal de actividad y un
proceso ajeno no puede publicar ni liberar el resultado del propietario. El estado
se consulta con `build_coord.ps1 -Lane build|tests -Action status`.
El resultado compartido se publica mediante reemplazo atómico: un proceso en cola
no puede leer una línea vacía/parcial y confundir un build limpio con un fallo o un
`DIRTY`.

Ese lock serializa *quién compila*, no *qué fuente hay en disco*: si dos sesiones
comparten el working tree, la otra puede editar `src/` mientras compilás. Para
trabajar en varias mejoras a la vez, aislá cada una en su worktree —
`worktree.ps1 -Action new -Name <tarea>` crea `../LlamaCode-<tarea>` con rama
`session/<tarea>` y sus propios `build/`, `build_tests/` y `.buildlock/`. Si igual
compartís el tree, `build_coord.ps1` avisa cuando la fuente se mueve durante un
`acquire` y marca el resultado **DIRTY** al liberar (nadie lo adopta por REUSE, y
el `.bat` avisa que el binario o el gate no corresponden a la fuente).

- Nada destructivo sin aprobación explícita.
- Escrituras fuera de workspace: bloqueadas por defecto.
- Las rutas se validan por su destino canónico; un symlink/junction no puede
  escapar del workspace o de las carpetas adicionales autorizadas.
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
| **dzhng/deep-research** | Patrón breadth/depth, learnings compactos, consultas con objetivo y seguimiento recursivo | https://github.com/dzhng/deep-research |
| **LangChain Open Deep Research** | Separación supervisor/investigador, reflexión, compresión intermedia, límites de iteración y validación antes del informe | https://github.com/langchain-ai/open_deep_research |
| **Tongyi DeepResearch** | Ideas de investigación de horizonte largo, test-time scaling, resumen de contexto y búsqueda agentic iterativa | https://github.com/Alibaba-NLP/DeepResearch |
| **Omnix** | Ideas de API local multimodal, `reqId` de correlación, modo headless y separación de colas texto/operaciones auxiliares. Revisión: [`docs/omnix_review.md`](docs/omnix_review.md) | https://github.com/LoanLemon/Omnix |
| **Honey (I Shrunk the AI)** | _Inspiración conceptual_ (no se toma código): la directiva opt-in "Frugalidad (honey)" del agente — código YAGNI, respuesta-primero y handoffs agente↔agente densos clave:valor en vez de JSON | https://github.com/Green-PT/honey-for-devs |
| **Vix** | Ideas (sin copiar código) para prefijos estables entre fases, telemetría comparable, workflows reanudables y vistas compactas efímeras con fallback seguro | https://github.com/get-vix/vix |
| **TurboLLM** | Inspiración de diseño para catálogo de motores/forks, compatibilidad por hardware, probe enriquecido y build-from-source guiado para forks sin prebuilts. No se copia código por su licencia source-available. | https://github.com/mohitsoni48/TurboLLM |
| **OpenModel** | Ideas (no se copia código): ingesta de modelos ya descargados por Ollama vía scheme `ollama://` (reusa los blobs GGUF sin re-descargar) y un diagnóstico consolidado estilo `om doctor` (binarios/roots/catálogo/hardware/git/gateway + issues accionables) | https://github.com/wundercorp/openmodel |

> Al sumar código/datos de otro repo, agregar la fila correspondiente acá.
