# Matriz completa DeepSeek y tiers SOL/TERRA/LUNA/METEOR

Esta campaña compara la configuración completa de `llama-server` y el agente:
GGUF, contexto, batch, KV, speculative/MTP, mmproj, reasoning, thinking y
harness. Un resultado sólo es promovido a la siguiente etapa si la huella
efectiva de modelo **y harness** coincide.

## Etapas

La petición se expresa como HE0 → BCB, pero el runner conserva la compuerta
intermedia obligatoria: `HE0 → HE20 → BCB`. HE20 no se salta porque evita
promover a BCB una configuración que sólo resolvió una tarea.

- HE0: `059b6f00-7b4b-48ff-a3a2-2a71f900c1c0`.
- HE20: `267bb33d-4510-417b-ae3c-6dbbfb2cb08d`.
- BCB: `05c28394-11d0-41fe-a55f-b3cb69db9c15`.

## Familias y variantes

| Grupo | Perfiles incluidos | Ejes | Visión/mmproj |
|---|---|---|---|
| DeepSeek IQ3_S 24 GB | `sys-ultraq-dsv4-0731-iq3s` y variantes `sys-bench-ultraq-*` | 64/131k, B/U, MoE CPU, KV q4/q8, reasoning off/low/medium/high, sin DSpark | No hay mmproj local en `D:\Models\llamacpp\DeepSeek-V4-Flash-0731-UD-IQ3_S`; no se inventa una prueba de visión |
| DeepSeek IQ3_S 48 GB | `sys-ultraq-dsv4-0731-iq3s-48gb`, `sys-48-dsv4-nospec` y variantes dual | 64/131k, KV q4/q8, reparto experto, reasoning off/medium/high, sin DSpark | Sin mmproj local; la variante dual conserva el reparto validado `1,0` |
| DeepSeek Antirez Q2/Q4 | `sys-48-antirez-dsv4-q2q4-0731` y sus `benchmarkVariants` activos; las variantes 32k `reasoning off/low` quedaron retiradas el 2026-08-28 | 16/32/64/131k, B/U, KV q4/q8, prefill, reasoning high y controles vigentes | Sin mmproj local |
| SOL | `8797a8cf-fea9-46cb-934a-0d62f3ee8ca7`, `abc1df7a-2af1-4957-9d12-dbe2d01988aa` | DSH medium, 160/192k, MTP2, KV q8, mmproj en RAM | Sí, mmproj Qwen3.8 |
| TERRA | `7d54c7f2-47dd-43df-a608-f67e4d4b027d`, `2c25280b-819e-411c-92fc-c5127cb3b900` | Browser Agent medium/xhigh, 131k, MTP, reasoning | Sí, mmproj Qwen3.8 |
| LUNA | `a03e65f5-2f2c-4d45-b67b-4b1270fa2a6c`, `sys-48-thinkingcap-131k`, `sys-48-thinkingcap-196k`, `sys-bench-48-tc-*` | MTP4, 131/196k, batch, KV q4/q8, thinking | Sí, `mmproj-ThinkingCap-Qwen3.6-27B-f16.gguf` |
| METEOR | `cbff7c85-2116-4b42-b1b9-485dd33384cc`, `sys-bench-48-bigbang-*`, `sys-repair-48-bigbang-*` | MTP/base, 64/131/196k, KV q8, ngram, reasoning off/high | BigBang declara mmproj; las variantes reparadas se miden primero text-only y luego visión si el backend carga |

## Harness y thinking

Cada configuración elegible se corre por separado con los perfiles disponibles:

- `agent-chat` / Sin fases ligero.
- `agent-intermedio` / Intermedio.
- `agent-maximo` / Con fases y máximo.
- `agent-browser` para TERRA y los controles Browser Agent.

El fingerprint del gate incluye ahora el `agentProfileId` y el hash del
harness. Por eso un HE0 de un harness no habilita HE20/BCB de otro.

## Métricas finales

Se conservarán por perfil: BCB `score/8`, tasa de aprobación, tiempo total,
TTFT, prefill tok/s, decode tok/s, tokens de reasoning, VRAM/RAM, contexto
efectivo, KV, mmproj, harness y motivo de fallo. Los `0/0`, crashes y
timeouts quedan como infraestructura; no se presentan como calidad.

Para consolidar la campaña sin mezclar corridas anteriores:

```powershell
.\tools\export_full_matrix_results.ps1 -Since (Get-Date).Date -Out .\benchmark-full-matrix-results
```

El exportador produce una fila por etapa y harness, más `finalBcb` agrupado por
perfil/harness, en JSON y CSV.
