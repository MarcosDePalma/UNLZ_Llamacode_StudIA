# Validación Qwen3.8 QAT Q2 / DFlash2 — 2026-08-29

## Resultado ejecutivo

La PC quedó libre y se repitió la validación local con una matriz separada de
la campaña del 2026-08-28. Los resultados no habilitan todavía QAT Q2 ni DFlash2
como perfil de LlamaCode:

- No hay un GGUF QAT Q2 de Qwen3.8 ni un draft DFlash2 Q2_K_S-MIX local.
- El `llama-server` b10331 reconoce `draft-dflash`, pero el target Qwen3.8 Q4
  estándar junto con el draft DFlash2 Q4 disponible vuelve a fallar antes de
  `/health` con `wrong number of tensors; expected 81, got 58`.
- El target Q4 estándar carga y genera en la escalera 32k, 64k, 100k y 131k
  bajo esta configuración. Los smoke de 64k, 100k y 131k tienen límites de
  salida demasiado bajos para juzgar calidad.
- MTP Q4 es el único modo especulativo que produjo drafts y aceptaciones. En
  32k registró 298/453 tokens draft aceptados (65,8 %) y 57,65 tok/s agregados.
- `ngram-mod` cargó, pero registró cero tokens draft y cero aceptaciones en el
  smoke; no se presenta como aceleración especulativa.

No se afirma equivalencia con modelos remotos ni se promueve ningún perfil a
predeterminado.

## Entorno y reproducibilidad

Servidor usado:

- `D:\Models\llamacpp\llama.cpp-b10331-cuda12.4\llama-server.exe`
- versión `10331 (7ba604f1c)`, Clang 20.1.8 para Windows x86_64
- capacidades verificadas: `draft-dflash`, `draft-mtp`, `ngram-mod`,
  `--spec-draft-model`, `--spec-draft-n-max`, `--cache-type-k` y
  `--cache-type-v`

Hardware: 2x RTX 3090 de 24 GB. Configuración común: `split-mode layer`,
`tensor-split 1,1`, `--n-gpu-layers 999`, Flash Attention, un slot,
`--batch-size 1024`, `--ubatch-size 256`, KV `q5_1`, sin context shift, Jinja,
reasoning off y `seed 42`.

Sampling común: `temperature 0.6`, `top-p 0.95`, `top-k 20`, `min-p 0.0`,
`repeat-penalty 1.0`, `presence-penalty 0.0`. Template:
`assets/chat-templates/qwen38-tools-fixed.jinja`.

Artefactos usados y hashes SHA-256, tomados del manifiesto de la corrida final:

| Artefacto | Bytes | SHA-256 |
|---|---:|---|
| llama-server b10331 | 9.216 | `23f1fa1f7fc673768b10fba736e3bc34ba6ae03187f94c37eb70d9699617ee9d` |
| Qwen3.8 Q4_K_M target | 17.106.775.008 | `7e78da5d7e3ae28d178121f58646953305f3e5bd3cb46f4a75584e8b6c6fe169` |
| DFlash2 Q4_K_M.v2 draft | 1.143.006.752 | `18a380efc9b7ed8d88677fc895f5c11ae170653434ee378f7348f715c14d0594` |
| MTP Q4_0 draft | 1.369.590.656 | `50d9ce5a6da381bbcfb31061cf73df94a90e6faf8efeddee379a9cb8f1501c6e` |
| chat template | 27.835 | `03c3e852d2c349ce306ed9ecd870c83487bf123918843387b179ead1fcce93af` |

El runner quedó en `tools/run_qat_dflash2_validation.ps1`. La corrida final de
32k conserva JSON y logs en
`docs/benchmark-artifacts/qat-dflash2-20260829-final/`; las corridas de
capacidad están en los directorios `qat-dflash2-20260829-64k`,
`qat-dflash2-20260829-100k` y `qat-dflash2-20260829-131k`.

## Smoke de 32k

Se usaron cuatro prompts fijos y `max_tokens 128`; `ok` exige que la respuesta
termine con el marcador esperado. Las dos tareas matemáticas pueden quedar
truncadas por ese límite, por lo que 2/4 es sólo un smoke conservador.

| Modo | Calidad | TTFT mediana superior (ms) | Decode mediana superior (tok/s) | Wall mediana superior (s) |
|---|---:|---:|---:|---:|
| Baseline autoregresivo | 2/4 | 163,4 | 36,01 | 3,774 |
| ngram-mod | 2/4 | 165,4 | 35,86 | 3,771 |
| MTP Q4, `n-max 4` | 2/4 | 167,5 | 63,29 | 2,228 |

Metrics agregadas del servidor:

- Baseline: 421 tokens predichos, 35,81 tok/s; drafts y aceptaciones: 0/0.
- ngram-mod: 421 tokens predichos, 34,74 tok/s; drafts y aceptaciones: 0/0.
- MTP: 413 tokens predichos, 57,65 tok/s; 453 drafts, 298 aceptados y 114
  verificaciones. Aceptación de tokens draft: `298/453 = 65,8 %`.

## Escalera de contexto

Los smoke de capacidad usaron baseline, ngram-mod y MTP con `max_tokens 64`,
excepto 131k que usó baseline con `max_tokens 16`. Por eso los `0/4` no son una
medición de calidad.

| Contexto | Baseline | ngram-mod | MTP | Interpretación |
|---:|---|---|---|---|
| 32k | carga/generación válidas | carga/generación válidas | carga/generación válidas | smoke de calidad separado |
| 64k | carga/generación válidas | carga/generación válidas | carga/generación válidas | capacidad; salida truncada |
| 100k | carga/generación válidas | carga/generación válidas | carga/generación válidas | capacidad; salida truncada |
| 131k | carga/generación válidas | omitido | omitido | capacidad; salida muy truncada |

Al finalizar quedó libre la máquina: no había LlamaCode, Ollama, llama-server ni
runner propio activos; RAM libre aproximada 99,1 GB y VRAM puntual 1.256/844 MiB
en las dos RTX 3090. Son muestras posteriores al teardown, no máximos sostenidos.

## DFlash2, QAT y omisiones

El intento final con `--spec-type draft-dflash`, target Qwen3.8 Q4 y el draft
DFlash2 Q4 disponible falló antes de `/health`:

```text
done_getting_tensors: wrong number of tensors; expected 81, got 58
failed to load model
```

El fallo se repitió en la campaña anterior y en esta. Demuestra que esta pareja
no es compatible con este loader; no demuestra que DFlash2 esté roto en general.
No se midieron TTFT, TPS ni aceptación de DFlash2.

QAT Q2, DFlash2 Q2_K_S-MIX, HE20, BCB y tool-calling con esos artefactos quedan
omitidos porque no hay archivos locales compatibles. El test de 131k sólo prueba
que el target Q4 puede iniciar y generar con una salida mínima; no es un juicio
de calidad ni confirma el comportamiento del QAT Q2 del post de Reddit.

## Cambios de instrumentación

Se corrigió el runner para:

- no pasar un argumento nulo cuando el modo baseline no tiene flags
- capturar la versión del binario sin abortar por el stderr normal de
  `llama-server --version`
- incluir el hash del draft MTP en el manifiesto
- registrar correctamente que los smoke con límite bajo son sólo de capacidad

No se modificaron C++, QML ni el comportamiento de la aplicación, por lo que no
se ejecutó un nuevo build/test por esta campaña. Se verificó que existe
`build\Debug\LlamaCode.exe`.

## Conclusión operativa

QAT Q2 + DFlash2 Q2 sigue siendo una línea de investigación opt-in. Para una
recomendación estable todavía faltan el target QAT Q2, su draft DFlash2 compatible,
pruebas de calidad con más tokens, tool-calling y una campaña de estabilidad.
MTP Q4 merece una campaña posterior más amplia, pero no se promueve a
predeterminado con esta evidencia.
