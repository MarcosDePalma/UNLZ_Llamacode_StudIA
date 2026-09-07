# Fable Fusion Qwen3.6-27B Q6 MTP frente a ThinkingCap

Fecha: 2026-08-08. Hardware: 2x RTX 3090 24 GB, Ryzen 7 7700, Windows.

## Veredicto

Fable Fusion Q6 MTP queda integrado y marcado como favorito/evaluado, todavía
experimental y opt-in, en el tier
48 GB. En una batería textual de instrucciones estrictas obtuvo 26/30 (86,7%)
frente a 12/30 (40%) de ThinkingCap Q4 MTP3 y terminó cada tarea en una mediana
de 2,41 s frente a 6,67 s. En BigCodeBench-Hard empató a ThinkingCap y KAT con
3/8 en dos pasadas. No reemplaza al perfil operativo: fue más lento y falta repetir
la suite agentica E2E con archivos, tools y tests reales.

## Compatibilidad

- Modelo MTP: `Qwen3.6-27B-Fable-Fus-711-UnHeretic-NM-DAU-NEO-MAX-NEO-MTP-Q6_K.gguf`
  (24.033.705.440 bytes).
- Modelo regular de control: `...NEO-Q6_K.gguf` (23.582.384.672 bytes).
- Visión: `mmproj-F16.gguf` (927.607.360 bytes).
- llama.cpp b10228 genera salida corrupta con ambos GGUF, incluso sin MTP.
- llama.cpp b10331 corrige la salida; el perfil exige esa build como mínimo.
- `split-mode=tensor` dispara una aserción en b10228. El perfil usa `layer` y
  `tensor-split 1,1`.

## Calidad textual reproducible

Runner: `tools/benchmark_fable_vs_thinkingcap.ps1`, diez tareas de matemática,
lógica, parsing, formato y código, tres pasadas, mismo sampling conservador,
32k de contexto, K=f16/V=q8, MTP3 y reasoning off.

| Perfil | Score | Decode mediano | Tiempo mediano/tarea |
|---|---:|---:|---:|
| ThinkingCap Q4 MTP3 | 12/30 | 54,50 t/s | 6,67 s |
| Fable Fusion Q6 MTP3 | **26/30** | 44,23 t/s | **2,41 s** |

Fable decodifica menos tokens por segundo, pero obedece mejor el formato y usa
menos tokens. ThinkingCap agotó con frecuencia el límite de 512 tokens antes de
emitir la respuesta final. Los dos fallaron de forma consistente el caso ambiguo
`a=b=c` y una salida JSON extremadamente estricta.

Artefacto completo:
`D:/Models/llamacpp/Qwen3.6-27B-Fable-Fusion-Q6/quality-3passes-b10331-instruct.json`.

## Barrido de inferencia

Mismo prompt corto, tres repeticiones por punto:

| Configuración | TPS observados |
|---|---:|
| MTP2, K=f16/V=q8 | 47,48–50,74 |
| MTP3, K=f16/V=q8 | 52,76–58,96 |
| MTP4, K=f16/V=q8 | **58,05–61,23** |
| MTP4, K=q8/V=q8 | 63,48–65,59 |

El barrido corto favoreció MTP4. Sin embargo, la carga sostenida descrita abajo
mostró que 120k/MTP4 no es estable. El perfil adopta 32k/MTP3. K=f16 se mantiene
porque cuantizar K puede degradar coherencia a contexto largo; q8/q8 queda como
variante de benchmark.

## Smoke final a 120k con visión

La configuración integrada cargó en b10331 con MTP4, `mmproj-F16`, K=f16/V=q8,
batch 2048, ubatch 512 y un slot efectivo de 120.064 tokens. Respondió
`FINAL: ready` correctamente. El uso observado fue 16.087 MiB en CUDA0 y
17.321 MiB en CUDA1, sin spill a RAM ni presión de VRAM.

## BigCodeBench-Hard sostenido

Se ejecutaron los mismos ocho IDs públicos y el mismo grader por tests usados para
KAT, ThinkingCap, Laguna y DeepSeek. La configuración 32k/MTP3 completó dos pasadas
idénticas: 3/8 + 3/8, 6/16 (37,5%), 71,5 s de media, 5.084 tokens y ~50,3 t/s.
Pasó siempre `/928`, `/906` y `/139`.

Antes de aceptar el resultado se probaron 120k/MTP4, 120k/MTP3 y 120k sin
speculative decoding. MTP4 llegó a completar una pasada de 2/8, pero luego cayó;
las tres alternativas de 120k terminaron con pérdida de transporte/acceso CUDA
ilegal y se descartaron como corridas inválidas. Reducir a 32k eliminó el crash en
las 16 requests. Artefacto válido: `BigCodeBench-Hard_8_tems__20260809_105610`.

## Pendiente antes de promover

1. Ejecutar `Agent efficiency E2E v1` x3 contra ThinkingCap con archivos y tests.
2. Medir calidad con prompts realmente largos cerca de 120k; carga y generación
   corta ya quedaron validadas.
3. Agregar un set de visión con imágenes y respuestas verificables.
