# Evaluación de ThinkingCap-Qwen3.6-27B para LlamaCode

Fecha: 2026-07-28. Hardware: RTX 3090 24 GB, Ryzen 7 7700, Windows.

## Veredicto

ThinkingCap-Qwen3.6-27B fue promovido a **MAX-Q** en configuración 131k tras la
comparación E2E dense contra dense: obtuvo 15/15 sin reparación en 219,5 s y
232,9 s. El anterior MAX-Q Qwen3.6-27B de 262k se conserva como **MAX-CTX** para
tareas donde prima la ventana máxima. KAT Coder 2.5, por ser MoE, se mantiene
únicamente como referencia secundaria de calidad.

## Configuración medida

- Modelo: `bottlecapai/ThinkingCap-Qwen3.6-27B-GGUF`,
  `ThinkingCap-Qwen3.6-27B-Q4_K_M.gguf` (16,8 GB).
- Visión: `mmproj-ThinkingCap-Qwen3.6-27B-f16.gguf` (0,93 GB).
- Backend: llama.cpp CUDA b9763.
- Contexto 32768, batch 2048, ubatch 512, GPU layers `-1`, KV K/V `q4_0`.
- `--spec-type draft-mtp --spec-draft-n-max 4`.
- Sampling: temp 0,6; top-p 0,95; top-k 20; min-p 0; repeat penalty 1,0;
  presence penalty 0.
- Thinking activo y `preserve_thinking=true`.

El smoke real cargó modelo, projector y contexto MTP sin OOM. Una generación de
1200 tokens midió 40,84 tok/s y 232,48 tok/s de prompt. MTP aceptó 869 de 1319
drafts (65,9%), con longitud media aceptada 3,63.

## Benchmark agente

Suite versionada `assets/benchmarks/custom/agent_efficiency_e2e_v1.json`:
parser Python con tests, localización TypeScript/JavaScript y programa C++17 con
tests. Se ejecutaron tres pasadas por perfil con `agent-avanzado`; cada aceptación
verificó archivos y ejecutó los comandos reales.

| Perfil | Pasadas | Calidad | Primer intento | Reparaciones | Mediana | TPS mediano | TTFT mediano | Tokens medianos | VRAM mediana |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| ThinkingCap 27B Q4_K_M MTP4 | 3 | 11/11 ×3 | 11/11 ×3 | 0 | 181,22 s | 33,17 | 3529,70 ms | 3764 | 21474 MB |
| KAT Coder 2.5 Q4_K_M | 3 | 11/11 ×3 | 11/11 ×3 | 0 | 214,41 s | 27,51 | 3682,63 ms | 4201 | 18675 MB |

Tiempos individuales:

- ThinkingCap: 181,222 s; 103,615 s; 197,901 s.
- KAT Coder: 184,482 s; 297,634 s; 214,407 s.

ThinkingCap redujo 33,185 s la mediana, equivalente a 15,5% respecto de KAT. Su
TPS mediano fue 20,6% mayor y utilizó 10,4% menos tokens en la mediana. El costo
es 2799 MB más de VRAM mediana (15,0%); por eso el perfil queda limitado a 24 GB.

Esta tabla no decide el reemplazo de MAX-Q: KAT es MoE y se incluye sólo como
referencia secundaria de calidad.

## Comparación primaria dense vs dense

`llama-bench` b9763, RTX 3090, cinco repeticiones, GPU completa, FA, batch 2048,
ubatch 512, KV q4_0; prompt 512 y generación 128:

| Dense | Quant | Prompt processing | Generación |
|---|---|---:|---:|
| MAX-Q Qwen3.6-27B | IQ4_XS | 843,31 tok/s | 30,96 tok/s |
| ThinkingCap Qwen3.6-27B | Q4_K_M | 802,68 tok/s | 29,42 tok/s |

En esta medición cruda MAX-Q fue 5,1% más rápido procesando prompt y 5,2% más
rápido generando. No se activó MTP en `llama-bench`; por eso este resultado mide
el costo base de ambos dense y se complementa con el smoke MTP, no lo sustituye.
La promoción queda pendiente de repetir `Agent efficiency E2E v1` x3 con
`agent-avanzado` contra MAX-Q, misma ventana de contexto y política MTP.

## Prueba de paridad MAX-Q a 262k

Se probó ThinkingCap con los parámetros operativos de MAX-Q: contexto solicitado
262000 (slot efectivo 262144), batch 512, ubatch 64, KV K/V q4_0, GPU completa,
`mlock`, sin `mmap`, un slot y cache RAM de 32 GB. ThinkingCap conservó su MTP
self-contained `draft-mtp` n=4 porque no puede usar exactamente el esquema
MTP+NGRAM del GGUF MAX-Q.

El servidor cargó modelo, projector, MTP y el slot completo, pero ocupó
aproximadamente 24229 MiB de VRAM y dejó apenas 98 MiB libres. En la primera
petición el procesamiento del prompt cayó a 2,24 tok/s. Se interrumpió la
generación: completar la suite E2E x3 bajo esas condiciones no representa un uso
operable y habría requerido horas.

Conclusión: ThinkingCap Q4_K_M **no puede reemplazar a MAX-Q IQ4_XS conservando
262k en una RTX 3090 de 24 GB**. El perfil experimental se dejó en modo de paridad
262k sólo durante la reproducción de este límite. Para hacerlo viable había que
reducir contexto o usar una quant más pequeña; ambas opciones dejan de ser un
reemplazo equivalente de MAX-Q.

## Punto operativo a 131k

Se repitió la prueba cambiando únicamente el contexto a 131000 (slot efectivo
131072). El resto permaneció en batch 512, ubatch 64, KV q4_0, GPU completa,
visión, MTP4, `mlock`, sin `mmap` y un slot.

- VRAM en reposo: 22374 MiB; margen: 1953 MiB.
- VRAM tras generar: 22357 MiB; margen: 1970 MiB.
- Prompt real: 212,12 tok/s.
- Decode real: 50,51 tok/s.
- MTP: 232/352 drafts aceptados (65,9%), longitud media 3,64.
- Respuesta: 318 tokens en 6,59 s de servidor, finalización normal y 1338
  caracteres de contenido útil.

Con `reasoning on`, un control separado agotó 1200 tokens pensando sin producir
contenido visible. El perfil operativo usa `reasoning off`, igual que MAX-Q.

Veredicto: **ThinkingCap 131k sí es viable en 24 GB** y pasa a ser MAX-Q. Tiene
la mitad de contexto del Qwen base anterior, que queda disponible como MAX-CTX.
El instalador queda fijado en esta configuración de 131k.

Resultados originales:
`AppLocalData/LlamaCode/benchmark-runs/Agent_efficiency_E2E_v1_20260728_113248/`.

## Cambios incorporados

- El detector reconoce los GGUF oficiales `ThinkingCap-Qwen3.6-*` como MTP
  self-contained aunque el nombre no contenga el token `-MTP`.
- `sys-maxq` apunta a ThinkingCap 131k; `sys-maxctx` conserva Qwen base 262k.
- `tools/add_thinkingcap_profiles.py` permite instalar de forma idempotente el
  perfil experimental/comparativo en una configuración de desarrollo.

Fuentes:

- https://huggingface.co/bottlecapai/ThinkingCap-Qwen3.6-27B
- https://huggingface.co/bottlecapai/ThinkingCap-Qwen3.6-27B-GGUF
