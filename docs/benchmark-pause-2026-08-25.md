# Pausa de campaña de benchmarks — 2026-08-25

## Estado de la pausa

- La campaña post-corrección fue cancelada de forma segura a las 11:36:43.
- El daemon confirmó `benchmarkRunning=false` y estado `Cancelado.`.
- El runner externo `tools/run-benchmark-post-correction.ps1` fue detenido después de que el daemon finalizara el benchmark actual, para evitar que iniciara otro perfil.
- `llama-server` quedó detenido y la VRAM fue liberada: GPU 0 quedó en aproximadamente 582 MiB y GPU 1 en aproximadamente 489 MiB.
- LlamaCode/ControlApi quedó activo; no se cerró la aplicación.

## Punto exacto

- La corrida interrumpida era el perfil `[bench antirez] 32k · B4096 · U512 · KV q8_0`.
- Estaba en HE20, prompt 2/20, cuando se solicitó la cancelación.
- El último registro fue:
  `2026-08-25 11:36:43 ... he20 · progreso=100 · Cancelado.`
- Antes de esa corrida, el perfil #49 (`[bench antirez stress] 131k · B4096 · U1024 · KV q4_0`) había terminado HE0/HE20 válidos, pero dejó BCB pendiente.

## Estado consolidado conocido

- 22 perfiles cerrados técnicamente.
- 27 perfiles incompletos o con alguna etapa pendiente hasta el perfil #49.
- El perfil #50 quedó interrumpido durante HE20.
- La campaña había expandido el catálogo a 83 perfiles `benchmark=true`.
- El estado de cobertura persistente del daemon es la fuente para continuar; el archivo `.resume.json` se elimina al cancelar de forma normal, por lo que la reanudación debe volver a consultar `benchmarkCoverage`.

## Artefactos

- Log: `C:\Users\cristian\AppData\Local\LlamaCode\LlamaCode\benchmark-campaign-post-correction.log`
- Resultados: `C:\Users\cristian\AppData\Local\LlamaCode\LlamaCode\benchmark-runs\`
- Runner: `tools/run-benchmark-post-correction.ps1`

## Reanudación

Ejecutar nuevamente el runner desde el repositorio. El runner consulta la cobertura persistente y sólo debe iniciar la siguiente etapa pendiente de cada perfil. Antes de continuar hay que corregir la política de BCB pendiente/timeout para que no vuelva a consumir horas sin reencolar o aislar automáticamente los Antirez.
