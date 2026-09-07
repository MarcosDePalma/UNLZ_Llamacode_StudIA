# Benchmark de workloads auxiliares

`qa_auxiliary_concurrency` es un probe opt-in para medir si un servidor local
tolera concurrencia útil para embeddings, juez, resumen u otras tareas pequeñas.
No forma parte de `ctest`: necesita un `llama-server` real y el operador decide
el modelo, el contexto y la carga.

Compilar con `tests.bat Debug` y ejecutar, por ejemplo:

```powershell
build_tests\Debug\qa_auxiliary_concurrency.exe http://127.0.0.1:8080 `
  --concurrency 1,2,4,8 --requests 8 --prompt-tokens 256 `
  --n-predict 32 --output auxiliary-concurrency.json
```

El servidor debe exponer `/completion` y conviene iniciarlo con `--metrics` para
obtener `promptTps` y `decodeTps`. El resultado conserva cada batch y muestra:

- latencia p50 y distribución de cada batch;
- throughput agregado de prefill y decode;
- requests por segundo observados por el cliente;
- errores, timeouts y respuestas sin timings.

## Conexión con el agente

El sidecar no se activa automáticamente. En **Ajustes → Modelo auxiliar · RAG**
se puede indicar su Base URL (sin el sufijo `/v1`) y los nombres de los modelos de embeddings/rerank.
El chat, las tools y los slots del modelo principal siguen usando el servidor
activo; sólo `/v1/embeddings` y `/rerank` van al sidecar. También se aceptan las
variables `LLAMACODE_AUXILIARY_URL`, `LLAMACODE_AUXILIARY_EMBED_MODEL`,
`LLAMACODE_AUXILIARY_RERANK_MODEL` y `LLAMACODE_AUXILIARY_KEY`.

Para un sidecar CPU conviene empezar con contexto corto, `--n-gpu-layers 0`,
`--threads N`, `--batch-size 256`, `--ubatch-size 128` y el quant que entre en
RAM. La concurrencia debe elegirse con el probe: más requests puede aumentar el
throughput agregado, pero también disparar la latencia del agente. Si la misma
máquina hospeda el modelo principal, reservar núcleos para cada proceso es una
decisión de despliegue y debe medirse con la carga real.

Interpretación mínima:

- comparar `concurrency=1` contra `2/4/8`, no sólo el máximo;
- si sube el throughput agregado pero la latencia p50 se dispara, reservar ese
  nivel para background y mantener el agente interactivo en un slot;
- repetir con un prompt corto y otro de 1k–4k tokens: el primero observa decode,
  el segundo también expone presión de prefill/memoria;
- repetir con el modelo principal y con el auxiliar. Sólo promover un perfil
  auxiliar si conserva calidad y libera tiempo o VRAM del modelo principal.

El probe no cambia perfiles, no lanza procesos, no descarga modelos y no decide
automáticamente un ganador.

## Registro de disponibilidad — 2026-08-27

Se verificó `qa_auxiliary_concurrency --help` y su contrato de uso. No se
ejecutó contra un servidor real: en la revisión apareció un `llama-server`
preexistente de otra instalación en `127.0.0.1:8033`, y esta prueba enviaría
carga `/completion` sobre la sesión ajena. No se lo interrumpió, no se lanzó un
segundo servidor y no se produjo un resultado de concurrencia que pudiera
confundirse con una medición del perfil auxiliar.
