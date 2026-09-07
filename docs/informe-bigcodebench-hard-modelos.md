# Informe comparativo: BigCodeBench-Hard por modelo

Fecha de consolidación: 2026-08-09.

## Corrida oficial completa (148 tareas)

Además de la muestra exploratoria de ocho tareas documentada más abajo, se
ejecutó el protocolo oficial completo BigCodeBench-Hard Instruct sobre 148
tareas. El evaluador local fue validado con las soluciones canónicas: 147/148
(99,3%), con la única falla esperada en `BigCodeBench/590`. Los tiempos de la
tabla son de generación de las 148 respuestas; la evaluación de tests se hizo
después y no está incluida.

| posición | perfil | estado | resultado | tiempo |
|---:|---|---|---:|---:|
| 1 | **DeepSeek-V4_IQ3-S_SinDrafter_131k** | Completo | **31,76% — 47/148** | **2h48m24s** |
| 2 | **KAT-Coder-2.5_Q4-K-M_SinDrafter_262k** | Completo | **30,41% — 45/148** | **9m22s** |
| 2 | Fable-Q6-K_MTP3_32k | Completo | 30,41% — 45/148 | 22m48s |
| 4 | ThinkingCap_Q4-K-M_MTP4_196k | Completo | 28,38% — 42/148 | 17m45s |
| 4 | Laguna-S-2.1_UD-Q2-K-XL_SinDrafter_100k | Completo | 28,38% — 42/148 | 15m46s |

MiniMax quedó explícitamente excluido de esta corrida completa. DeepSeek mejora
al KAT Q4 en sólo 2 tareas y 1,35 puntos, pero tarda aproximadamente 18 veces
más.

### Experimento KAT-Coder Q5 descartado

`Q5_K_M` obtuvo 41/148 (27,70%) tanto sin drafter como con `NgramMod`, frente a
45/148 (30,41%) del `Q4_K_M`. Sin drafter tardó 9m15,5s y con `NgramMod`
10m05,5s; por lo tanto Q5 no aportó calidad ni una mejora operativa relevante.
El modelo, sus perfiles y las corridas locales se eliminaron después de documentar
el resultado. Se conserva únicamente esta nota para evitar repetir el experimento.

## Resumen ejecutivo

Se evaluaron seis perfiles locales sobre las mismas ocho tareas de
BigCodeBench-Hard, con dos pasadas válidas por perfil. KAT-Coder,
ThinkingCap+MTP y Fable Fusion empataron el primer puesto con 6/16 (37,5%). KAT
obtuvo esa calidad con el menor tiempo: fue 1,8× más rápido que ThinkingCap y
2,0× más rápido que Fable. Laguna quedó en 25%. DeepSeek V4 y MiniMax M2.7
empataron en 12,5%; MiniMax necesitó 3,9× el tiempo de DeepSeek y 50× el de KAT.

| posición | perfil | pasadas | total | tiempo medio | tokens (2 pasadas) | decode ponderado |
|---:|---|---:|---:|---:|---:|---:|
| 1 | **KAT-Coder** | 3/8, 3/8 | **6/16 (37,5%)** | **35,6 s** | 5.733 | **118,4 t/s** |
| 1 | **ThinkingCap+MTP** | 3/8, 3/8 | **6/16 (37,5%)** | 64,6 s | 6.027 | 64,3 t/s |
| 1 | **Fable Fusion Q6 MTP3 32k** | 3/8, 3/8 | **6/16 (37,5%)** | 71,5 s | 5.084 | 50,3 t/s |
| 4 | Laguna S 2.1 Q2 100k | 2/8, 2/8 | 4/16 (25,0%) | 91,1 s | 10.267 | 64,2 t/s |
| 5 | DeepSeek V4 IQ3_S | 1/8, 1/8 | 2/16 (12,5%) | 460,5 s | 4.950 | 5,6 t/s |
| 5 | MiniMax M2.7 Q3_K_S 32k | 1/8, 1/8 | 2/16 (12,5%) | 1.783,8 s | 19.200 | 5,4 t/s |

## Protocolo

- Hardware: 2× RTX 3090 (48 GB de VRAM total) y 128 GB de RAM.
- Dataset: BigCodeBench-Hard v0.1.4, split Instruct.
- Muestra fija: `/928`, `/765`, `/771`, `/1019`, `/906`, `/583`, `/139` y
  `/360`.
- Selección: orden SHA-256 con seed fija; se excluyeron tareas de red, procesos y
  dependencias ausentes.
- Control del instrumento: la solución canónica debía aprobar localmente los
  tests oficiales antes de admitir una tarea. Esa solución no se incluyó en el
  prompt.
- Inferencia: misma muestra, target `model`, temperatura conservadora 0,60 y dos
  pasadas completas por perfil.
- Score: tests ejecutables sobre la función generada; completar una respuesta no
  equivale a aprobar.

### Matriz de resultados por tarea

Los aciertos fueron idénticos entre las dos pasadas de cada perfil.

| perfil | /928 | /765 | /771 | /1019 | /906 | /583 | /139 | /360 |
|---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| KAT-Coder | ✓ | — | — | — | ✓ | — | ✓ | — |
| ThinkingCap+MTP | ✓ | — | — | — | ✓ | — | ✓ | — |
| Fable Q6 MTP3 32k | ✓ | — | — | — | ✓ | — | ✓ | — |
| Laguna S 2.1 | ✓ | — | — | — | — | — | ✓ | — |
| DeepSeek V4 IQ3_S | ✓ | — | — | — | — | — | — | — |
| MiniMax M2.7 Q3_K_S | — | — | — | — | ✓ | — | — | — |

## Análisis por modelo

### KAT-Coder Q4_K_M

**Resultado:** 3/8 en ambas pasadas; 31,3 s y 40,0 s, 5.733 tokens totales.

Fue el perfil con mejor relación calidad/latencia. Aprobó `/928`, `/906` y
`/139`, exactamente el mismo conjunto que ThinkingCap y Fable, pero con 118,4
t/s. Su configuración dual-GPU usa reparto `layer 1,1`, KV q8 y contexto
seleccionable desde 32k hasta 262k. Es el default recomendado para coding local:
ningún modelo obtuvo más aciertos y todos necesitaron más tiempo.

**Lectura:** ganador operativo. El límite observado es de resolución de las tareas,
no de velocidad ni estabilidad.

### ThinkingCap+MTP Q4_K_M

**Resultado:** 3/8 en ambas pasadas; 58,9 s y 70,2 s, 6.027 tokens totales.

Repitió los tres aciertos de KAT. MTP4 elevó el decode a 64,3 t/s, pero aun así
la pasada media tardó 1,8× más que KAT. No apareció ningún acierto exclusivo que
justificara el costo adicional en este conjunto. Conserva valor cuando se necesita
su soporte multimodal/visión, capacidad que este benchmark no mide.

**Lectura:** alternativa funcional y estable, no una mejora de coding sobre KAT.

### Fable Fusion Qwen3.6-27B Q6, MTP3 32k

**Resultado:** 3/8 en ambas pasadas; 68,8 s y 74,3 s, 5.084 tokens totales.

El quant Q6 empató exactamente los aciertos de los dos Qwen Q4. La configuración
estable fue b10331, contexto 32k, KV K=f16/V=q8 y MTP3. El decode ponderado fue
50,3 t/s: 10,7% más lento que ThinkingCap y aproximadamente 2× más lento que
KAT. Las variantes 120k/MTP4, 120k/MTP3 y 120k sin speculator no sostuvieron dos
pasadas: aparecieron accesos CUDA ilegales. Esos intentos se excluyeron del score.

**Lectura:** calidad competitiva y repetible a 32k; no ofrece una ganancia medible
frente a KAT y su preset largo todavía no es estable.

### Laguna S 2.1 118B-A8B UD-Q2_K_XL

**Resultado:** 2/8 en ambas pasadas; 92,5 s y 89,7 s, 10.267 tokens totales.

Aprobó `/928` y `/139`. El modelo completo entró en las dos GPU con KV q4 y
reparto `layer 1,1`; mantuvo estabilidad y 64,2 t/s de decode. Produjo casi el
doble de tokens que el grupo líder, pero obtuvo un acierto menos. Aun así duplicó
el score de DeepSeek usando cerca de una quinta parte de su tiempo.

**Lectura:** buen rendimiento técnico para un 118B Q2, pero queda por debajo de los
Qwen especializados en coding.

### DeepSeek V4 Flash 0731 UD-IQ3_S

**Resultado válido:** 1/8 en ambas pasadas; 457,9 s y 463,1 s, 4.950 tokens
totales.

Sólo aprobó `/928`. En los fallos cambió firmas, argumentos o retornos exigidos,
renombró `task_func` y se extendió repetidamente en `/771`. No consiguió ningún
acierto exclusivo. El perfil dual-GPU original falló dos veces al primer prompt
con `CUDA illegal memory access`; por eso la medición de capacidad usa el mismo
IQ3_S con el perfil conservador ULTRA-Q y offload estable. Los intentos caídos,
sin tokens ni score válido, no se mezclaron con el resultado.

**Lectura:** el tamaño y el razonamiento no se tradujeron en mejor cumplimiento de
API. Para funciones aisladas no justifica 460,5 s por pasada.

### MiniMax M2.7 Q3_K_S

**Resultado:** 1/8 en ambas pasadas; 1.777,6 s y 1.790,1 s, 19.200 tokens totales.

Se optimizó antes de medirlo. El mejor punto estable fue llama.cpp b10331,
`--n-cpu-moe 45`, `--split-mode layer` y `--tensor-split 3,1`. Usa alrededor de
9,1/22,6 GB de VRAM y ejecuta tool-calling nativo. Aprobó sólo `/906`; falló las
otras siete tareas y casi todas sus respuestas agotaron el límite de 1.200 tokens.
Por eso una llamada corta alcanzó 11,4 t/s, pero la carga sostenida del benchmark
quedó en 5,4 t/s. Fue estable durante las 16 tareas, sin OOM ni crash.

**Lectura:** la compatibilidad quedó resuelta, pero no la eficiencia. Empata la
calidad de DeepSeek, tarda 3,9× más que él y 50× más que KAT.

## Decisión recomendada

1. Usar **KAT-Coder** como perfil principal de coding: empata el mejor score y
   domina en latencia.
2. Usar **ThinkingCap+MTP** cuando la tarea también requiera visión.
3. Mantener **Fable Q6 MTP3 32k** como perfil experimental estable; no promover
   sus presets de 120k hasta corregir las caídas CUDA.
4. Mantener **Laguna**, **DeepSeek** y **MiniMax** como opciones de investigación,
   no como defaults de coding.
5. El siguiente benchmark debe ser agentic E2E sobre un repositorio y tools. Este
   estudio mide generación de una función; no demuestra ni descarta una ventaja en
   trabajo acumulativo de proyecto.

## Trazabilidad y limitaciones

Los resultados provienen de los JSON guardados bajo el directorio local
`benchmark-runs` de LlamaCode. Se conservaron sólo dos pasadas comparables por
perfil. Hubo corridas diagnósticas adicionales y configuraciones inestables; no se
promediaron con la tabla principal. Ocho tareas permiten comparar estos perfiles
bajo condiciones idénticas, pero no estimar con precisión el pass@1 global de las
148 tareas del split Hard. La igualdad entre pasadas muestra reproducibilidad de
esta muestra, no generalización a todo el dataset.
