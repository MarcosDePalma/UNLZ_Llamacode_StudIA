# Validación Qwen3.8 QAT Q2 / DFlash2 — 2026-08-28

## Resultado ejecutivo

La validación local quedó limitada por disponibilidad y compatibilidad de artefactos:

- No hay un GGUF QAT Q2 de Qwen3.8 ni un draft DFlash2 Q2_K_S-MIX en los discos locales.
- El binario `llama.cpp` b10331 reconoce `draft-dflash`, pero la pareja disponible
  Qwen3.8 Q4 estándar + DFlash2 Q4 no carga: el loader informa `wrong number of
  tensors; expected 81, got 58`.
- El target Qwen3.8 Q4 estándar sí carga con KV Q5_1 a 32k, 64k y 100k.
- El artefacto MTP local sí es compatible con ese target y produjo una mejora
  preliminar de throughput, con la misma calidad del pequeño smoke (2/4). Es un
  control experimental, no una promoción a perfil predeterminado.

## Entorno y reproducibilidad

Servidor usado:

- `D:\Models\llamacpp\llama.cpp-b10331-cuda12.4\llama-server.exe`
- versión `10331 (7ba604f1c)`, Clang 20.1.8 para Windows x86_64
- capacidades verificadas: `--spec-type draft-dflash`, `--spec-draft-model`,
  `--spec-draft-n-max`, `--cache-type-k` y `--cache-type-v`

Hardware observado: 2x RTX 3090 de 24 GB. Configuración común: `split-mode
layer`, `tensor-split 1,1`, `--n-gpu-layers 999`, Flash Attention, un slot,
`--batch-size 1024`, `--ubatch-size 256`, contexto según la etapa, KV `q5_1`,
sin context shift, Jinja, reasoning off, `seed 42`.

Sampling común: `temperature 0.6`, `top-p 0.95`, `top-k 20`, `min-p 0.0`,
`repeat-penalty 1.0`, `presence-penalty 0.0`. Se usó el template
`assets/chat-templates/qwen38-tools-fixed.jinja`.

Artefactos usados y hashes SHA-256:

| Artefacto | Bytes | SHA-256 |
|---|---:|---|
| llama-server b10331 | 9,216 | `23f1fa1f7fc673768b10fba736e3bc34ba6ae03187f94c37eb70d9699617ee9d` |
| Qwen3.8 Q4_K_M target | 17,106,775,008 | `7e78da5d7e3ae28d178121f58646953305f3e5bd3cb46f4a75584e8b6c6fe169` |
| DFlash2 Q4_K_M.v2 draft | 1,143,006,752 | `18a380efc9b7ed8d88677fc895f5c11ae170653434ee378f7348f715c14d0594` |
| MTP Q4_0 draft | 1,369,590,656 | `50d9ce5a6da381bbcfb31061cf73df94a90e6faf8efeddee379a9cb8f1501c6e` |
| chat template | 27,835 | `03c3e852d2c349ce306ed9ecd870c83487bf123918843387b179ead1fcce93af` |

El runner reproducible quedó en
`tools/run_qat_dflash2_validation.ps1`. Cada etapa conserva JSON y logs de
servidor en `docs/benchmark-artifacts/`.

## Smoke de calidad y rendimiento a 32k

Se ejecutó un warm-up no medido y cuatro prompts fijos: último dígito,
permutaciones de MISSISSIPPI, salida de Python y revisión breve de un requisito
de código. `ok` exige el marcador final esperado; una respuesta truncada cuenta
como fallo.

| Modo | Calidad | TTFT mediana superior (ms) | Decode mediana superior (tok/s) | Wall mediana superior (s) |
|---|---:|---:|---:|---:|
| Baseline autoregresivo | 2/4 | 164.8 | 35.32 | 3.820 |
| ngram-mod | 2/4 | 164.1 | 35.71 | 3.783 |
| MTP Q4, `n-max 4` | 2/4 | 179.3 | 60.34 | 2.349 |

El MTP reportó 482 tokens draft, 322 aceptados y 121 verificaciones: aceptación
de tokens draft de `322/482 = 66.8%`. El throughput agregado reportado por
metrics fue 56.39 tok/s. Para baseline y ngram-mod metrics reportó cero tokens
draft y cero aceptaciones; por tanto ngram-mod no se activó de forma efectiva en
estos prompts y no debe presentarse como speedup speculative.

La captura puntual de VRAM durante un baseline cargado fue aproximadamente
9,740 MiB en GPU0 y 9,876 MiB en GPU1; después del teardown propio quedó en
aproximadamente 1,157 MiB y 912 MiB. La memoria física total observada fue
127.2 GB; el sistema quedó con aproximadamente 95.7 GB libres al finalizar.
Son muestras puntuales, no un perfil de máximos sostenidos.

## Escalera de contexto

Los smoke de capacidad se repitieron con baseline y ngram-mod, mismos parámetros,
pero `max_tokens 64`; por eso su calidad no es evaluable y no entra en el ranking:

| Contexto configurado | Baseline | ngram-mod | Observación |
|---:|---|---|---|
| 32k | carga y generación válidas | carga y generación válidas | smoke de calidad válido |
| 64k | carga y generación válidas | carga y generación válidas | 0/4 por truncamiento a 64 tokens |
| 100k | carga y generación válidas | carga y generación válidas | 0/4 por truncamiento a 64 tokens |
| 131k | omitido | omitido | no necesario tras 100k y sin QAT/DFlash2 compatible |

## DFlash2 y omisiones

El intento reproducible con `--spec-type draft-dflash`, target Qwen3.8 Q4 y el
draft DFlash2 Q4 falló antes de `/health`:

```text
done_getting_tensors: wrong number of tensors; expected 81, got 58
failed to load draft model
```

Esto demuestra incompatibilidad de esta pareja con este loader; no demuestra que
DFlash2 esté roto en general. No se midieron aceptación, TTFT ni TPS de DFlash2.

No se ejecutaron QAT Q2, DFlash2 Q2_K_S-MIX, HE20, BCB, tool-calling ni una
campaña de 100k/131k con esos artefactos porque no están disponibles localmente
o no se verificó una pareja compatible. El MTP se probó por separado sólo después
de que el loader aceptó el artefacto local.

## Conclusión operativa

Para LlamaCode, QAT Q2 + DFlash2 Q2 sigue siendo una línea de investigación
opt-in condicionada a conseguir el target QAT Q2 y su draft compatible. No se
promueve a predeterminado ni se afirma equivalencia con modelos remotos. El MTP
Q4 local merece una campaña posterior con más pasadas, contexto largo y
tool-calling antes de cualquier recomendación estable. ngram-mod puede quedar
como control, pero esta muestra no evidenció generación especulativa efectiva.

