# Plan — Harness modular y personalizable

> **IMPLEMENTADO.** E0–E8 están en el código; la referencia viva es la sección
> "Harness modular" de `docs/harness.md`. Este documento queda como registro del
> diseño y de las decisiones (incluidas las que se desviaron: los presets de
> sistema quedaron como specs construidos en código, no como JSON bundleado).

Objetivo: que un "nivel de agente" deje de ser un preset cerrado y pase a ser una
**composición declarativa de módulos** que el usuario arma, guarda, exporta y
compara. Hoy `Chat liviano / Básico / Intermedio / Avanzado / Máximo` son cinco
puntos fijos en una recta; queremos el espacio completo.

Referencia del estado actual: `docs/harness.md`.

---

## 1. Punto de partida (qué YA es personalizable)

`AgentProfile` (`src/core/profiles/ProfileTypes.h:148`) ya se puede crear,
duplicar, editar y borrar (`ProfileManager::addAgentProfile` /
`duplicateAgentProfile` / `updateAgentProfile` / `removeAgentProfile`), y expone:

- `enabledTools` (nombres de `toolCatalog()`, o `"*"`)
- `directives` (keys de `directiveCatalog()`, o `"*"`)
- `approvalMode`, `thinking`, `thinkingLeakGuard`, `temperature`, `systemExtra`
- `mcpEnabled`
- `personalityProfileIds` / `styleProfileIds` + límites de inyección
- `progressCredits` / `progressMaxCredits` / `progressReplanAfter` /
  `progressStopAfter` / `quickToolTimeoutSec`

`AppController::applyAgentProfileCaps()` los baja al backend. **Eso funciona: no
se toca.** El plan lo absorbe como la primera capa del spec nuevo.

## 2. Qué NO es personalizable hoy (el gap real)

| Área | Hoy | Dónde |
|---|---|---|
| Loop | `kMaxSameCall=3`, `kMaxTurnIters=1000`, `kMaxTransportRetries=60`, `kFailureSpiralThreshold=3` | constantes en `LlamaAgentBackend.h` |
| Stream | idle timeout sólo por env `LLAMACODE_STREAM_IDLE_TIMEOUT` | `streamIdleTimeoutMs()` |
| Watchdog | tabla fija por tool | `toolWatchdogSeconds()` |
| Contexto | umbral de compactación, poda, `trimStaleImages(keepLast=1)`, read-dedup, preflight, warmup | constantes / siempre-ON |
| Directivas | 7 keys fijas compiladas | `directiveCatalog()` |
| Playbooks | desktop playbook se inyecta por heurística | `buildSystemPrompt()` |
| Permisos | reglas por patrón y scope FS no viven en el perfil | `setPermissionRules`, `setTaskScope` |
| Guardrails | `hitlDestructive` / `mailAutoSend` globales | setters sueltos |
| Multi-agente | master chain vive en `LaunchProfile`, no en el perfil de agente; caps de subagentes fijos | `MasterConfig`, `adaptiveSubagentLimit` |
| Protocolo | native / text-tools se decide por 400 o flag global | `m_forceTextTools` |
| Fases | mismo harness para plan / ejecución / verificación | — |
| Tools propias | sólo vía MCP externo | — |

Conclusión: el perfil controla **qué tools y qué texto**; no controla **cómo se
comporta el loop**. Eso es lo que hay que abrir.

---

## 3. Modelo objetivo

Un perfil de agente pasa a ser un **`HarnessSpec`**: manifiesto declarativo,
resuelto en capas, con defaults idénticos a los actuales.

```
HarnessSpec
├── extends: <id de otro spec>        herencia explícita (preset o propio)
├── tools     ToolsModule       packs + tools sueltas + MCP + presupuesto
├── prompt    PromptModule      directivas built-in + packs de usuario + orden
├── loop      LoopModule        créditos, anti-loop, reintentos, watchdogs
├── context   ContextModule     compactación, poda, imágenes, dedup, preflight
├── permissions PermModule      approvalMode + reglas por patrón + scope + guardrails
├── escalation EscalationModule master chain, subagentes, DifficultyRouter
├── protocol  ProtocolModule    native|compat|text, thinking, temperatura
└── phases    PhasesModule      overrides por fase (plan/exec/verify)
```

Resolución en capas (la de más abajo pisa):

```
defaults del código  →  preset de sistema  →  spec de usuario (extends)
                     →  override de proyecto (.llamacode/harness.json)
                     →  override de sesión / Task
```

Reglas duras:
- **Un módulo ausente = heredado**, nunca "vacío". Sin esto la herencia miente.
- Los presets de sistema siguen siendo inmutables; "editar Avanzado" =
  `duplicate` + `extends: agent-avanzado`.
- Ningún módulo puede **subir** privilegios por encima del scope de la Task ni
  saltear el guardrail Zero-Autonomy salvo modo `super` explícito.
- Todo módulo tiene un default que reproduce el comportamiento de hoy →
  perfiles viejos siguen andando bit a bit.

---

## 4. Etapas

Cada etapa: código + test registrado con `add_lc_test` + `tests.bat` verde antes
de commitear. Cada etapa es entregable sola.

### E0 — Contrato `HarnessSpec` (sin consumidores)
- `src/core/profiles/HarnessSpec.{h,cpp}`: structs de los 9 módulos, `toJson` /
  `fromJson`, y `resolve(base, override)` **puro**.
- `AgentProfile::toSpec()` — mapea los campos de hoy al spec (capa 1). El JSON
  viejo se lee igual (`fromJson` sin `spec{}` → spec derivado).
- Defaults = constantes actuales, extraídas a `HarnessDefaults`.
- Test `tests/test_harness_spec.cpp`: herencia (ausente≠vacío), cadena de
  `extends` con ciclo detectado, round-trip JSON, migración de un `AgentProfile`
  legacy, y que el spec de cada preset de sistema reproduce sus valores actuales.
- **Riesgo cero**: nadie lo consume todavía.

### E1 — Cablear `loop` + `context` (destrabar las constantes)
- `LlamaAgentBackend::setLoopPolicy(LoopModule)` y `setContextPolicy(ContextModule)`.
  Reemplazar `kMaxSameCall`, `kMaxTransportRetries`, `kFailureSpiralThreshold`,
  `streamIdleTimeoutMs()`, `toolWatchdogSeconds()` (base + multiplicador por
  grupo), `trimStaleImages` keepLast, umbral de compactación, on/off de
  read-dedup y de preflight por campos del spec. `kMaxTurnIters` queda como techo
  duro no configurable (es un fusible, no una política).
- Precedencia del idle timeout: spec > env `LLAMACODE_STREAM_IDLE_TIMEOUT` >
  default. Documentarlo.
- `applyAgentProfileCaps()` pasa a aplicar el spec resuelto.
- Test en `tests/test_agent_wire.cpp` + nuevo `tests/test_harness_modules.cpp`:
  un spec con `sameCallLimit=1` corta a la primera repetición; `keepLastImages=0`
  saca todas las capturas; compactación con umbral bajo dispara y con umbral alto
  no. Sin spec → valores de hoy (test de no-regresión).

### E2 — Packs de tools
- `ToolsModule`: `packs[]` + `include[]` + `exclude[]` + `mcp{servers, toolsEnabled}`.
  Los packs salen del `group` que **ya** tiene `toolCatalog()`
  (Archivos/Búsqueda/Código/Web/Escritorio/Correo/Multi-Agente/Conocimiento/
  Habilidades/Browser/Revisión) más packs compuestos (`core`, `rag`, `rpa`).
- Resolución: `packs` → expandir → `include` suma → `exclude` resta. Determinista
  y ordenada.
- **Preflight de dependencias**: `task` sin git, `semantic_search`/`hybrid_search`
  sin endpoint de embeddings, `desktop_*` sin sesión de escritorio,
  `email_send` sin cuenta → warning en el editor y en headless, no crash en runtime.
- **Presupuesto de contexto**: sumar `approxTokens` del catálogo y mostrar el
  costo del pack en el editor (el dato ya existe, no se usa).
- Test: expansión pack→tools, precedencia include/exclude, presupuesto, matriz de
  dependencias faltantes.

### E3 — Packs de prompt (directivas propias)
- Hoy `directiveCatalog()` está compilado. Sumar **directivas de usuario** en
  Markdown, reusando la convención de `PortableSkillStore`:
  `<AppLocalData>/harness/directives/<slug>.md` y
  `<workspace>/.llamacode/directives/<slug>.md`, con frontmatter
  `{name, description, when}`.
- `PromptModule`: `builtin[]` (keys de hoy) + `custom[]` (slugs) + `order[]` +
  `systemExtra`. `when` permite gating declarativo (`tools.desktop`,
  `vision`, `project.hasGit`) — reemplaza la heurística que hoy decide si
  inyectar `desktopPlaybookSection`.
- `buildSystemPrompt()` pasa a **componer por lista ordenada** en vez de
  concatenar bloques con `if`s.
- Límite duro de tamaño del prompt compuesto, con aviso: una directiva de usuario
  de 8 KB es una regresión de contexto silenciosa.
- Test: orden estable, gating por `when`, directiva inexistente = warning y sigue,
  tope de tamaño, y que el prompt de cada preset de sistema **no cambia**
  (snapshot).

### E4 — Permisos declarativos
- `PermModule`: `approvalMode`, `rules[]` (`allow|deny|ask [kind:]<glob>`, formato
  que `setPermissionRules` ya entiende), `fsScope` (`project|folder|full` +
  carpetas), `guardrails{hitlDestructive, mailAutoSend, alwaysAllowKinds}`.
- El perfil deja de necesitar que el usuario toque Ajustes globales para esto.
- **Invariante**: el scope del perfil se **intersecta** con el de la Task, nunca
  lo amplía. `hitlDestructive:false` sólo tiene efecto en `approvalMode:"super"`.
- Test: intersección de scopes, precedencia regla→política, que un perfil no puede
  desactivar el guardrail fuera de `super`, y que `deny` gana sobre `allow`.

### E5 — Escalación y multi-agente por perfil
- Mover/duplicar `MasterConfig` al spec (`escalation.master`), con
  `LaunchProfile.master` como fallback (compat).
- `escalation.subagents{maxParallel, isolate, promptPack, honey}` y
  `escalation.router{thresholds de DifficultyRouter}` — hoy fijos.
- `adaptiveSubagentLimit()` pasa a recibir el techo del spec en vez de
  `kAbsoluteMaxParallelSubs` como único límite (el absoluto sigue siendo tope).
- Test: cadena de fallbacks resuelta desde el spec, cap efectivo = min(spec,
  adaptativo, absoluto), thresholds del router aplicados.

### E6 — Fases
- `PhasesModule`: overrides por fase — `plan`, `exec`, `verify`, `goalCheck`.
  Cada fase puede pisar `tools`, `prompt`, `protocol` y `launchProfileId`.
- Encaja con lo que ya existe: `plannerProfileId` / `hybridMode` de `LaunchProfile`
  y el swap de modelo de verify-phase de Loops.
- Fase ausente = spec base (comportamiento de hoy).
- Test: resolución por fase, que `exec` sin override == spec base, y que el
  goal-check de Loops toma su override (extender el harness de
  `tests/test_appcontroller.cpp` con `FakeAgentBackend`).

### E7 — Editor modular (QML)
- Página/diálogo de perfil de agente reorganizado por módulos (acordeón), con:
  - **Diff contra el `extends`**: "tu perfil cambia 6 cosas respecto de Avanzado".
    Es lo que hace mantenible un perfil propio.
  - **Costo de contexto en vivo** (suma de `approxTokens` + tamaño del prompt).
  - **Warnings de dependencias** (E2) y de tamaño de prompt (E3).
  - Import / export JSON de un spec (`.llamacode/harness.json` o archivo suelto),
    para compartir un harness entre máquinas o pegarlo en un issue.
- Test QML con el patrón de `tests/qml/tst_color_picker.qml` para los widgets
  nuevos; la lógica de diff/costo vive en C++ (`HarnessSpec::diff(base, spec)`,
  pura y testeada).

### E8 — Headless + benchmark
- `ControlApi`: `harness.list`, `harness.get`, `harness.set`, `harness.apply`,
  `harness.diff` (contrato headless obligatorio, ver `docs/HEADLESS.md`).
- Correr una tarea con dos specs y comparar con
  `AgentEfficiency::benchmarkComparison()` — cierra el ciclo: personalizar sin
  medir es adivinar. Un flag en `tools/` para barrer specs sobre el pack de
  benchmark existente.
- Test: `tests/test_control_api.cpp` con los verbos nuevos; comparación de dos
  specs sobre corridas sintéticas.

---

## 5. Presets después del cambio

Los cinco niveles sobreviven como **specs de sistema**, no como código:
`agent-chat`, `agent-basico`, `agent-intermedio`, `agent-avanzado`, `agent-maximo`
pasan a ser JSON bundleado, cada uno con `extends` del anterior donde tenga
sentido. Beneficio directo: la escalera queda legible y auditable, y agregar
"Avanzado sin web" es un archivo, no un `if`.

Sumar dos presets que el review de codehamr ya pedía y hoy no existen:
- **`agent-minimal`** — modo local-first duro: 5 tools, sin MCP, prompt corto,
  budgets de salida estrictos. Para 7B–30B con 32k de contexto.
- **`agent-rpa`** — pack de escritorio + playbook + guardrails apretados.

---

## 6. Compatibilidad y migración

- `AgentProfile` **no se borra**: sigue siendo la fila persistida en
  `agent_profiles/`, con un campo `spec{}` opcional. Perfil viejo sin `spec` →
  `toSpec()` lo deriva. Perfil nuevo → los campos legacy se siguen escribiendo
  para que una versión anterior del app no se rompa (una versión, después se
  limpia).
- Un spec que referencia un pack/directiva/preset inexistente **no invalida el
  perfil**: warning + fallback al default. Un perfil roto que no arranca el agente
  es peor que uno degradado.
- Test de migración explícito en E0, con un JSON de perfil de la versión actual
  congelado como fixture.

## 7. Orden sugerido y por qué

```
E0 (contrato) → E1 (loop+context, el gap más grande) → E2 (tools) → E3 (prompt)
                                                          ↓
                                    E4 (permisos) → E5 (escalación) → E6 (fases)
                                                          ↓
                                                 E7 (UI) → E8 (headless+bench)
```

E1 primero porque es donde está el valor que hoy no se puede tocar de ninguna
forma (ni editando JSON a mano). E7 al final a propósito: la UI sobre un contrato
que todavía se mueve se reescribe dos veces.

## 8. Riesgos

| Riesgo | Mitigación |
|---|---|
| Explosión de superficie configurable → perfiles rotos e irreproducibles | Defaults == comportamiento actual; diff contra el `extends`; presets de sistema inmutables |
| Un perfil sube privilegios | Intersección (nunca unión) con Task; guardrail sólo desactivable en `super`; test dedicado |
| Regresión silenciosa del prompt | Snapshot del system prompt de los 5 presets en E3 |
| Contexto inflado por packs de usuario | Presupuesto en `approxTokens` + tope de prompt + warnings |
| Herencia confusa | "Módulo ausente = heredado" como invariante testeada, no como convención |
