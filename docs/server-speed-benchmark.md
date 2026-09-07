# Server Speed Benchmark v1

LlamaCode separa la medición del servidor de la calidad E2E del agente. `Server
Speed v1` usa un corpus JSON versionado (`assets/benchmarks/server_speed_v1.json`)
con categorías de chat, prosa, razonamiento, matemática, código, JSON,
resumen y edición de archivos.

## Qué mide

- **PP** (`prompt_tps`), **TG** (`decodeTps`), TTFT, latencia total e ITL.
- Media, mediana, IQR y percentiles. Las colas P95/P99 de requests no se
  publican cuando no hay suficientes observaciones para evitar precisión falsa;
  ITL puede acumular más observaciones por tokens.
- Dos fases por ítem: `cold` usa nonce y `cache_prompt=false`; `warm` conserva
  el prompt para medir el camino cacheado. El seed y los hashes de prompt quedan
  en cada muestra.
- Barrido de prefill desde 2K hasta 64K tokens (limitado por la UI y el
  contexto real del perfil). Un límite de contexto se registra como
  `skipped-context`, no como throughput cero.
- Barrido de concurrencia con olas de clientes contra un único `llama-server`.
  Cada punto conserva sus requests, errores, TTFT y throughput agregado.

## Comparación A/B

El modo A/B ejecuta un solo servidor a la vez, alterna el orden `AB`/`BA`, usa
el mismo prompt, nonce y seed para cada par, y guarda deltas pareados con IC
95% y ganador sólo cuando el intervalo excluye cero. También ejecuta un control
**A/A**; si ese control detecta una diferencia significativa, el resultado se
marca como no confiable.

Cada corrida deja `metadata.json`, `comparison.json` y el resultado completo en
`AppLocalData/LlamaCode/benchmark-runs/<run>/`. El metadata incluye hash del
corpus, fingerprints de perfiles, hardware, flags de sampling, warmups,
concurrencia y límites de prefill, para que una captura sea comparable y no
oculte sus condiciones.

Las cifras del servidor no sustituyen un benchmark de calidad. Para decidir
entre perfiles conviene mirar PP, TG, TTFT, ITL, estabilidad, VRAM por GPU y la
calidad E2E por separado.
