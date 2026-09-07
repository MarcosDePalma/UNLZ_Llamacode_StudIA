# Evaluación de Ternary Bonsai 27B para LlamaCode

Fecha de la prueba: 2026-07-27.

## Conclusión

No conviene incorporar Ternary Bonsai 27B como perfil de agente/coding recomendado
para GPUs de 8 GB. Sí merece seguimiento como perfil **experimental** para chat,
razonamiento y contexto largo en GPUs de 10–12 GB, cuando su soporte CUDA llegue a
`llama.cpp` mainline o LlamaCode pueda fijar de forma segura el fork de PrismML.

MAX-Q sigue siendo la opción de máxima calidad para 24 GB. Ternary Bonsai reduce a
menos de la mitad el peso residente, pero MAX-Q con MTP fue más rápido en respuestas
reales y conserva bastante más calidad según los benchmarks del propio fabricante.
En el tier de 8 GB, Qwen3.5-9B Q4_K_M ocupa menos, genera mucho más rápido y obtuvo
mejor resultado en el benchmark agente independiente disponible.

## Qué se evaluó

- Modelo: `prism-ml/Ternary-Bonsai-27B-gguf`,
  `Ternary-Bonsai-27B-Q2_0.gguf`.
- Runtime: fork oficial de PrismML, commit `9fcaed7`, CUDA 12.4.
- Hardware local: RTX 3090 24 GB, Ryzen 7 7700, Windows, driver 610.47.
- Comparación local homogénea: CUDA, offload completo, flash attention, batch 512,
  ubatch 64, KV Q4_0, PP512 y TG128.
- Comparación de servicio: contexto 8192, un slot, reasoning apagado, cinco tareas
  cortas de calidad de LlamaCode y una llamada nativa a herramienta.

Los pesos y el runtime quedaron fuera del repo en
`D:\Models\llamacpp\Bonsai-demo`.

## Resultados locales

| Modelo | GGUF | PP512 | TG128 sin especulación | Servicio real | VRAM total observada, ctx 8K | Calidad corta |
|---|---:|---:|---:|---:|---:|---:|
| Ternary Bonsai 27B Q2_0 | 7.15 GB | 806.1 t/s | 53.1 t/s | 42–48 t/s | 8.9 GB | 5/5 + tool call válido |
| MAX-Q Qwen3.6-27B IQ4_XS | 15.67 GB | 887.4 t/s | 34.8 t/s | 63–93 t/s con MTP | 17.8 GB | 5/5 + tool call válido |
| Qwen3.5-9B Q4_K_M | 5.67 GB | 2246.3 t/s | 94.2 t/s | no medido | ~6 GB esperado | ver benchmark agente |
| Gemma 4 E4B Q4_K_M | 4.96 GB | 3205.0 t/s | 111.0 t/s | no medido | ~5–6 GB esperado | no comparable |
| Granite 4.1 8B Q4_K_M | 5.34 GB | 2344.6 t/s | 95.0 t/s | no medido | ~6 GB esperado | no comparable |

La VRAM es el total informado por `nvidia-smi`, incluyendo unos 1.2 GB del
escritorio y otros procesos. Por eso sirve para decidir si entra en una placa,
pero no debe interpretarse como memoria exclusiva exacta del modelo.

El TG128 aislado no incluye el MTP de MAX-Q. Al medir la configuración de servicio
real, MAX-Q aceptó casi todos los drafts MTP y superó a Bonsai. El resultado evita
la conclusión engañosa de que el GGUF ternario es más rápido que el perfil MAX-Q.

Las cinco pruebas cortas son deliberadamente básicas: primalidad, aritmética,
refactor de una línea, silogismo y JSON estricto. Los dos modelos aprobaron todas,
por lo que este resultado sólo confirma compatibilidad básica y no equivalencia de
calidad para trabajo agente prolongado.

### Tres benchmarks propios de LlamaCode

Se ejecutaron los tres primeros casos de `Suite rápida · Python coding`
(`f779d1cb-0a6f-4c31-ba9a-54b640209b9b`) con tres pasadas por modelo:

- `mini_calc_lang`
- `expense_tracker`
- `log_analyzer`

Se usaron los prompts y criterios de aceptación versionados de la suite. Cada
respuesta se guardó como el archivo solicitado y sólo contó como éxito cuando
aprobó `python -m py_compile` y su `--self-test` terminó en menos de 30 segundos,
imprimió `SELF_TEST_OK` y salió con código 0. Ambos modelos usaron el mismo runtime,
contexto 16K, sampling conservador y las mismas tres semillas. Esta corrida ejercitó
la generación directa de los artefactos; no incluyó el loop de reparación por tools
del benchmark agente E2E.

| Modelo | Éxitos | Compilan | Mediana pared | Mediana generación |
|---|---:|---:|---:|---:|
| Ternary Bonsai 27B Q2_0 | 0/9 | 9/9 | 102.16 s | 28.90 t/s |
| Qwen3.5-9B Q4_K_M | 1/9 | 8/9 | 80.49 s | 47.85 t/s |

Qwen3.5-9B aprobó una pasada de `expense_tracker`; las otras dos fallaron la
importación CSV. Ningún modelo aprobó `mini_calc_lang` ni `log_analyzer`. Bonsai
produjo siempre Python sintácticamente válido, pero presentó timeouts, errores de
parser, uso incorrecto de `argparse`, duplicación de IDs y un `NameError` dentro
de sus self-tests. El 9B también tuvo errores de parser/CLI y una salida con Markdown
inválido.

El resultado favorece al 9B, pero sobre todo muestra que ninguno es suficientemente
estable en generación directa de programas largos. No justifica reemplazar el perfil
8 GB por Bonsai. La siguiente comparación útil debe usar `Agent efficiency E2E v1`
con tools y reparación habilitados, una vez que Bonsai pueda registrarse con un
runtime CUDA soportado por LlamaCode.

## Calidad publicada

El whitepaper de PrismML informa, sobre 15 benchmarks en thinking mode:

| Variante | Footprint publicado | Promedio | Coding | Tool/agent |
|---|---:|---:|---:|---:|
| Qwen3.6-27B FP16 | 54 GB | 85.07 | 88.74 | 80.00 |
| Qwen3.6-27B Q4_K_XL | 17.6 GB | 84.99 | no desglosado en la tabla resumida | no desglosado |
| Ternary Bonsai 27B | 5.9 GB ideal / 7.2 GB desplegado | 80.49 | 85.96 | 74.01 |
| Bonsai 27B 1-bit | 3.9 GB | 76.11 | 81.88 | 66.03 |

Así, Ternary retiene 94.6% del promedio FP16, pero pierde aproximadamente 5.8% en
coding y 7.5% en tool/agent frente al baseline. El dato de 5.9 GB es ideal; el
GGUF ejecutable actual ocupa aproximadamente 7.2 GB porque usa slots físicos de
dos bits.

Como contrapeso independiente, una corrida comunitaria de Terminal-Bench 2.0
(89 tareas, RTX 5070 Laptop 8 GB) obtuvo:

- Ternary Bonsai 27B: 7.9%.
- Qwen3.5-9B: 9.2%.
- Qwen3.6-35B-A3B: 24.3%.
- VRAM a 16K con KV Q4_0: 7.29 GB para Ternary frente a 5.68 GB para Qwen3.5-9B.
- Decode: 22.6 t/s para Ternary en ese equipo.

Es una sola prueba comunitaria y no debe generalizarse como verdad definitiva,
pero usa exactamente el tipo de workflow agente/coding que importa a LlamaCode y
contradice la idea de que “27B comprimido” supera automáticamente a un 9B sano.

## Compatibilidad y riesgos

- El Q2_0 g128 usado por el release necesita hoy el fork de PrismML para CUDA.
- En `llama.cpp` mainline, CPU, Metal y Vulkan ya tienen soporte; el PR de CUDA
  seguía abierto al 2026-07-27.
- PrismML también publica un Q2_0 g64 compatible con mainline, pero ocupa más y
  CUDA mainline todavía depende del PR pendiente.
- El setup oficial de Windows no detectó el driver 610.47 porque busca
  `CUDA Version` y `nvidia-smi` ahora imprime `CUDA UMD Version`; descargó Vulkan
  por error. La prueba CUDA requirió bajar manualmente el asset 12.4.
- Agregar un perfil de sistema ahora obligaría a mantener otro pin de binario y
  dos formatos Q2_0 transitorios. No es un costo razonable para un perfil default.
- El fabricante reconoce que el ajuste específico para agentic coding todavía
  está en su roadmap.

## Recomendación por VRAM

| VRAM | Recomendación |
|---|---|
| 4–6 GB | Mantener Qwen3.5-4B / Gemma 4 E4B; Bonsai 27B no entra con margen útil. |
| 8 GB | Mantener Qwen3.5-9B Q4_K_M como agente recomendado. Ternary entra demasiado justo y no mejora el benchmark agente disponible. |
| 10–12 GB | Posible perfil experimental de chat/razonamiento, ctx 16K–32K, KV Q4_0, sin visión ni DSpark por defecto. |
| 16 GB | Preferir Qwen3.6-35B-A3B para coding; considerar Ternary sólo si se priorizan contexto y residencia simultánea de otras cargas. |
| 20–24 GB | Mantener Qwen3.6-27B IQ4_XS MTP / MAX-Q para calidad y velocidad agente. |

## Condiciones para reconsiderar un perfil

1. CUDA Q2_0 integrado en `llama.cpp` mainline y disponible en los binarios que
   instala LlamaCode.
2. Al menos tres corridas de Agent efficiency E2E v1 contra
   `sys-vram-8-qwen-agent`, con igual contexto, presupuesto y tools.
3. Calidad no inferior a 95% del Qwen3.5-9B en éxito de tareas, sin loops ni
   degradación de tool calls.
4. VRAM pico menor a 7.5 GB en 8 GB o, si no, clasificar el perfil como 10/12 GB.
5. Medición separada con DSpark: agrega ~1.95 GB y puede acelerar decode, pero
   elimina el principal margen de memoria del candidato.

## Fuentes

- PrismML, repositorio y estado de runtimes:
  https://github.com/PrismML-Eng/Bonsai-demo
- Modelo GGUF:
  https://huggingface.co/prism-ml/Ternary-Bonsai-27B-gguf
- Whitepaper:
  https://github.com/PrismML-Eng/Bonsai-demo/blob/main/bonsai-27b-whitepaper.pdf
- Benchmarks comunitarios de velocidad:
  https://github.com/PrismML-Eng/Bonsai-demo/tree/main/community-benchmarks
- PR CUDA Q2_0 de `llama.cpp`:
  https://github.com/ggml-org/llama.cpp/pull/25707
- Terminal-Bench 2.0 comunitario:
  https://www.reddit.com/r/LocalLLaMA/comments/1v1ya97/
