# Benchmark externo Qwen3.8 DFlash2 con vLLM

El bundle incluye dos candidatos externos para el artefacto
`lued/Qwen3.8-27B-INT8-W8A16-DFlash2`:

| ID | Endpoint | Configuración | Uso |
|---|---|---|---|
| `sys-bench-qwen38-dflash2-vllm-262k` | `http://127.0.0.1:8000` | DFlash2, drafter `lued/Qwen3.8-27B-DFlash2-W8`, 7 tokens, TP=2, KV `fp8_e4m3`, 262144 | candidata exacta de la receta |
| `sys-bench-qwen38-dflash2-vllm-ar-262k` | `http://127.0.0.1:8001` | mismo target, sin speculative decoding, TP=2, KV `fp8_e4m3`, 262144 | control autoregresivo |

Son perfiles `BackendProfile.kind=cloud` aunque el endpoint sea loopback. LlamaCode
no descarga safetensors, no instala los parches de vLLM y no inicia el contenedor:
el operador debe levantar cada servidor externo y luego seleccionar el perfil.
El bundle conserva la configuración declarativa para que la huella del benchmark
identifique target, drafter, método especulativo, contexto, KV y TP.

La receta DFlash2 del modelo usa el target y drafter publicados por `lued`,
`--tensor-parallel-size 2`, `--max-model-len 262144`,
`--kv-cache-dtype fp8_e4m3` y el método `dflash` con 7 tokens especulativos.
Requiere el soporte/parches DFlash2 de `noonghunna/club-3090` según el model card.

Para una comparación válida:

1. Levantar el candidato y el control en puertos separados, con la misma suite,
   seed, prompt y perfil de agente.
2. Ejecutar HE0 en ambos; sólo continuar a HE20 y BCB si el transporte y el
   grader son válidos.
3. Registrar versión exacta de vLLM, revisión de los parches, hash/revisión de
   ambos modelos, aceptación de draft, TTFT, prefill, decode y VRAM por GPU.
4. No comparar estas filas con perfiles `llama-server` como si fueran el mismo
   backend: sirven para medir el eje vLLM/DFlash2 y el control local por separado.

Si el endpoint no está disponible, la fila queda pendiente de HE0; no se inventa
un resultado de velocidad a partir del model card.
