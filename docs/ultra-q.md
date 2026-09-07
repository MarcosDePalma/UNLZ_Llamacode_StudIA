# ULTRA-Q · DeepSeek V4 Flash 0731

Perfil experimental para una RTX 3090 de 24 GB, Ryzen 9900X y 128 GB DDR5. No
reemplaza a MAX-Q hasta superar los benchmarks locales.

## Instalación

El perfil `sys-ultraq-dsv4-0731-iq3s` descarga manualmente los cuatro shards
`UD-IQ3_S` desde `unsloth/DeepSeek-V4-Flash-0731-GGUF`. Se recomiendan al menos
150 GB libres. Las descargas usan la cola reanudable existente y el perfil no es
companion de ningún showcase.

Requiere llama.cpp oficial b10228 o posterior. Esa release incorpora MTP/DSpark
para DeepSeek V4. LlamaCode compara el número de
build, elige el binario compatible más nuevo y conserva instaladas las builds que
usan otros perfiles.

## Configuración inicial

```text
ctx 131072 · gpuLayers 44 · batch 1024 · ubatch 512
KV q4_0 · flash-attn · mmap · parallel 1
--n-cpu-moe 39 · temp 0.60 · top-p 0.95 · top-k 20
--spec-type draft-dspark · --spec-draft-n-max 5
--spec-draft-conf-min 0.0…0.8 (opcional; validar por benchmark)
```

El perfil original se conserva sin cambios como control histórico: asume que el
cabezal DSpark quedó disponible dentro del GGUF principal. Los GGUF `UD-IQ3_S`
actuales de Unsloth no exponen esas capas como un drafter separado utilizable por
llama.cpp, por lo que activar sólo `draft-dspark` puede no acelerar la generación.
Las variantes de contexto y benchmark existentes continúan ligadas a ese perfil
para no cambiar resultados anteriores.

El perfil paralelo
`sys-ultraq-dsv4-0731-iq3s-dspark-external` reutiliza los mismos cuatro shards y
descarga además `DeepseekV4-Flash-20260731-DSpark.gguf` desde
`am17an/DeepseekV4-Flash-20260731-DSpark`. LlamaCode lo asocia como draft
obligatorio y genera:

```text
--spec-draft-model <ruta al DSpark.gguf>
--spec-type draft-dspark
--spec-draft-n-max 5
--spec-draft-ngl auto
--spec-draft-conf-min 0.0…0.8 (opcional)
```

El draft agrega aproximadamente 10,9 GB. En una RTX 3090 con el modelo principal
parcialmente en VRAM puede terminar en RAM o forzar otra distribución, de modo que
la aceleración no está garantizada. Comparar el perfil externo con ULTRA-Q y con
`[bench ULTRA-Q] ... nospec`, registrando aceptación, TG, RAM, VRAM y pagefile,
antes de promoverlo.

La configuración de sampling es deliberadamente específica para el agente de
código. DeepSeek recomienda `temperature 1.0` y `top_p 1.0` para el uso local
general de V4 Flash, mientras que su guía por casos baja coding/math a temperatura
`0.0`; el post de referencia no publica valores de sampling. ULTRA-Q usa el punto
conservador común de LlamaCode (`0.60 / 0.95 / top-k 20`, `min-p 0`, penalidades
neutras) para limitar deriva y repetición sin hacer completamente determinista una
elección de tool equivocada. El detector del agente sigue siendo la defensa
principal contra loops: el sampler no la reemplaza.

En la medición local validada con llama.cpp b10223, el proceso estabilizó su
working set en ~104,8 GB y la RTX 3090 en 20.963/24.576 MiB (85,3%). Se conserva
`--no-warmup`: con sólo ~4,1 GB de RAM física libre, un cache RAM adicional o un
prefill grande al arrancar induciría paginación. `mmap` permite que Windows conserve
las páginas del GGUF en su file cache mientras el server permanezca vivo; cerrar el
proceso destruye VRAM/KV y no existe una instantánea portable que pueda restaurarlos.
Para reinicios rápidos, mantener el servidor persistente y reutilizar
`cache_prompt=true`, ya enviado por el agente en cada iteración.

Los contextos permitidos para crear variantes son 64k, 131k, 192k, 256k y 384k.
384k es experimental: debe verificarse contra RAM comprometida, pagefile, VRAM y
tiempo de prefill. El tuner incluye `--n-cpu-moe 31/35/39/43` y conserva el gate
de calidad/perplexity existente.

## Rama DeepSeek LID CUDA

El perfil separado `sys-ultraq-dsv4-0731-lid-cuda` prueba la rama
`spencer-zaid/llama.cpp`, branch `deepseek-lid-cuda`, que incorpora Lightning
Indexer CUDA. LlamaCode la instala sólo como build-from-source y la resuelve por
el flavor `deepseek-lid-cuda`; nunca reemplaza ni contamina el binario oficial.
La receta desactiva el build npm de la Web UI, permite descargar los assets
prebuilt que el servidor igual necesita y agrega un `loading.html` mínimo si el
paquete lo omite; así no depende de que Node coincida con los engines del fork.
Mantiene `--n-cpu-moe 39`, `--fit-target 512`, `--fit-ctx 131072` y fuerza
`GGML_CUDA_NO_PINNED=1` en el proceso.

Esta rama debe usar KV f16: el test local cargó los cuatro shards y generó `OK`
con 131072 tokens en 2× RTX 3090 y 128 GB de RAM. Por eso el perfil LID no hereda
los flags q4_0 del baseline. Los presets 256k/512k/1M están disponibles para
medición, pero son hipótesis del fork y no una garantía de que entren o conserven
calidad. Para cada uno registrar carga, TTFT, PP/TG, RAM comprometida, pagefile,
VRAM y una prueba de calidad; si falla, conservar el resultado como límite y
volver al perfil oficial.

## Perfiles de benchmark

El bundle incluye doce perfiles opt-in `[bench ULTRA-Q]` que reutilizan los mismos
shards y el mismo binario. La matriz cubre batch lógico 2048/4096/8192, ubatch
512/1024/2048, DSpark n-max 1/3/5 y un control sin speculative decoding. Dos
variantes adicionales comparan `--n-cpu-moe 35/43` contra el baseline 39. Los
perfiles `stress` con ubatch 2048 pueden fallar por OOM: ese fallo es un resultado
válido y no deben usarse como configuración diaria sin medir estabilidad.

Comparar con el mismo prompt y contexto efectivo. Registrar PP tok/s, TG tok/s,
TTFT, tiempo total, aceptación DSpark, VRAM pico, RAM comprometida y pagefile. Un
batch mayor que el prompt no aporta velocidad; `batch >= ubatch` se mantiene en
toda la matriz. Ninguna variante se recomienda ni descarga automáticamente.

## Híbrido ULTRA-Q → MAX-Q

El perfil `sys-hybrid-ultraq-maxq` usa ULTRA-Q sólo para planificar y MAX-Q para
editar, ejecutar tools y verificar. El flujo es secuencial porque ambos modelos
comparten GPU:

1. Construye contexto de sólo lectura con AGENTS/README, árbol acotado y Git.
2. Detiene MAX-Q y carga ULTRA-Q.
3. Exige y valida `HybridPlan v1` JSON.
4. Guarda el plan bajo `AppLocalData/LlamaCode/hybrid-plans/<sha256>.json`.
5. Descarga ULTRA-Q, restaura MAX-Q y entrega request + plan internamente.

El transcript conserva sólo el request original. Planes vacíos, inválidos o con
schema incorrecto cancelan la ejecución. Un journal en QSettings permite volver a
seleccionar el ejecutor después de un cierre durante el hot-swap. La revisión
final con ULTRA-Q no es automática porque exigiría otro intercambio de ~116 GB.

## Validación

Antes de promover ULTRA-Q ejecutar, con tres pasadas como mínimo:

- `Agent efficiency E2E v1`;
- `Stress largo y difícil`;
- casos de tool calling encadenado;
- A/B MAX-Q, ULTRA-Q y el híbrido;
- soak de cargas, cancelaciones y reinicios.

Registrar calidad final/al primer intento, reparaciones, tokens de thinking,
TTFT, tok/s, tiempo de carga, RAM/VRAM pico, pagefile y tiempo total. Las suites
requieren que los cuatro shards y el runtime compatible estén instalados; no se
publican resultados simulados cuando esos artefactos no están presentes.
