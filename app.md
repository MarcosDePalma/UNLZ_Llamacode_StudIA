# LlamaCode App Notes For Future AI Agents

This document is operational context for AI agents modifying this repository for Cristian. It explains where runtime data lives, how launch profiles are composed, and what files/classes usually need edits.

## Project Basics

- Repository: `C:\Users\cristian\Documents\LlamaCode`
- Main app type: C++ / Qt / QML desktop app.
- Main controller: `src/AppController.cpp`, `src/AppController.h`
- UI pages: `qml/pages/*.qml`
- Build output: `build\Debug\LlamaCode.exe`
- Build command:

```powershell
cmake --build build --config Debug --parallel
```

If linking fails with `LNK1168 cannot open ... LlamaCode.exe for writing`, the app is running. Stop it, then rebuild:

```powershell
Get-Process LlamaCode -ErrorAction SilentlyContinue | Stop-Process -Force
cmake --build build --config Debug --parallel
```

## Data Locations

Qt uses different writable locations for different registries in this app. On Cristian's machine, the important paths are:

- **Profiles (launches/backends/models/runtimes/harnesses/workspaces):**
  `C:\Users\cristian\Documents\LlamaCode\profiles` (project root, since 2026-06-08).
  Set in `ProfileManager::storagePath()`; override with env `LLAMACODE_PROFILES_DIR`.
  Rolling backups + atomic writes under `profiles\.backups\`. The app rewrites
  these files from memory on profile changes / on exit, so edit JSON ONLY while
  the app is closed (no save-on-exit hook in `main.cpp` — reverted on user request).
- Binary registry, model roots, model catalog (still in Roaming):
  `C:\Users\cristian\AppData\Roaming\LlamaCode\LlamaCode`
- Benchmark results, benchmark runs, logs, chat/session data:
  `C:\Users\cristian\AppData\Local\LlamaCode\LlamaCode`

Important files/directories:

- `C:\Users\cristian\AppData\Roaming\LlamaCode\LlamaCode\binary_registry.json`
- `C:\Users\cristian\AppData\Roaming\LlamaCode\LlamaCode\model_roots.json`
- `C:\Users\cristian\Documents\LlamaCode\profiles\backends.json`
- `C:\Users\cristian\Documents\LlamaCode\profiles\models.json`
- `C:\Users\cristian\Documents\LlamaCode\profiles\runtimes.json`
- `C:\Users\cristian\Documents\LlamaCode\profiles\harnesses.json`
- `C:\Users\cristian\Documents\LlamaCode\profiles\workspaces.json`
- `C:\Users\cristian\Documents\LlamaCode\profiles\launches.json`
- `C:\Users\cristian\AppData\Roaming\LlamaCode\LlamaCode\model_catalog.db` (NOTA: el catálogo real vive en **Roaming**, no Local. Tabla SQLite `catalog_models`.)
- `C:\Users\cristian\AppData\Local\LlamaCode\LlamaCode\benchmarks\index.json`
- `C:\Users\cristian\AppData\Local\LlamaCode\LlamaCode\benchmarks\custom`
- `C:\Users\cristian\AppData\Local\LlamaCode\LlamaCode\benchmark-runs`
- `C:\Users\cristian\AppData\Local\LlamaCode\LlamaCode\logs\server.log`
- `C:\Users\cristian\AppData\Local\LlamaCode\LlamaCode\logs\agent.log`

Always make a timestamped backup before manually changing JSON registries.

## Profile Model

Profiles are split into six independent lists and connected by IDs. A usable launch profile references one item from each relevant list.

Code:

- `src/core/profiles/ProfileTypes.h`
- `src/core/profiles/ProfileTypes.cpp`
- `src/core/profiles/ProfileManager.h`
- `src/core/profiles/ProfileManager.cpp`
- Effective command builder: `src/core/profiles/EffectiveProfileBuilder.*`

### BackendProfile

Stored in `profiles\backends.json`.

Fields:

- `id`
- `name`
- `binaryId`: references `binary_registry.json`
- `host`: usually `127.0.0.1`
- `port`: e.g. `8021`
- `baseArgs`: args applied before runtime/model/launch args
- `envOverrides`

### ModelProfile

Stored in `profiles\models.json`.

Fields:

- `id`
- `name`
- `modelId`: references `model_catalog.db`
- `mmprojId`: references `model_catalog.db`, can be empty
- `draftModelId`: references `model_catalog.db`, often empty because BeeLlama DFlash profiles may use explicit `--spec-draft-model` or `--spec-draft-hf`

### RuntimePreset

Stored in `profiles\runtimes.json`.

Fields:

- `id`
- `name`
- `ctx`
- `batch`
- `ubatch`
- `threads`
- `gpuLayers`
- `flashAttention`
- `mmap`
- `mlock`
- `contBatching`
- `cacheType`
- `parallelSlots`

These map to common llama.cpp flags such as `--ctx-size`, `--batch-size`, `--ubatch-size`, `--threads`, `--n-gpu-layers`, `--flash-attn`, `--mmap` / `--no-mmap`, `--mlock`, `--cont-batching`, and `--cache-type-k`.

### HarnessProfile

Stored in `profiles\harnesses.json`.

Fields:

- `id`
- `name`
- `adapter`: `none`, `opencode`, `aider`, `llamaagent`, etc.
- `args`
- `env`

Benchmark agent mode uses the internal `LlamaAgentBackend`, not regular persistent project sessions.

### WorkspaceProfile

Stored in `profiles\workspaces.json`.

Fields:

- `id`
- `name`
- `cwd`
- `allowedPaths`
- `blockedPaths`
- `allowShellCommands`

Most benchmark-created workspaces should not become normal Agent projects.

### LaunchProfile

Stored in `profiles\launches.json`.

Fields:

- `id`
- `name`
- `backendProfileId`
- `modelProfileId`
- `runtimePresetId`
- `harnessProfileId`
- `workspaceProfileId`
- `extraArgs`
- `envOverrides`

`extraArgs` is where most specialized llama.cpp/BeeLlama tuning lives. Examples:

### Diagnóstico de fallback GPU

El triage del log del servidor detecta degradaciones silenciosas de llama.cpp:
`SPLIT_MODE_TENSOR` con sampler CPU, capas asignadas a CPU y operadores sin
soporte GPU. No cambia automáticamente `--split-mode`; informa el problema para
comparar `tensor` contra `layer` manteniendo constantes contexto, batch y ubatch.
El auto-tuner incluye esa comparación cuando detecta más de una GPU NVIDIA y el
binario CUDA declara soporte para `--split-mode`; conserva PP y TG como métricas
separadas y persiste la opción ganadora en el perfil optimizado.

```json
[
  "--alias", "precision-100k-q5k-q4v-cross1024",
  "--cache-type-v", "q4_1",
  "--spec-draft-model", "D:\\Models\\llamacpp\\Anbeeld-Qwen3.6-27B-DFlash-GGUF\\Qwen3.6-27B-DFlash-Q4_K_M.gguf",
  "--spec-type", "dflash",
  "--spec-dflash-cross-ctx", "1024",
  "--spec-draft-ngl", "all",
  "--spec-draft-n-max", "8",
  "--kv-unified",
  "--no-host",
  "--no-mmproj-offload",
  "--metrics",
  "--cache-ram", "8192",
  "--cache-reuse", "512",
  "--predict", "4096",
  "--temp", "0.6",
  "--top-k", "20",
  "--top-p", "1.0",
  "--min-p", "0.0",
  "--reasoning", "on",
  "--chat-template-kwargs", "{\"preserve_thinking\":true}",
  "--no-warmup"
]
```

Prefer local `--spec-draft-model` for reproducible benchmarks. `--spec-draft-hf` can introduce network/cache variability.

## Adding A Binary

UI path: Binaries page.

Code:

- Registry: `src/core/BinaryRegistry.*`
- Capability detection: `src/core/CapabilityDetector.*`
- Installer logic: `AppController::installOfficialBinary()`

Manual JSON path:

`C:\Users\cristian\AppData\Roaming\LlamaCode\LlamaCode\binary_registry.json`

Required core fields:

```json
{
  "id": "uuid",
  "name": "BeeLlama v0.3.1 CUDA 13.1",
  "path": "C:\\Users\\cristian\\AppData\\Roaming\\LlamaCode\\LlamaCode\\tools\\beellama-v0.3.1-win-cuda-13.1-x64\\llama-server.exe",
  "flavor": "mtp-fork",
  "backend": "cuda",
  "versionHint": "v0.3.1-cuda13.1",
  "workingDirectory": "C:\\Users\\cristian\\AppData\\Roaming\\LlamaCode\\LlamaCode\\tools\\beellama-v0.3.1-win-cuda-13.1-x64",
  "supportedFlags": [],
  "flagAliases": {},
  "binaryHash": ""
}
```

After adding a binary through code/UI, call capability detection so supported flags and aliases are populated. If manually editing JSON, either restart the app and use the UI capability detection, or mirror an existing BeeLlama entry.

Known BeeLlama binary path on this machine:

`C:\Users\cristian\AppData\Roaming\LlamaCode\LlamaCode\tools\beellama-v0.3.1-win-cuda-13.1-x64\llama-server.exe`

## Adding Models

UI path: Model Roots page.

Code:

- Roots: `src/core/ModelRootRegistry.*`
- Catalog: `src/core/ModelCatalog.*`
- Scanner: `src/core/GGUFScanner.*`

Model roots are stored in:

`C:\Users\cristian\AppData\Roaming\LlamaCode\LlamaCode\model_roots.json`

Cristian's main model root:

`D:\Models\llamacpp`

Example root:

```json
{
  "enabled": true,
  "id": "uuid",
  "label": "llamacpp",
  "path": "D:\\Models\\llamacpp",
  "priority": 0,
  "scanMode": "manual",
  "tags": ["imported", "opencode"]
}
```

The scanner recursively finds `*.gguf` and inserts rows into SQLite (tabla `catalog_models`):

`C:\Users\cristian\AppData\Roaming\LlamaCode\LlamaCode\model_catalog.db`

Do not manually invent model IDs unless necessary. Prefer adding/downloading `.gguf` under an existing model root and triggering a scan. If manually wiring profiles and you need an existing model ID, inspect `model_catalog.db` or existing `profiles\models.json`.

Useful local model paths:

- Main Qwen target:
  `D:\Models\llamacpp\Qwen3.6-27B-MTP-IQ4_XS-GGUF\Qwen3.6-27B-MTP-IQ4_XS.gguf`
- Qwen mmproj:
  `D:\Models\llamacpp\Qwen3.6-27B-MTP-GGUF-froggeric\mmproj-Qwen3.6-27B-f16.gguf`
- DFlash Q4 drafter:
  `D:\Models\llamacpp\Anbeeld-Qwen3.6-27B-DFlash-GGUF\Qwen3.6-27B-DFlash-Q4_K_M.gguf`
- DFlash IQ4 drafter:
  `D:\Models\llamacpp\Anbeeld-Qwen3.6-27B-DFlash-GGUF\Qwen3.6-27B-DFlash-IQ4_XS.gguf`
- DFlash Q5 drafter:
  `D:\Models\llamacpp\Anbeeld-Qwen3.6-27B-DFlash-GGUF\Qwen3.6-27B-DFlash-Q5_K_M.gguf`

## Creating Profiles Safely

The safest approach is to clone an existing working launch and reuse its backend/model/runtime/harness IDs, changing only `name`, `id`, `extraArgs`, and optional `envOverrides`.

### MANDATORY anti-data-loss procedure (read before touching ANY profile JSON)

Profiles live in six linked files under
`C:\Users\cristian\AppData\Roaming\LlamaCode\LlamaCode\profiles\` :
`backends.json`, `models.json`, `runtimes.json`, `harnesses.json`, `workspaces.json`, `launches.json`.
The running app holds them in memory and **rewrites all of them from memory on exit / on any profile change**. This is how profiles get lost: editing a file while the app is alive, or replacing instead of appending.

Hard rules (never skip):

1. **App MUST be closed** before editing any profile JSON. Verify: `Get-Process LlamaCode`. If alive, `Stop-Process -Force`, wait 1s. If you edit while it runs, the app overwrites your change on exit.
2. **NEVER replace the array. Always load → append/union-by-id → write.** Load the existing file, add your entries, write the union back. Do not regenerate the file from a subset.
3. **Never delete entries you did not create**, unless the user explicitly named them. Deletion is by id, one at a time, after confirming.
4. **Count must not drop.** Before write, assert `new.Count >= old.Count` (minus any you were explicitly asked to remove). A drop = bug, abort.
5. **Timestamped backup before every write**: `Copy-Item launches.json launches.json.bak.<ts>`.
6. The app itself now (since 2026-06-07) keeps **rolling backups** in `profiles\.backups\<entity>.<timestamp>.json` (last 15 per entity) and writes atomically (`.tmp` + rename). Recovery source if anything is lost: newest file in `profiles\.backups\`, or the dated `beellama031_*backup*` dirs, or `*.bak.<ts>` siblings. See `ProfileManager::save()`.
7. **Restoring lost profiles**: their `backendProfileId`/`modelProfileId` may point at an OLD id generation. Restore by **union-merging the whole graph** (backends+models+runtimes+harnesses+launches) from the same backup dir — restoring launches alone leaves broken refs.

Steps for a normal add:

1. Confirm app is closed.
2. Backup `profiles\launches.json` (timestamped).
3. Load existing `launches.json` as an array.
4. Read an existing profile to clone; generate a NEW UUID for `id`.
5. Change `name`, edit `extraArgs`.
6. **Append** to the loaded array (assert count grew); write the full array back.
7. Restart LlamaCode.

If a profile requires different context/batch/cache values that are represented by `RuntimePreset`, either clone the corresponding runtime in `runtimes.json` and reference it, or place explicit override flags in `extraArgs` if the effective builder appends launch args last.

When creating benchmark comparison profiles, use a clear prefix such as:

`BENCH vs MAX Q 01 / ...`

This makes filtering and later cleanup easy.

## Benchmark System

UI:

- `qml/pages/BenchmarkPage.qml`

Controller:

- Standard/chat requests: `AppController::benchmarkRequest`
- Agent benchmark: `AppController::runAgentBenchmark`
- Benchmark storage:
  - `AppController::benchmarkStorageDir`
  - `AppController::saveBenchmarkResult`
  - `AppController::loadBenchmarkResults`

Results are stored in:

- Summary index:
  `C:\Users\cristian\AppData\Local\LlamaCode\LlamaCode\benchmarks\index.json`
- Per-result JSON:
  `C:\Users\cristian\AppData\Local\LlamaCode\LlamaCode\benchmarks\<id>.json`
- Isolated generated workspaces:
  `C:\Users\cristian\AppData\Local\LlamaCode\LlamaCode\benchmark-runs`

Custom benchmark definitions:

`C:\Users\cristian\AppData\Local\LlamaCode\LlamaCode\benchmarks\custom`

Important result fields:

- `profileName`
- `target`: `chat` or `agent`
- `benchmarkName`
- `qualityScore`
- `qualityTotal`
- `firstAttemptScore`
- `firstAttemptTotal`
- `finalScore`
- `finalTotal`
- `repairAttempts`
- `timeToFirstAttempt`
- `totalTime`
- `passedAfterRepair`
- `avgTps`
- `avgTtftMs`
- `ramMb`
- `vramMb`
- `elapsedSec`
- `failed`
- `failureStage`
- `failureMessage`
- `failureDetail`
- `acceptance`
- `workspace`

Agent benchmark repair behavior is in `runAgentBenchmark`. It should:

1. Run prompts in an isolated workspace.
2. Run acceptance checks.
3. If score is partial, send a repair prompt with failed checks.
4. Retry acceptance up to the configured repair limit.
5. Persist first/final score and repair count.

Benchmark agent sessions must be ephemeral so they do not pollute the normal Agent project sidebar. `LlamaAgentBackend` has `setEphemeralSessions(true)` for this.

## Resource Measurement

Code:

- `AppController::benchmarkMeasureResourcesNow()`

VRAM uses `nvidia-smi`:

- First tries `--query-compute-apps=pid,used_memory`
- Falls back to `--query-gpu=memory.used`

RAM on Windows should use process data from `Get-CimInstance Win32_Process`, not just `tasklist`. The current implementation sums the server process tree and uses the larger of working set and pagefile/private usage.

Old benchmark rows are not recalculated after changing measurement logic.

## Agent Mode Notes

Main internal backend:

- `src/core/agent/LlamaAgentBackend.*`

Benchmark agent setup lives in `AppController::runAgentBenchmark`.

Important benchmark settings:

- Use `agent->setEphemeralSessions(true)` to avoid normal project/session persistence.
- Use `agent->setApprovalPolicy("super")` so the benchmark can write and test files without manual prompts.
- Use `agent->setDisabledTools({})` so file and shell tools are available.
- Agent benchmark currently uses a no-hard-timeout idle watchdog. It should fail only when server/agent activity is idle for the configured period.

## Logs

Useful logs:

- Server lifecycle and llama-server command lines:
  `C:\Users\cristian\AppData\Local\LlamaCode\LlamaCode\logs\server.log`
- Agent events:
  `C:\Users\cristian\AppData\Local\LlamaCode\LlamaCode\logs\agent.log`

Search examples:

```powershell
rg -n "Starting:|BeeLlama|MAX Q|turn completed|idle" "$env:LOCALAPPDATA\LlamaCode\LlamaCode\logs"
```

## Current BeeLlama/Qwen Benchmark Context

Cristian compares profiles against:

`MAX Q BeeLlama 0.3.1 / Precision 100k q5k-q4v cross1024`

Common temp:

`--temp 0.6`

Common reasoning:

- Quality/precision/balanced profiles often use `--reasoning on` and `--chat-template-kwargs {"preserve_thinking":true}`.
- Speed profiles often use `--reasoning off`.

When benchmarking quality, do not optimize only for TPS. Cristian cares about:

- final score
- first attempt score
- repair attempts
- total time
- stability across repeated passes
- VRAM/RAM
- whether failures are repaired correctly

## Editing Rules For Future Agents

- Use `rg` for searches.
- Use `apply_patch` for source edits.
- Do not wipe or regenerate profile JSON from scratch.
- Make timestamped backups before manual JSON edits under `%APPDATA%`.
- Avoid creating benchmark agent workspaces as normal Agent projects.
- After C++/QML changes, compile with `cmake --build build --config Debug --parallel`.
- If the app executable is locked, close only the running `build\Debug\LlamaCode.exe` process and rebuild.
- Tell Cristian whether old benchmark rows need rerunning. Most metric and acceptance changes only apply to new runs.
