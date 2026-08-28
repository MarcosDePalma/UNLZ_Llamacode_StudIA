<p align="center">
  <img src="https://github.com/JonatanBogadoUNLZ/PPS-Jonatan-Bogado/blob/9952aac097aca83a1aadfc26679fc7ec57369d82/LOGO%20AZUL%20HORIZONTAL%20-%20fondo%20transparente.png?raw=true" alt="National University of Lomas de Zamora — Faculty of Engineering" width="520">
</p>

<h1 align="center">UNLZ_Llamacode_StudIA</h1>

<p align="center">
  <strong>National University of Lomas de Zamora (UNLZ) — Faculty of Engineering</strong><br>
  Fork of <a href="https://github.com/cristianlukas/UNLZ_Llamacode">UNLZ_Llamacode</a> · Adds the <strong>StudIA</strong> module · Supervised Professional Practice 2026
</p>

<p align="center">
  🇦🇷 <a href="README.md">Español</a> ·
  🇬🇧 <strong>English</strong> (this document)
</p>

---

## 🎓 About this fork

This repository is a **fork of [cristianlukas/UNLZ_Llamacode](https://github.com/cristianlukas/UNLZ_Llamacode)**
that adds **StudIA**, a study assistant that answers questions **using only the indexed
academic corpus** —lecture notes, books and problem sets— citing the source document and
page, and running entirely on the student's machine.

StudIA was developed as a **Supervised Professional Practice (PPS)** project in
Mechatronics Engineering at FI-UNLZ. Development lives on the `feature/studia` branch:
55 files, ~14,700 lines, 7 commits. It adds `src/core/studia/`,
`qml/pages/StudiaPage.qml`, `tools/studia/`, `tests/test_studia.cpp` and `installer/`,
and does **not** modify the agent, chat, backends or model servers of the base project.

The module documentation is in the Spanish README:
[StudIA (asistente de estudio)](README.md#studia-asistente-de-estudio-sobre-corpus-académico).
PPS documentation (report, schedule, Gantt, manuals):
[2026_1C_PPS_Asistente_de_Estudio_DE-PALMA](https://github.com/MarcosDePalma/2026_1C_PPS_Asistente_de_Estudio_DE-PALMA).

> The rest of this README documents the **original project**.

---

> **Institutional project of the National University of Lomas de Zamora (UNLZ), Faculty of Engineering.**
>
> UNLZ_Llamacode is a local-AI workstation: a native desktop app (Qt/QML + C++)
> that, on your own hardware and with no dependency on the cloud, covers
> **integrated chat** with persistent history, a **coding agent harness** over
> local repositories, **server launch** for `llama.cpp` models (multi-binary /
> multi-GGUF roots / composable profiles), **cloud backends with encrypted
> secrets**, **Talk mode** (voice-to-voice STT/TTS), **memory/RAG and claim
> verification**, **master/supervisor escalation**, **mail accounts**, **browser
> automation (Playwright)**, **attachments/documents + vision**, **Tasks**
> (semantic macros + cron scheduler), and a **watchdog + live VRAM meter**.
>
> It is developed as a foundation for academic and research work, aimed at teaching,
> experimentation with local LLMs, and the work of the Faculty's fellows/thesis
> students.

## Table of contents

- [Ultra-fast install](#ultra-fast-install-isolated-test-bench)
- [What it is](#what-it-is) · [Privacy and local data](#privacy-and-local-data) · [Recommended hardware](#recommended-hardware) · [Current status](#current-status)
- [Goal](#goal) · [Differentiators](#differentiators)
- [Architecture](#architecture)
- [Multi-llama.cpp design](#multi-llamacpp-design) · [Multi-GGUF roots](#multi-gguf-roots-design) · [Composable profiles](#composable-multi-profile-design)
- [Model cookbook (hardware-fit)](#model-cookbook-hardware-fit-recommendations)
- [Integrated chat](#integrated-chat) · [Agent harness](#agent-harness-opencode) · [Server launch](#server-launch-launchpage)
- [Cloud backends + secrets](#cloud-backends--encrypted-secrets) · [Talk mode (voice)](#talk-mode-voice-to-voice) · [Memory/RAG](#memory-rag-and-verification) · [Master/supervisor](#master--supervisor-escalation)
- [Mail](#mail-accounts) · [Browser (Playwright)](#browser-automation-playwright) · [Attachments/vision](#attachments-documents--vision) · [Watchdog + VRAM](#server-robustness-watchdog--vram) · [Other capabilities](#other-capabilities)
- [Process lifecycle](#process-lifecycle) · [Tech stack](#tech-stack) · [Build](#build) · [Repo layout](#repo-layout)
- [Phases](#phases) · [Tasks (macros + scheduler)](#tasks-configurable-macros--cron-scheduler) · [Benchmarking](#benchmarking) · [Auto-tuning](#parameter-auto-tuning) · [Operational security](#operational-security)
- [Acknowledgements](#acknowledgements)

## Ultra-fast install (isolated test bench)

A single command: installs every dependency, clones the repo into an isolated
folder, builds and launches. No manual clone or environment prep required.

**Windows** (PowerShell):

```powershell
irm https://raw.githubusercontent.com/guideahon/UNLZ_Llamacode/main/scripts/bootstrap.ps1 | iex
```

**Linux** (bash):

```bash
curl -fsSL https://raw.githubusercontent.com/guideahon/UNLZ_Llamacode/main/scripts/bootstrap.sh | bash
```

It automatically installs:

- **git, CMake, Ninja, Python** and the C++ toolchain — MSVC v143 (Build Tools
  2022) on Windows / `g++` + `build-essential` on Linux.
- **Qt 6.8.3** via `aqtinstall` on both platforms (Windows `msvc2022_64`, Linux
  `gcc_64`), including required modules such as `qtmultimedia` and `qtsvg`.
  On Linux aqt is used — **not** distro Qt packages — because the code
  needs Qt ≥ 6.5 (`QQmlApplicationEngine::loadFromModule`) and several LTS
  distros ship an older Qt (Ubuntu 24.04 = 6.4.2). From the distro we only pull
  the toolchain and the system libs Qt links against (GL, xcb, glib,
  fontconfig…).

Clones into `%USERPROFILE%\LlamaCode` / `~/LlamaCode` and launches the app when
done (unless `LC_NORUN=1`). On Windows it also creates a per-user shortcut at
`%APPDATA%\Microsoft\Windows\Start Menu\Programs\LlamaCode.lnk`, so searching
"LlamaCode" from the Start menu can find it.

Optional variables (set before running):

| Var | Default | What it does |
|---|---|---|
| `LC_DIR` | `~/LlamaCode` | isolated install folder |
| `LC_BRANCH` | `main` | branch to clone |
| `LC_CONFIG` | `Release` | `Release` or `Debug` |
| `LC_QTVER` | `6.8.3` | Qt version (Linux only) |
| `LC_QTROOT` | `~/Qt` | Qt install root (Linux only) |
| `LC_NORUN` | (empty) | `1` = don't launch when done |

Example with overrides (Linux):

```bash
LC_DIR=/opt/llamacode LC_CONFIG=Debug LC_NORUN=1 \
  bash -c "$(curl -fsSL https://raw.githubusercontent.com/guideahon/UNLZ_Llamacode/main/scripts/bootstrap.sh)"
```

Minimum prerequisites: **Windows** needs `winget` (App Installer from the Microsoft
Store). **Linux** supports apt / dnf / pacman / zypper and asks for `sudo` for
system packages. Validated in a clean Ubuntu 24.04 container (toolchain + aqt Qt
6.8.3 + build).

---

## What it is

UNLZ_Llamacode is a native app (Qt/QML + C++) to orchestrate multiple `llama.cpp` backends, manage chat sessions, and run AI agent harnesses (opencode, aider) over local repos.

Core principle:
- The GUI does **not** embed `llama.cpp`.
- The GUI **orchestrates external binaries** (`llama-server.exe`, MTP forks, CUDA/Vulkan/CPU builds).
- The GUI **composes reusable profiles** over binaries, models and presets.
- The GUI **integrates agent harnesses** (opencode) via native HTTP API.

## Privacy and local data

UNLZ_Llamacode is designed as a local-first workstation: the GUI, profiles, model
catalog, chat/agent history and `llama-server` processes run on the user's machine.
The project also supports optional external integrations, so privacy depends on the
profile and on the features enabled for each session.

### What stays local

| Data / process | Location or scope | Leaves the machine by default |
|---|---|---|
| GGUF models | Folders registered in Model Roots | No |
| Profiles and presets | `AppLocalData/LlamaCode/profiles/` | No |
| Chat history | `AppLocalData/LlamaCode/chat/` | No |
| Scheduled Tasks | `AppLocalData/LlamaCode/tasks/` | No |
| Benchmark results | `AppLocalData/LlamaCode/benchmarks/` | No |
| Process state | `AppLocalData/LlamaCode/services.json` | No |
| Secrets | OS SecretStore or env-var references | Not stored in repo JSON |

### When external traffic happens

- **Model downloads**: if the download queue is used, the app contacts the
  configured model provider, such as Hugging Face.
- **Cloud backends**: if a `BackendProfile` points to OpenAI, OpenRouter, Groq,
  DeepSeek or another OpenAI-compatible endpoint, messages sent through that
  profile leave the machine for that service.
- **Search, Deep Research and web verification**: required queries and URLs are
  sent to search engines or external sites when those features are enabled.
- **Mail**: IMAP/POP3/SMTP connect to the configured provider; sending mail is an
  irreversible external action.
- **Cloud STT/TTS**: Talk mode can use local or remote endpoints. If a remote
  provider is configured, audio/text travels to that provider.
- **Browser automation**: Playwright may navigate external sites by user request or
  by a Task.

### Security note

The local API, spawned processes and configuration files live under the operating
system user account. Other processes running as that same user may interact with
local resources if they have enough permissions. For sensitive work, use local
profiles, review external-tool toggles and keep approvals enabled for shell, mail,
browser and destructive actions.

## Recommended hardware

LlamaCode does not bind the project to one fixed model: it indexes
`llama.cpp`-compatible GGUFs, estimates memory and recommends options based on
available RAM/VRAM. This table is a practical starting point; real performance
depends on the model, quant, context, batch, backend and machine thermals.

| Available hardware | Typical mode | Suggested models/quant | Rough context | Expectation |
|---|---|---|---|---|
| CPU + 16 GB RAM | `cpu_only` | 3B–7B `Q4_K_M` / `Q5_K_M` | 4k–8k | Functional for tests and light chat |
| CPU + 32 GB RAM | `cpu_only` | 7B–14B `Q4_K_M` | 8k–16k | Better quality, lower speed |
| 6–8 GB VRAM GPU + 16 GB RAM | `gpu` or `partial_offload` | 7B–9B `Q4_K_M`, compact coder models | 8k–16k | Good entry point for a local agent |
| 12 GB VRAM GPU + 32 GB RAM | `gpu` | 9B–14B `Q4_K_M` / `Q5_K_M` | 16k–32k | Recommended for daily use |
| 16 GB VRAM GPU + 32–64 GB RAM | `gpu` | Quantized 14B–32B, small MoE models | 16k–32k | More stable agent and RAG workflows |
| 24 GB+ VRAM GPU + 64 GB RAM | `gpu` | Quantized 32B+ or higher quants | 32k+ | Best margin for long context and multitasking |

`partial_offload` combines VRAM and RAM when the full model does not fit on the GPU,
at the cost of speed. On notebooks or low-memory machines, start with 8k context,
`Q4_K_M` and close heavy processes before running benchmarks or Deep Research.

## Current status

**P0–P4 complete and working.**

| Component | Status |
|---|---|
| `BinaryRegistry` + `CapabilityDetector` | ✅ |
| `ModelRootRegistry` + `GGUFScanner` | ✅ |
| `ModelCatalog` (SQLite) | ✅ |
| `ProfileManager` (6 entities) | ✅ |
| `EffectiveProfileBuilder` | ✅ |
| Profile importer from CLI args | ✅ |
| Start/Stop server (QProcess + Job Object, async stop) | ✅ |
| Integrated streaming chat (P4) | ✅ |
| Chat history with projects | ✅ |
| opencode harness via HTTP API (P3) | ✅ |
| Agent View (chat bubbles) + terminal view | ✅ |
| opencode session history with projects | ✅ |
| Process lifecycle (Job Object + PID file) | ✅ |
| Dedicated `LlamaProcessManager` | ⏳ P1 refactor |
| Automatic health-check endpoint | ✅ (polling /health post-start) |
| Port-collision pre-check on server start | ✅ |
| First-run setup popup (binary + model + automatic profile) | ✅ |
| Native agent (LlamaAgentBackend, ReAct + tools + MCP) | ✅ P5 |

## Goal

A serious launcher for `llama-server`, evolved into a command center for coding agents with integrated chat and persistent history.

## Differentiators

- **Multi-llama.cpp**: coexist with several builds/forks frictionlessly.
- **Multi-GGUF roots**: index several model folders/disks.
- **Composable multi-profiles**: mix `Backend + Model + Runtime + Harness + Workspace`.
- **Persistent chat**: conversation history grouped by project/profile.
- **Integrated agent**: opencode via HTTP API with no per-message subprocess, with sessions and projects.

## Architecture

```text
LlamaCode
├── UI Layer (Qt Quick / QML)
│   ├── Main.qml (ApplicationWindow + NavBar)
│   ├── pages/
│   │   ├── BinariesPage.qml
│   │   ├── ModelRootsPage.qml
│   │   ├── ProfilesPage.qml      ← import from CLI args
│   │   ├── LaunchPage.qml
│   │   ├── ChatPage.qml          ← streaming chat + history + projects
│   │   └── AgentPage.qml         ← Agent View + terminal view + sessions
│   └── components/
│       ├── LcButton, LcTextField, LcDialog
│       ├── NavBar, PageHeader
│       └── CommandPreview
├── AppController (singleton → QML "App")
│   ├── Chat session management   ← local JSON, grouped by launchProfile
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
    ├── services.json             ← PID state for orphan detection
    ├── chat/{index.json, *.json} ← persisted chat sessions
    ├── tasks/tasks.json          ← Tasks (macros) + their cron schedule
    └── benchmarks/               ← quality benchmark cache + run results
```

## Multi-llama.cpp design

### Binary Registry

`LlamaBinary` entity:
- `id`, `name`, `path`, `flavor` (`official`, `mtp-fork`, `custom`)
- `backend` (`cuda`, `vulkan`, `cpu`, `metal`)
- `versionHint` (free text)
- `supportedFlags`, `conflictingFlags`, `flagAliases`
- `envDefaults`, `workingDirectory`, `binaryHash` (SHA256 of first 1MB)
- `pathValid` (validated at runtime)

### Capabilities Matrix

Each binary tracks supported flags, aliases and conflicts. `EffectiveProfileBuilder.addFlag()` degrades with a `warning` or emits a `blockingError` depending on criticality.

## Multi-GGUF roots design

### Model Root Registry

`ModelRoot` entity: `id`, `path`, `label`, `scanMode` (manual/startup/watch), `enabled`, `priority`, `tags`, `isOnline`.

### Model catalog (SQLite)

`CatalogModel` entity: `id`, `rootId`, `absolutePath`, `fileName`, `sizeBytes`, `mtime`, `familyHint`, `quantHint`, `isVisionCandidate`, `isDraftCandidate`, `isAvailable`, `sha256`.

### GGUFScanner

- Async scan via `QtConcurrent::run`
- Infers family (deepseek, llama, mistral, phi, qwen, gemma...) by regex over the name
- Infers quant (`Q4_K_M`, `IQ3_XS`, `BF16`...) by regex
- `isDraftCandidate`: contains "draft"/"small" OR size < 2GB

## Model cookbook (hardware-fit recommendations)

`ModelRootsPage` recommends which models to download based on detected hardware (RAM / VRAM / GPU via `nvidia-smi`), using the `assets/hwfit/hf_models.json` catalog (~900 models, based on the Odysseus cookbook).

The download list is limited to **GGUF models compatible with llama.cpp**. MLX/AWQ/GPTQ/EXL2 catalog entries are filtered out so the app does not offer repositories that require another runtime or have no `.gguf` file downloadable by the app. Recent curated picks (for example `Qwen3.5-9B-GGUF`) are added when the base catalog lacks an explicit GGUF source.

### Scoring

Each model gets a `0–100` score combining, weighted for the *general* use case (quality 0.45 / speed 0.30 / fit 0.15 / context 0.10):

- **Quality** — preferably a **real benchmark** (Artificial Analysis *Intelligence Index*, remapped to 0–100); if there's no match, a heuristic by params + family + architecture bonus (qwen3.6 +9, qwen3.5 +8, qwen3-next +6, …) with a penalty by quant tier. Coder models are penalized in the general scan so they don't dominate.
- **Speed** — estimated t/s based on GPU bandwidth and active params (MoE-aware). In `partial_offload` speed is a harmonic GPU/CPU blend by the fraction resident in VRAM.
- **Fit** — required-memory vs. budget ratio. In `partial_offload`, the budget is
  VRAM + usable RAM, not VRAM alone.
- **Context** — modern target: 32k=100, 16k=85, 8k=70, 4k=50 (the 4k stub isn't rewarded).

Tie-break by version (Qwen3.6 > Qwen3.5).

### Memory estimation (`estimateCatalogMemoryGb`)

Footprint = **weights + KV cache + overhead**, sized for a realistic context, not 4k:

- **Weights** — **total** params (MoE keeps all experts in memory, only routes a subset per token) × bytes-per-param of the quant.
- **KV cache** — scales with the **full** model (every layer caches K/V regardless of MoE routing) and the **real sizing context**, not a 4k stub. Constant `1.5e-5 GB/token/B` ≈ fp16 KV of a GQA-era model (Llama3-8B @32k ≈ 4 GB).
- **Overhead** — llama.cpp compute graph + MTP/draft buffers (`0.7 GB + 5%` of weights).
- **Sizing context** (`sizingContext`) — target 32k, capped by the model's max ctx, floor 8k. Avoids underestimating KV cost with small defaults.

### Run modes (run mode / fit)

Computed against VRAM (`nvidia-smi`) and system RAM (90% usable as headroom):

| Mode | Condition | Notes |
|---|---|---|
| `gpu` | fits in VRAM | all on GPU |
| `partial_offload` | doesn't fit in VRAM, fits in VRAM+RAM | VRAM+RAM spill (llama.cpp partial `-ngl`); `gpuFraction = vram/required` |
| `cpu_only` | no GPU, fits in RAM | all in RAM |
| `no_fit` | doesn't fit in VRAM+RAM | — |

### Quality benchmark (Artificial Analysis)

- **Bundled table** `assets/benchmarks/aa_intelligence.json` — offline floor, no network dependency.
- **Weekly refresh**: if the cache (`AppLocalData/LlamaCode/benchmarks/`) is >7 days old, it fetches in the background and overwrites it; on any network/JSON failure, the bundled one stays.
- **Matching**: `benchmarkKey()` normalizes the catalog name (strips provider, quant/format, GGUF, `-4bit`, `instruct`/`it`/`base`…) to map against the table.

## Composable multi-profile design

| Entity | What it defines |
|---|---|
| `BackendProfile` | host / port / binary / base args |
| `ModelProfile` | main model + mmproj + draft |
| `RuntimePreset` | ctx / batch / threads / gpu-layers / flash-attn / cache |
| `HarnessProfile` | adapter / args / env of external harness |
| `WorkspaceProfile` | cwd / policies / shell permissions |
| `LaunchProfile` | composition of the 5 above + overrides |

### Profile importer from CLI

Paste a terminal command (e.g. `llama-server --model ... --ctx-size 8192 --n-gpu-layers 99`) and UNLZ_Llamacode extracts and configures all recognized parameters automatically.

## Integrated chat

- **Streaming chat** directly to `llama-server` via `/v1/chat/completions` SSE
- **Persisted sessions** in local JSON (`AppLocalData/LlamaCode/chat/`)
- **Grouped by project** (launch profile active when the session was created)
- **"⏳ Processing..." indicator** while waiting, `▌` cursor during generation
- **Stop generation** while saving what was received

## Agent harness (opencode)

- **Native HTTP integration**: talks to the opencode server via REST + SSE, with no `opencode run` subprocess (eliminates the SQLite DB conflict on Windows)
- **Agent View**: chat bubbles with real-time streaming
- **Terminal view**: raw log for debugging
- **opencode sessions**: history persisted in the opencode DB, grouped by directory/project
- **Automatic resume**: resumes the last session when the agent restarts
- **Auto-generated titles**: real-time update via `session.updated` SSE

## Cloud backends + encrypted secrets

Although the focus is 100% local, each profile can point to an **external
OpenAI-compatible endpoint** (OpenAI, OpenRouter, Groq, DeepSeek, etc.) instead of a
local `llama-server`. `BackendProfile.kind = "cloud"` spawns no process or binary:
chat/agent hit the `cloudBaseUrl` directly with the configured model.

- **SecretStore**: API keys are **never** serialized into the repo's JSON. The
  profile stores a **reference** (`cloudKeyRef`) and the value is resolved at runtime
  from an environment variable or an on-disk encrypted store — **QtKeychain** (Secret
  Service / WinCred / macOS Keychain) with a **DPAPI** fallback on Windows.
- Applies equally to HTTP masters, mail accounts and voice providers.

## Talk mode (voice-to-voice)

Speak to the AI and hear the answer, hands-free. A **🎙 Talk** section in the NavBar
(reuses the chat backend: sessions and history included).

- **STT and TTS** go through **OpenAI-compat** endpoints (`/v1/audio/transcriptions`,
  `/v1/audio/speech`). A single code path: **local** (whisper.cpp server,
  openedai-speech, piper-http on localhost, no key) or **cloud** (remote URL +
  keyRef). Configurable independently for STT and TTS.
- **Capture** PCM16 mono 16 kHz (`QAudioSource`) with **energy-RMS VAD** (configurable
  end-of-turn by silence), **microphone selection** and a live **level meter**. A
  *Test microphone* button validates input with no server.
- **Barge-in**: interrupt TTS when new speech is detected. State machine
  `listening → transcribing → thinking → speaking` with optional auto-listen.

## Memory, RAG and verification

The native agent doesn't just read files: it keeps memory and structured knowledge.

- **Layered MemoryStore**: durable facts extracted from conversations (background
  consolidation when leaving a session) + per-project memory in a file.
- **GraphStore**: entity/relation graph for structured knowledge.
- **Tools**: `hybrid_search` (lexical+semantic search), `verify_claims` (claim
  checking), layered memory. RAG over the project material.

## Master / supervisor (escalation)

When the local model gets stuck, the agent can **escalate** the sub-problem to a more
capable model or CLI. Configured per `LaunchProfile` (or a global fallback).

- **Ordered fallback chain**: type `profile` (another of LlamaCode's own profiles),
  `http` (OpenAI-compat endpoint with keyRef) or `cli` (`claude-code` / `codex`
  detected on the system).
- Escalation **manual** (button), **auto** (after N failures of the same tool
  signature) or **both**, with per-signature anti-recursion. `ask_teacher` tool for
  the agent.

## Mail accounts

A minimal SMTP (send) + IMAP/POP3 (receive) client over sockets, with `email_*` tools
for the agent. Per-provider presets (Gmail/Outlook/custom). The password goes to
SecretStore (`mail/<name>`), never to JSON. `email_send` requires approval unless
*auto-send* is enabled (sending mail is an irreversible external action).

## Browser automation (Playwright)

A global toggle + per-profile override (`browserAutomation` inherit/on/off) that
injects the **Playwright MCP** into the agent's tool set. **Teach mode**: the user
records actions with Playwright codegen and they're saved as **replayable skills**
that Tasks can re-run.

## Attachments (documents + vision)

`DocumentExtractor` converts **pdf/office → markdown** attachments via the
**markitdown** sidecar (with md5 caching) to inject them into the chat/agent context.
With a vision model (server launched with `--mmproj`) it also accepts **images**.

## Server robustness (watchdog + VRAM)

- **Watchdog**: auto-restart of `llama-server` on crash (with a retry cap);
  `serverState` = `stopped|running|restarting|failed`.
- **Live VRAM/stats meter**: async `nvidia-smi` poll while the server runs
  (`serverStats`) to see real usage.
- **Log diagnostics**: regex detection of OOM, port collision, model loaded, etc.,
  emitted as leveled events.

## Other capabilities

- **Router mode (hot-swap)**: a single `llama-server` with several models loaded via
  an `.ini` preset; chat/agent switch by the request's `model` field.
- **GPU power limit**: set the per-GPU power limit (W) via `nvidia-smi` (relaunched
  elevated on Windows), globally or per profile.
- **Deep Research**: multi-page research with persisted reports.
- **Integrations**: unified registry of **MCP Tool Servers** + **API services**
  (endpoint + key), with connection testing.
- **ControlApi / headless**: every feature is controllable via a local API (target
  traversal), with dialog-free variants for automation.
- **EvalSuite**: reproducible model evaluation (importable as a custom benchmark).
- **Mermaid**: diagram rendering in chat (mermaid-cli sidecar).
- **Multi-language**: UI in Spanish, English, Chinese, French, Italian and German.
- **Export/Import/Wipe** of user data by category.

## Server launch (`LaunchPage`)

- **Command preview** with a *Copy* button.
- **Start server + agent** — brings up `llama-server` and the agent harness.
- **Start server only** — just `llama-server`, no agent.
- **OpenAI endpoint** — with the server running it shows `http://<host>:<port>/v1` (read-only, selectable) + a *Copy* button, to point external agents (opencode, aider, etc.) at the local backend.

## Process Lifecycle

- **Windows Job Object**: all subprocesses (llama-server + harness) are assigned to the main process's Job Object. When UNLZ_Llamacode closes (normally or on crash), children die automatically.
- **Traceability env vars**: `LLAMACODE_MANAGED=1`, `LLAMACODE_ROLE=server|harness-*`, `LLAMACODE_APP_PID=<pid>` on all spawned processes.
- **PID state file** (`services.json`): on startup, detects orphans from previous sessions and kills them before bringing up new processes.
- **Async stop**: `stopServer()` doesn't block the UI. It sends `terminate()`, exposes a `serverStopping` property, shows "Stopping..." on the button and status. Force kill after 5s if the process doesn't finish.

## Tech stack

- **Qt 6.8.3** (`msvc2022_64`)
- **Qt modules**: Core, Quick, Sql, Concurrent, Network, Widgets, Multimedia, Svg
- **Secrets**: QtKeychain (Secret Service / WinCred / Keychain) with a DPAPI fallback
- **Compiler**: MSVC 2022 (VS BuildTools)
- **CMake 3.21+**, generator: Visual Studio 17 2022 (multi-config)
- **QML theme**: Catppuccin Mocha
- **Persistence**: JSON (registries/profiles/chat) + SQLite (catalog) + QSettings

## Build

### Fast (recommended)

`build.bat` kills hung processes, configures, builds, deploys the Qt runtime (`windeployqt`) and regenerates the shortcuts. Accepts a config:

```bat
build.bat            REM Debug + Release (default)
build.bat Debug      REM Debug only
build.bat Release    REM Release only
```

Outputs:

| Config | Binary | Shortcut | Icon |
|--------|--------|----------|------|
| Release | `build\Release\LlamaCode.exe` (optimized, `NDEBUG`) | `LlamaCode.lnk` | `assets\app_icon.ico` (normal llama) |
| Debug | `build\Debug\LlamaCode.exe` (symbols + asserts) | `LlamaCode-Debug.lnk` | `assets\debug_icon.ico` (**red** llama) |

The Debug red icon is embedded in the `.exe` (taskbar/explorer) via `app_icon.rc` + `#ifdef LC_DEBUG_ICON` (CMake defines `/dLC_DEBUG_ICON` only in Debug config), and also in the `.lnk`.

> After touching code, always rebuild — the QML is embedded in the binary via `qt_add_qml_module`.

### Manual

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\msvc2022_64"
cmake --build build --config Release --parallel
```

### First install

`install.bat` / `setup.bat` install Python deps + Qt 6.8.3 via `aqtinstall` before the first build.

## Repo layout

```text
LlamaCode/
├── CMakeLists.txt          ← CMake root
├── app_icon.rc             ← icon resource (Debug/Release conditional)
├── build.bat / install.bat / setup.bat
├── update-shortcut.ps1     ← generates the .lnk (parametrizable by config/icon)
├── LlamaCode.lnk / LlamaCode-Debug.lnk
├── src/                    ← C++ (AppController, agent backends, core)
├── qml/                    ← UI (Main.qml, pages/, components/)
├── assets/
│   ├── app_icon.ico / debug_icon.ico / app_icon.png
│   ├── hwfit/hf_models.json          ← model catalog (cookbook)
│   └── benchmarks/aa_intelligence.json ← quality scores (offline)
├── docs/                   ← documentation (agent.md, TODO.md, plan_harness.md, tuner.md, ...)
├── logs/                   ← runtime/install logs (gitignored)
├── tests/ + build_tests/   ← Qt Test suite
└── build/                  ← artifacts (Debug/ + Release/, gitignored)
```

## Phases

1. **P0** ✅ Base multi-binary/multi-model launcher + UI
2. **P1** ✅ (partial) Validation, execution, live logs
3. **P2** ✅ (partial) Profile UX, CLI importer
4. **P3** ✅ opencode harness via HTTP API + sessions + projects
5. **P4** ✅ Integrated streaming chat + persistent history + projects
6. **P5** ✅ Built-in native coding agent (`LlamaAgentBackend`): ReAct loop against `llama-server`, tools (read/write/edit/grep/glob/list_dir/run_shell/web_fetch/task), MCP stdio, approvals, plan mode, checkpoint/rollback, parallel subagents in git worktrees, per-pattern permissions, @-mentions, images (vision)
7. **P6** ✅ Tasks (configurable semantic macros) + in-app cron scheduler, with automatic agent lifecycle
8. **P7** ✅ Cloud backends + encrypted secrets, Talk mode (voice-to-voice), mail, browser (Playwright/teach), memory/RAG, master/supervisor, watchdog + VRAM, router hot-swap, headless ControlApi

## Tasks (configurable macros + cron scheduler)

A **Tasks** section (in the NavBar, above Benchmark): macros the user configures,
saves and runs. **They are not dumb macros** — instead of recording raw
coordinates TinyTask-style, they delegate to the AI agent: each Task stores a
**natural-language goal** + **reference steps**, and at run time the agent
re-derives the actions with its tools (browser MCP, shell, mail, etc.) and
**adapts** if a button, element or file moved or got renamed.

### Data model (`TaskStore`)

- `id`, `name`, `description` (the goal), `profileId` (optional agent profile).
- `steps[]`: each step `{kind, intent, ref}` with `kind` ∈
  `instruction|browser|shell|mail|desktop`. `browser` steps record a replayable
  skill via Playwright codegen (reuses the browser *teach* mode).
- `scheduleEnabled` / `scheduleCron`, `lastRunAt` / `lastRunStatus`.
- JSON persistence at `AppLocalData/LlamaCode/tasks/tasks.json`.
- `composePrompt()` builds the goal prompt with an explicit instruction that the
  steps are a **guide, not a literal script** (adaptive replay).

### Execution (manual or scheduled)

`runTask()` unifies the ▶ button and the scheduler with automatic agent lifecycle:

- If the **agent is already running**, it uses it as-is (does not stop it).
- If **no agent is up**, it auto-starts server + agent (the Task's profile or the
  active one), runs once ready and **shuts it down** when the turn ends.
- No assignable profile → marks `lastRun = "error"`.

The lifecycle close-out relies on the `IAgentBackend::turnFinished` signal (emitted
when the turn completes), which marks `lastRun = "ok"` and stops the auto-started
agent.

### Cron scheduler (`CronSchedule` + `TaskScheduler`)

- Pure 5-field cron parser `min hour dayOfMonth month dayOfWeek`: `*`, lists
  `a,b`, ranges `a-b`, steps `*/n` and `a-b/n`, day-of-week `0`/`7` = Sunday, OR
  semantics of dayOfMonth/dayOfWeek when both are restricted.
- `TaskScheduler` evaluates per minute (in-app timer, per-minute de-dup) and fires
  `runTask` for every due Task. Global toggle persisted; runs while the app is open.
- Examples: `0 9 * * *` (daily at 9:00) · `*/15 9-17 * * 1-5` (every 15 min, 9–17h,
  Mon–Fri) · `0 0 1 * *` (1st of each month).

## Benchmarking

A module to compare quants and profiles systematically: measures RAM, VRAM, speed and relative quality with results persisted in a table.

### Workflow

1. Select one or more `LaunchProfile`s to compare.
2. Pick a test mode: **Short** (~30 s) or **Full** (1–5 min).
3. Run: UNLZ_Llamacode launches each profile in sequence, runs the prompts, records metrics.
4. View results in a comparison table; export or save for future comparisons.

### Test modes

| Mode | Prompts | `n_predict` | Score | Estimated time |
|------|---------|-------------|-------|----------------|
| **Short** | 5 fixed | 256 | 0–2 per prompt (max 10) | ~30 s |
| **Full** | 15 configurable | 512 | 0–5 per prompt (max 75) | 1–5 min |

Fixed parameters across every run: `temp 0`, `top_p 1`, `top_k 0`, fixed seed, `ctx` per profile.

### Prompt categories (Full mode)

```text
3 × logical reasoning
3 × code / debug
3 × technical / expert writing
3 × structured data extraction
3 × long context (1,000–4,000 input tokens)
```

Prompts are editable and persisted; the user can replace them with real cases (llama.cpp logs, expert reports, SQL, Airflow, court files, etc.).

### Scoring

```text
Short mode:  0 = fail  /  1 = acceptable  /  2 = good
Full mode:
  5 = equal or better than baseline
  4 = slight loss, usable
  3 = correct but less precise
  2 = significant error
  1 = severe failure
  0 = didn't follow the instruction
```

Relative quality normalized against the baseline profile (the highest scoring one):

```
relative_quality = profile_score / baseline_score × 100
```

### Metrics recorded per run

```text
profile / model / quant
RAM used (MB)
VRAM used (MB)
tokens/s — prompt eval
tokens/s — generation
total time (s)
short score / full score
severe errors (count)
```

### Persistence and view

- Results in JSON (`AppLocalData/LlamaCode/benchmarks/{timestamp}.json`).
- Table view in `BenchmarkPage.qml`: sortable columns, filter by profile/quant/date.
- Export to CSV from the UI.

### Example table

| Quant | Score | Δ baseline | t/s gen | RAM | VRAM |
|-------|-------|------------|---------|-----|------|
| Q8_0 | 92/100 | base | 20 | 2 GB | 28 GB |
| Q6_K | 90/100 | −2.2% | 25 | 2 GB | 22 GB |
| Q5_K_M | 86/100 | −6.5% | 30 | 2 GB | 18 GB |
| Q4_K_M | 80/100 | −13.0% | 38 | 2 GB | 14 GB |
| IQ4_XS | 77/100 | −16.3% | 42 | 2 GB | 12 GB |
| Q3_K_M | 65/100 | −29.3% | 55 | 2 GB | 9 GB |

## Parameter auto-tuning

Automatic search for the `llama-server` flags (`ngl`, `batch`, `ubatch`, `flash-attn`, `cache-type-k/v`) that maximize **tok/s** without degrading **quality**. A TPE-lite optimizer (discrete Parzen) with a **quality gate**: unlike *llama-launcher v1.3*, tuning the KV cache quant by speed alone doesn't collapse to the lowest quant, because the loss penalizes dropping below the threshold heavily.

- Runs `N` trials on a scratch port (launches/measures/kills the server per candidate, in a separate `QThread` so the UI doesn't freeze).
- Measures `timings.predicted_per_second` throughput (`/completion`) and grades the output with EvalSuite-style substrings.
- When done it **clones** the profile into a new `-tuned` one with the best config in `extraArgs`; the original stays intact.
- UI: `ProfilesPage` → **Auto-tune** / **Cancel tune** + live status.

Full detail in [`docs/tuner.md`](docs/tuner.md).

## Operational security

- Nothing destructive without explicit approval.
- Writes outside the workspace: blocked by default.
- Shell commands with allowlist/denylist per `WorkspaceProfile`.
- Subprocesses tagged with env vars for auditing and lifecycle control.

## Acknowledgements

Code, data and design taken from other projects:

| Project | Use in UNLZ_Llamacode | Repo / Source |
|---|---|---|
| **llama.cpp** | Orchestrated binaries (`llama-server`), OpenAI-compatible API, GGUF format | https://github.com/ggml-org/llama.cpp |
| **opencode** | External agent harness (HTTP API + SSE); MCP `mcp{}` config format | https://github.com/sst/opencode |
| **aider** | Supported external agent harness | https://github.com/Aider-AI/aider |
| **markitdown** | Document extraction sidecar (pdf/office → markdown) in `DocumentExtractor` | https://github.com/microsoft/markitdown |
| **Odysseus cookbook** | Base of the hardware-fit catalog `assets/hwfit/hf_models.json` (~900 models) | https://github.com/TheBlokeAI/odysseus-cookbook |
| **Artificial Analysis** | Bundled quality scores `assets/benchmarks/aa_intelligence.json` (Intelligence Index) | https://artificialanalysis.ai |
| **Playwright (MCP)** | Browser automation + codegen (teach mode) | https://github.com/microsoft/playwright-mcp |
| **OpenAI audio API** | `/v1/audio/transcriptions` and `/v1/audio/speech` contract for Talk mode (whisper.cpp, openedai-speech, piper) | https://platform.openai.com/docs/api-reference/audio |
| **QtKeychain** | OS-backed secret encryption | https://github.com/frankosterfeld/qtkeychain |
| **Catppuccin (Mocha)** | QML theme palette | https://github.com/catppuccin/catppuccin |
| **archex** | Code-context pipeline ideas in `hybrid_search`: token-budget packing + dep-graph expansion (neighbors via imports/includes). Review: [`docs/archex_context_review.md`](docs/archex_context_review.md) | https://github.com/Mathews-Tom/archex |

> When adding code/data from another repo, add the corresponding row here.
