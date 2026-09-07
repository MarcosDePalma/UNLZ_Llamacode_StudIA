# Campaña DeepSeek local — 2026-08-30

Campaña manual de diagnóstico y comparación sobre la máquina local: Ryzen 7
7700, 128 GiB nominales de RAM (los cuatro DIMM quedaron configurados a
DDR5-4000), 2× RTX 3090 de 24 GiB, Windows, llama.cpp CUDA b10331
(commit `7ba604f1c`) salvo donde se indica b10228. La máquina no dispone de
NVLink y ambas placas se observan en PCIe Gen4 x8.

Los resultados de esta tabla son `llama-server` nativo, `/completion`, una
petición caliente de 256 tokens y un prompt real de 57 tokens. `--ctx-size`
es el límite nominal; no es una medición de decode después de llenar 131k.
Por eso sirven para aislar runtime/colocación, pero no reemplazan HE20/BCB.
Cada fila conserva su respuesta y su log en este directorio.

## Matriz ejecutada

| Caso | Modelo / build | Configuración | Resultado | Lectura |
|---|---|---|---:|---|
| IQ3 baseline | UD-IQ3_S / b10331 | 2 GPU, 131k, B4096/U1024, KV q4, `tensor-split 1,0`, expertos 29–36 en CUDA1 | **5,764 tok/s** | Arranca y genera 256 tokens |
| IQ3 más expertos | UD-IQ3_S / b10331 | Igual, expertos 25–36 en CUDA1 | **6,171 tok/s** | Mejor A/B: +7,1% sobre baseline |
| IQ3 demasiados expertos | UD-IQ3_S / b10331 | Igual, expertos 21–36 en CUDA1 | **3,604 tok/s** | Peor: −41,6%; no conviene maximizar residencia |
| IQ3 reparto 1:1 | UD-IQ3_S / b10331 | Igual, `tensor-split 1,1`, KV q4 | **5,381 tok/s** | Respuesta corta exacta “París”; −6,6% vs 1:0 |
| IQ3 KV q8 | UD-IQ3_S / b10331 | 2 GPU, 131k, B4096/U1024, `tensor-split 1,0`, KV q8 | **6,238 tok/s** | Cargó; +8,2% en esta pasada, no concluyente |
| IQ3 build anterior | UD-IQ3_S / b10228 | Misma receta base, KV q4 | **2,617 tok/s** | Arranca pero rinde −54,6% vs b10331 actual |
| IQ3 una GPU | UD-IQ3_S / b10331 | 1×3090, 131k, `n-gpu-layers 44`, `n-cpu-moe 39`, KV q4 | **6,071 tok/s** | La segunda GPU no acelera por sí sola esta huella |
| antirez q4 | híbrido antirez / b10331 | 2 GPU, 131k, B4096/U1024, KV q4, expertos 37–42 en CUDA1 | **8,280 tok/s** | Más rápido en este smoke nativo |
| antirez q8 | híbrido antirez / b10331 | 2 GPU, 64k, B4096/U1024, KV q8, expertos 37–42 en CUDA1 | **9,066 tok/s** | Cargó; no reemplaza su BCB histórico |
| IQ3 `load-mode none` | UD-IQ3_S / b10331 | 131k, misma colocación, carga sin mmap | **FALLA** | No reserva buffer host CUDA de 109.117.186.048 bytes |
| IQ3 `llama-bench` | UD-IQ3_S / b10331 | `llama-bench`, 128 tokens, 10 capas | **FALLA** | CUDA genérico durante carga; el `llama-server` sí carga |

Archivos principales: `iq3s-b10331-server-131k-parallel1-result.json`,
`iq3s-b10331-server-131k-experts25-36-result.json`,
`iq3s-b10331-server-131k-experts21-36-result.json`,
`iq3s-b10331-server-131k-split11-result.json`,
`iq3s-b10331-server-131k-kvq8-result.json`,
`iq3s-b10331-server-131k-single3090-result.json`,
`antirez-b10331-server-131k-result.json` y
`antirez-b10331-server-64k-kvq8-result.json`.

## Contexto largo y LID CUDA

El recibo existente `D:\Models\llamacpp\deepseek-lid-context-matrix-ultraq-ot-50.json`
se auditó junto con la matriz nueva:

| Contexto | KV / runtime | Recuperación exacta | Decode reportado | Estado |
|---:|---|---:|---:|---|
| 131k | LID CUDA, f16, `GGML_CUDA_NO_PINNED=1` | 1/1 | 4,733 tok/s | **Verificado** |
| 262k | LID CUDA, f16, `GGML_CUDA_NO_PINNED=1` | 1/1 | 4,877 tok/s | **Verificado** |
| 524k | LID CUDA, f16 | 0/1; conexión rechazada | — | Servidor terminó con `3221226505` |
| 1M | LID CUDA, f16 | No evaluable | — | Terminó antes de `/health` |
| 1M | LID CUDA, q4 KV | 0/1; timeout | — | No promocionable |

La capacidad confirmada de esta rama queda en 262k para recuperación exacta;
524k y 1M siguen siendo experimentales/no verificados. El IQ2_M no se probó:
no existe el GGUF local de sus tres shards y no se descargó material externo.

## Conclusión de la campaña

- La colocación óptima observada no es “más expertos en GPU” sin límite: en
  esta máquina el máximo medido fue el rango 25–36 (12 capas); 21–36 empeoró
  claramente.
- `tensor-split 1,1` no reprodujo con b10331 la corrupción histórica observada
  con b10228, pero quedó más lento en el smoke. Se mantiene `1,0` por ser la
  ruta más conservadora y por requerir validación E2E antes de cambiar el
  perfil.
- El perfil `sys-48-dsv4-nospec` queda marcado como **BEST dentro de DeepSeek
  por calidad comprobada**: es el único perfil DeepSeek/IQ3_S con BCB histórico
  8/8 y supera a DeepSeek Fusion en calidad (2–4/8). No se lo presenta como
  ganador universal de velocidad: antirez conserva el mejor BCB histórico
  DeepSeek (8/8, 10,548 tok/s).
- Ninguna cifra de esta matriz nativa invalida los resultados E2E históricos;
  las huellas, builds, longitudes de prompt y harnesses son distintas.
