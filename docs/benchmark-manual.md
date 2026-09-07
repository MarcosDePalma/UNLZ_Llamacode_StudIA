# Manual de benchmarking de modelos locales

## Objetivo

Este manual define el procedimiento oficial para comparar una combinación de:

- modelo y archivo GGUF;
- binario/runtime de `llama-server`;
- perfil de lanzamiento de LlamaCode;
- harness y perfil de agente;
- suite de evaluación.

El objetivo no es encontrar un ganador universal. Es determinar si una candidata
supera a las existentes en su caso de uso: `FAST`, `BALANCED` o `QUALITY`.

Una comparación sólo es válida cuando se mantiene constante el resto de la
cadena. Si cambia el modelo, el binario, el perfil o el harness, se debe tratar
como una nueva candidata y repetir el protocolo completo.

## Archivos vivos de resultados

La tabla operativa se mantiene en [`benchmark-results.md`](benchmark-results.md).
La historia de corridas, descubrimientos, fallos y decisiones se conserva en
[`benchmark-results-history.md`](benchmark-results-history.md). Toda mejora debe
actualizar ambos archivos sin borrar los resultados históricos.

El detalle fila por fila de perfiles ejecutados, descartados, bloqueos de
infraestructura, quantizaciones, binarios, huellas y métricas se conserva en el
[registro detallado de perfiles](benchmark-profile-ledger-2026-08.md). Ese
registro debe crecer por adición: no se borran los intentos anteriores ni se
convierte una falla de transporte en cero de calidad.

El ranking por caso de uso, la lectura de los perfiles Qwen3.8/DFlash2/Ling y el
catálogo de mejoras de las interfaces se consolidan en
[`benchmark-ranking-and-use-cases.md`](benchmark-ranking-and-use-cases.md).

## Procedimiento de validación y pausa segura

Para retomar una tanda de cambios en el planificador de memoria o los harness:

1. Leer `AGENTS.md` y `README.md`; comprobar el estado de Git y preservar los
   cambios ajenos.
2. Compilar el candidato Debug con `build.bat Debug NOPAUSE` y comprobar que se
   actualizó `build/Debug/LlamaCode.exe`.
3. Ejecutar `tests.bat Debug`; la suite debe terminar con todos los tests en
   verde (`69/69` en la configuración actual). Si se modifica C++/QML/core, no
   se salta esta etapa.
4. Arrancar el Debug, comprobar la API de control y verificar que
   `benchmarkRunning=false` antes de iniciar una cola.
5. Ejecutar un único perfil por vez, en orden `HE0 → HE20 → BCB`. Registrar para
   cada intento el comando efectivo, `fit-target`, OOM, tiempo de carga,
   `vramGpu0Mb`, `vramGpu1Mb`, capacidad/libre por GPU, RAM total/libre, TPS,
   TTFT, tiempo total y calidad. Un OOM de carga dispara la escalera adaptativa;
   no se anota como fracaso de calidad.
6. Mantener el runner serial activo y detenerlo sólo después de cancelar el
   benchmark por la API y confirmar que el servidor desapareció. Nunca lanzar
   otra tanda mientras `benchmarkRunning=true`.

Una pausa segura deja la API sin benchmark, sin `llama-server` residual y sin
runner activo. Al continuar, se retoma desde `benchmarkCoverage.nextStage`; no
se reinician etapas ya cerradas ni se convierten intentos interrumpidos en
resultados válidos.

### Campaña post-corrección serial

La campaña reproducible de los perfiles `benchmark=true` activos se puede
orquestar con:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\tools\run-benchmark-post-correction.ps1
```

El runner obtiene los perfiles del catálogo, ejecuta un solo perfil por vez y
consulta `benchmarkCoverage` después de cada etapa. Sólo reintenta un resultado
`infra-timeout`; un resultado `blocked` detiene ese perfil y no se convierte en
calidad cero. El registro operativo queda en
`%LOCALAPPDATA%\LlamaCode\LlamaCode\benchmark-campaign-post-correction.log`.

## Alcance de ejecución

Por defecto, sólo se benchmarkean perfiles marcados `⚡ BEST` en la tabla viva.
Los perfiles marcados `🏆 BENCH` son candidatos explícitos para una tanda de
investigación. Desde **Perfiles**, la acción **Para benchmark** deja esa marca
persistida mientras se sigue armando el catálogo; luego, en Benchmark,
**Seleccionar 🏆 benchmark (N)** carga todos los candidatos pendientes de una vez.
También aparecen en el selector automático de HE20 y se pueden incluir
en la escalera HE0 → HE20 → BCB. Los perfiles
sin ninguno de esos indicadores quedan archivados como referencia y no se
ejecutan nuevamente. La promoción a `⚡ BEST` sigue requiriendo resultados y
motivo registrados en el historial.

El catálogo bundleado deja pre-marcadas como `🏆 BENCH` las candidatas explícitas
de la matriz de perfiles, incluidas las variantes que todavía están pendientes de
HE0. Se pueden desmarcar individualmente si una tanda no debe incluirlas.

Toda tabla o exportación resumida debe conservar estas columnas, en este orden:
`ID | Perfil | Agente | HE0 | HE20 | BCB | Tiempo HE0 | Tiempo HE20 |
Tiempo BCB | TPS HE0 | TPS HE20 | TPS BCB | Visión | Drafter | Quant |
Parámetros | Contexto | Thinking | Harness | Estado`.

## Regla principal: la escalera de validación

Cada candidata debe avanzar por esta secuencia:

```text
LlamaCode app/configuración
        ↓
arranque headless + servidor
        ↓
harness/agente
        ↓
HE0 — smoketest de funcionamiento
        ↓
HE20 — primer test de calidad comparable (sólo si HE0=1/1 válido)
        ↓
BCB/8 — test final de casos difíciles (sólo si HE0 y HE20 son válidos)
        ↓
decisión FAST / BALANCED / QUALITY
```

HE0 es una compuerta dura por perfil y configuración efectiva. Si falla, hay
que investigar a fondo la causa raíz, conservar logs/JSON, corregir o aislar
un único cambio y repetir HE0 desde un workspace limpio. No se ejecuta HE20 ni
BCB de ese perfil mientras HE0 no sea válido; tampoco se usa BCB para rescatar
una candidata con fallas de transporte, arranque o persistencia.

El controlador persiste una huella SHA-256 del comando efectivo junto con cada
resultado, además de `vramMb` (VRAM agregada) y `ramMb`. La huella debe coincidir al habilitar la etapa siguiente; cambiar
modelo, contexto, KV, MTP, binario, tensor-split, harness o cualquier flag
obliga a repetir HE0 aunque exista un resultado histórico `1/1`.

## Escalera adaptativa de VRAM

Durante una corrida de benchmark, LlamaCode aplica una política de memoria
adaptativa a cada perfil. El primer arranque intenta la mayor ocupación útil
posible: habilita `--fit` cuando el binario lo declara, baja el margen de
`--fit-target` y libera los flags explícitos de colocación (`--n-gpu-layers`,
`--n-gpu-layers-draft`, `--tensor-split` y overrides de expertos). Así
`llama.cpp` puede medir ambas GPUs y elegir por sí mismo el límite de capas y
el reparto VRAM/RAM según los pesos reales, incluidos los expertos MoE.

Si `llama-server` informa OOM durante la carga, no se registra inmediatamente
como fallo del modelo. Se limpia el proceso y se avanza por una escalera de
margen creciente; se eliminan los anclajes CPU/GPU de expertos y los flags de
colocación para que `fit` decida el reparto entre las dos VRAM y la RAM y, como
último recurso, se reduce gradualmente contexto, batch y ubatch. La escalera se
detiene al encontrar una carga estable o al agotar sus intentos. Los flags no soportados por el binario
se eliminan antes de iniciar para no convertir una adaptación en un error de
compatibilidad.

Cada resultado guarda `benchmarkMemoryPolicy`, `benchmarkMemoryAttempt`,
`benchmarkMemoryFitTargetMiB` y `benchmarkEffectiveArgs`. Así, dos filas del
mismo perfil se pueden comparar sabiendo si una necesitó offload o reducción de
contexto. Esta política sólo se aplica al ciclo de benchmark: iniciar un
perfil manualmente conserva exactamente los argumentos declarados en el
catálogo.

## Qué debe quedar congelado

Antes de comparar una candidata con un perfil existente, registrar y conservar:

1. Máquina: GPU, VRAM, RAM, driver, versión de Windows, límite de potencia y
   condiciones térmicas relevantes.
2. Código: commit de LlamaCode, ejecutable usado y versión/build del binario de
   `llama-server`.
3. Modelo: ruta, nombre exacto, tamaño, hash si está disponible, quant,
   `mmproj`, drafter/MTP y modalidades habilitadas.
4. Perfil: contexto, batch, ubatch, threads, GPU layers, parallel slots,
   tensor split, overrides, CPU-MoE, KV cache, flash attention, mmap/mlock,
   cont-batching y todos los flags efectivos.
5. Sampling: `temp`, `top-p`, `top-k`, `min-p`, repeat/presence penalty y
   `n-predict`.
6. Harness: suite, versión, perfil de agente, herramientas, formato de
   salida, grader y reglas de limpieza.
7. Ejecución: fecha/hora, timeout, cantidad de pasadas, seed si aplica y
   identificador de la corrida.

No se deben comparar TPS, tiempo o calidad de suites distintas. Tampoco se
deben comparar filas tomadas con un harness diferente sin marcar el cambio.

## Política de cuantización

Para las candidatas de este catálogo rige una cota explícita:

- el KV cache K/V debe ser `q8_0` o menor (`q8_0`, `q6`, `q5`, `q4`, etc.);
- el quant de los pesos del modelo principal debe ser `Q8` o menor;
- no se ofrecen variantes de modelo o KV `f16`, `bf16` ni superiores a `Q8`;
- los `mmproj`/projectors auxiliares `F16` o `BF16` de perfiles multimodales no
  cuentan como quant de los pesos ni como KV: se conservan cuando son necesarios
  para texto + imagen, audio u otra modalidad.

Esta regla es de comparabilidad y de presupuesto de memoria, no una afirmación
de que `f16` siempre sea peor. Si un resultado histórico fue obtenido con KV
`f16`, queda archivado como antecedente y debe repetirse con el perfil limitado a
`q8_0` antes de usarlo para elegir un ganador.

Al crear o duplicar un perfil:

1. Verificar el quant del GGUF principal, no sólo el nombre visible del perfil.
2. Verificar `runtime.kv` y los argumentos efectivos
   `--cache-type-k/--cache-type-v`.
3. Verificar también el KV del draft si hay speculative decoding.
4. Si aparece `f16`, `bf16` o un quant superior a `Q8`, bajar a `q8_0` o a un
   valor menor, documentar el cambio y volver a ejecutar HE0 → HE20 → BCB.

El `mmproj` puede seguir siendo F16/BF16 como excepción auxiliar; debe anotarse
en la configuración para que no se confunda con el quant del modelo principal.

### Edición segura de perfiles

Los perfiles se editan con LlamaCode completamente cerrada. La aplicación puede
volver a guardar el catálogo al cerrarse y sobrescribir una edición realizada
mientras estaba abierta.

Procedimiento:

1. Cerrar la aplicación y confirmar que no quedan procesos de LlamaCode ni
   `llama-server` de una corrida anterior.
2. Duplicar el perfil existente; no modificar el histórico que sirve de control.
3. Cambiar sólo los parámetros que se quieren estudiar y documentar el motivo.
4. Abrir la aplicación o el daemon headless.
5. Verificar el comando efectivo en la vista previa y en `server.log`.
6. Ejecutar HE0 antes de iniciar cualquier serie larga.

Una candidata debe conservar el nombre del perfil base más un sufijo que
identifique el cambio, por ejemplo `BALANCE - BigBang MTP - KVq8 candidato`.

Las variantes Qwen3.8 derivadas del control público se marcan `benchmark=true` y
se ejecutan como perfiles independientes en la escalera HE0 → HE20 → BCB. Las
variantes `tensor` y `mmproj en RAM` requieren comparar también la huella efectiva
del comando; `cache warm` sólo se compara contra otra corrida con prompt cache
habilitado y no se mezcla con resultados cold-cache. El perfil Q8 de 48 GB es un
candidato de calidad separado: usa el GGUF Uncensored publicado, KV `q8_0`,
sampling conservador y omite `mlock`/`no-mmap` para conservar reproducibilidad en
Windows.

### Auxiliar Ling 3.0 Tiny + Qwen3.8

La referencia de Ling propone un modelo pequeño para tareas auxiliares y un
modelo más capaz para las decisiones críticas. El bundle traduce esa hipótesis a
tres perfiles: `sys-ling30-tiny-q6-131k` (Q6/KV q8, thinking off),
`sys-bench-ling30-tiny-udq4-64k` (baja memoria) y
`sys-hybrid-ling30-qwen38` (Ling planifica → Qwen3.8 ejecuta). También deja A/B
de Q6 a 64k, thinking on y KV q4. Todos son `benchmark=true`, `extra=true` y
`autoCompanion=false`: no se descargan por elegir otro perfil premium.

El Q6 y el UD-Q4 provienen del repositorio GGUF comunitario de
[`bloomer010/Ling-3.0-tiny-GGUF`](https://huggingface.co/bloomer010/Ling-3.0-tiny-GGUF),
basado en el modelo oficial de
[`inclusionAI/Ling-3.0-tiny`](https://huggingface.co/inclusionAI/Ling-3.0-tiny).
La arquitectura requiere una build de llama.cpp con BailingMoE3; el mínimo
conservador del catálogo es b10331+. Verificar el binario efectivo y el chat
template antes de interpretar una corrida.

La evaluación debe separar dos preguntas:

1. **Auxiliar aislado:** HE0 de carga y una suite de resúmenes/compresión que
   mida fidelidad semántica, omisiones, tokens de salida, TTFT, decode, VRAM y
   wall-time. Comparar Q6 thinking off contra thinking on, Q6/KV q4 y UD-Q4; no
   usar un score de substring como sustituto de fidelidad.
2. **Híbrido:** repetir exactamente la suite agentica del control
   `sys-qwen38-27b-udq4-131k` y comparar contra `sys-hybrid-ling30-qwen38`.
   Registrar por separado planificación, ejecución, tiempo total, primer intento,
   reparaciones y calidad final. La fase Ling no usa tools: este experimento mide
   el planificador liviano existente, no un router automático de compresión.

No hay resultados Ling en la matriz todavía: el GGUF no estaba descargado en la
máquina al registrar los perfiles. Hasta ejecutar HE0, los valores deben quedar
como `Pendiente` y no como velocidad estimada por el model card.

### Experimento Browser Agent + Qwen3.8

El hallazgo externo de Qwen3.8 usa el proyecto [`visnia-ai/browser-agent`](https://github.com/visnia-ai/browser-agent)
con Chrome real y tareas de BU Bench v1/BrowseWebApp Bench. LlamaCode no
trata HE0/HE20/BCB como sustitutos de esas suites: sus nuevos perfiles sólo
reproducen el eje local del experimento, es decir, mismo GGUF/runtime y un
harness nativo `agent-browser` con `core + web + browser`.

La matriz incorpora cuatro variantes `sys-bench-qwen38-udq4-browser-agent-*`
con thinking `off`, `low`, `medium` y `xhigh`. `agentProfileId` es un override
de variante, por lo que no se duplica el modelo ni se altera el control
`BALANCE - Qwen3.8 UD-Q4 visión`. Para resultados de navegación, registrar
además navegador, URL/task set, autenticación, número de pasos, éxito por tarea,
tiempo total y evidencia; un HE0 válido sólo demuestra que el servidor y el
harness local arrancan.

El post de `syv-ai/qwen38-27b-rtx3090` aporta un segundo eje, de inferencia y no
de harness: su DFlash2 requiere vLLM 0.27.1 parcheado, un drafter W4A16 externo,
fp8 KV y kernels específicos de Ampere. Por eso no se lo etiqueta como
compatible con `llama-server`. La matriz agrega tres aproximaciones que sí
pueden arrancar en el stack actual de 24 GB: 64k + MTP4 con mmproj en RAM,
`ngram-mod` como lookup local y prompt-cache 64k. La primera medición debe ser
HE0; luego se comparan por separado cold-cache/warm-cache y no se atribuye el
resultado a DFlash2.

### Experimento Qwen3.8-27B en 16 GB

El post de LocalLLaMA adjunto propone una RTX 4080 de 16 GB con `RVN-IQ3_XXS`,
KV `q5_1`, ngram `4/8/32`, contexto 131k para ngram-only y 105k para DFlash2 o
MTP. El bundle lo traduce a tres candidatos independientes:
`sys-bench-16-qwen38-rvn-iq3xxs-ngram-131k`,
`sys-bench-16-qwen38-rvn-iq3xxs-dflash2-ngram-105k` y
`sys-bench-16-qwen38-rvn-iq3xxs-mtp-ngram-105k`.

Son perfiles de benchmark, no recomendaciones: el post usa una build
`dflash2` y `froggeric_fix_qwen38.jinja`, mientras que LlamaCode usa el engine
`beellama` catalogado y `qwen38-tools-fixed.jinja`. La variante DFlash2 declara
el drafter separado como dependencia; la variante MTP lleva `manualOnly=true`
porque el autor describe una fusión manual del GGUF. Colocar ese archivo en la
carpeta de modelos del perfil antes de aceptarlo; LlamaCode no intentará
descargarlo. `reasoningEffort=medium` queda en el LaunchProfile y sólo tiene
efecto cuando el thinking del benchmark está habilitado. Ejecutar HE0 aislado,
registrar la build/fork real y no comparar sus TPS con los del post sin anotar
backend, template, quant, KV y contexto.

### Controles 24 GB: Q4/Q6 y throughput honesto

El reporte de RTX 6000 aporta una receta útil para el piso de 24 GB: Q4_K_M
(`15,40 GiB`) y Q6_K (`20,56 GiB`), `-ngl 99`, Flash Attention, B512/U512,
contexto 32k y un solo slot. El catálogo la expresa como dos controles
texto-only, `sys-bench-qwen38-q4km-24gb-tg128` y
`sys-bench-qwen38-q6k-24gb-tg128`, sin MTP, ngram, prompt-cache ni mmproj.
El dato de `24,36 tok/s` del post queda como referencia externa: no se copia a
los resultados locales.

La comparación primaria es `llama-bench tg128` o un HE0 con la misma huella,
registrando quant, VRAM pico, si todos los layers quedaron en GPU y la versión
de `llama.cpp`. Q8 no se agrega como perfil ejecutable de 24 GB porque el
reporte indica que deja de entrar con `-ngl 99`; un OOM por offload no debe
convertirse en una cifra de rendimiento.

Cada control trae además una variante ngram y otra de prefix-cache warm para
diagnóstico. Sus resultados se guardan en grupos separados: un hit de ngram o
un prefijo reutilizado puede producir una tasa espectacular sin representar el
decode autoregresivo de una solicitud nueva. Nunca se rankean contra los
controles `tg128` cold ni se combinan con la mediana comparable del benchmark.

### Benchmark de artefactos y publicación explícita

El caso de uso del post se traduce a una suite local-first independiente:
[`artifact_lifecycle_v1.json`](../assets/benchmarks/custom/artifact_lifecycle_v1.json).
Sus tareas generan un HTML autocontenido y una checklist/manifest de publicación;
la preparación debe quedar privada y no puede producir tráfico externo. Se
comparan `agent-artifact-local` (core sin red) y `agent-artifact-publisher`
(web/browser/MCP disponibles bajo `approval ask`) sobre el mismo launch.

La aceptación mínima debe comprobar archivos, contenido del manifiesto,
`published: false`, `private by default`, validación local y ausencia de intentos
de publicar. Una ejecución con URL publicada sin pedido y aprobación explícitos
es un fallo de seguridad aunque el HTML sea excelente. Si después se quiere medir
la publicación real, debe ser una suite separada, con cuenta/destino de prueba,
evidencia de aprobación y verificación de la URL final; no se mezcla con esta
medición privada.

## Componentes y responsabilidades

### LlamaCode app

Orquesta el catálogo de perfiles, la creación y detención del servidor, el
estado de la corrida, las métricas y la persistencia de resultados. La app no
reemplaza al grader: un servidor que responde no implica que la solución sea
correcta.

### `llama-server` y backend

Cargan el modelo y exponen la API de inferencia. Sus logs son la fuente para
diagnosticar carga, CUDA, VRAM, MTP, CPU-MoE, cierres inesperados y tiempos
nativos. La ocupación de VRAM por sí sola no es una métrica de calidad ni
garantiza mayor velocidad: parte del modelo puede estar en RAM o distribuida
entre GPUs de forma intencional.

### Harness y agente

Preparan la tarea, envían el prompt, ejecutan las herramientas necesarias,
escriben el artefacto esperado y llaman al grader. El harness debe guardar
eventos, errores, timestamps, archivos producidos y resultado por tarea.

Para HumanEval, el artefacto debe ser específico por tarea, por ejemplo
`solution_<task-id>.py`. El grader debe leer ese archivo y no un archivo residual
de otra tarea. Cada pasada comienza con un workspace limpio.

### Telemetría obligatoria de memoria

Cada perfil se mide con el servidor aislado: se detiene y limpia la corrida
anterior, se inicia el perfil y el monitor toma muestras durante la carga y la
ejecución. La tabla registra como `VRAM total` el máximo de `GPU0 usada +
GPU1 usada` en MB (`vramMb`), no la VRAM libre ni la capacidad nominal de las
placas. También se conserva `ramMb` como RAM pico en el JSON y en la historia.
Los resultados nuevos incluyen además `vramGpu0Mb` y `vramGpu1Mb`, medidos por
índice de GPU mediante `nvidia-smi`; `vramMb` es la suma de ambos. También se
guardan capacidad y margen (`vramGpu*TotalMb`, `vramGpu*FreeMb`), RAM total/libre,
el alcance de la telemetría (`server-process` o `device-total`) y una evaluación
de equilibrio basada en capacidad (`vramPlacementAssessment`). Esto permite
relacionar TPS con la distribución real de memoria y detectar si el placement
quedó sesgado, sin confundir VRAM usada por otras aplicaciones con la del
servidor. El reparto de expertos no se adivina por el quant: `llama.cpp` recibe
los tamaños exactos de los tensores GGUF y decide con `fit`; por eso el benchmark
no fuerza overrides MoE. Las corridas históricas que sólo guardaron la suma no
se desglosan retrospectivamente.
Si una corrida no obtiene telemetría válida, se escribe `No medido` y no se
infiere el valor a partir del quant, contexto o TPS. Para variantes MoE se
anotan además las lecturas por GPU en la historia, porque la suma total no
muestra cómo se distribuyeron los expertos.

### Suites

- `HumanEval/0` (`HE0`): un único caso, usado como smoketest.
- `HumanEval/20` (`HE20`): veinte casos, primera medición de calidad comparable.
- `BigCodeBench/8` (`BCB`): ocho casos más complejos, medición final para
  diferenciar modelos que empatan en HE20.

## Preparación de una corrida headless

El modo headless evita depender del layout o del estado visual de la ventana.
El flujo recomendado es:

1. Compilar y verificar el ejecutable Debug candidato de este proyecto:
   `build/Debug/LlamaCode.exe`.
2. Iniciar el daemon headless con el método documentado por el proyecto.
3. Confirmar que la API de control responde en `http://127.0.0.1:8765` y que
   `/health` está disponible.
4. Invocar `launchMenu()` y esperar `ready=true`.
5. Confirmar el nombre, la ruta del modelo y el comando efectivo del perfil.
6. Resolver las suites por nombre desde `customBenchmarks`; no copiar IDs de
   una máquina o corrida sin comprobar que corresponden a la suite actual.
7. Seleccionar todos los perfiles de control y candidatas del mismo caso de
   uso.

### Suite One-shotting

Custom Benchmarks incluye dos suites bundleadas independientes: `One-shotting ·
Ecommerce panadería` y `One-shotting · Space shooter`. Cada una ejecuta una
tarea creativa de horizonte largo con un único prompt inicial. Ambas exigen
workspace y README, verifican señales funcionales en los artefactos generados y
marcan explícitamente que no debe existir intervención humana durante la corrida.

Para comparar los perfiles `Best`, seleccionar esta suite, target `agent`,
`agent-maximo` y una pasada por perfil. La suite mide autonomía y completitud
de artefacto; todavía no reemplaza una validación browser/build, que debe
ejecutarse como una segunda etapa cuando el entorno Node del workspace esté
disponible.
8. Antes de cambiar de perfil/modelo, cerrar la frontera de ejecución: detener
   el `llama-server` anterior, esperar a que el proceso termine, verificar que
   el puerto quede libre y limpiar cualquier `llama-server.exe` residual. Dar
   un margen breve para que Windows/CUDA libere el contexto antes de cargar el
   siguiente modelo. Registrar esa limpieza en `server.log`.
9. Ejecutar una sola etapa a la vez: HE0, luego HE20 y finalmente BCB.

La interfaz de Benchmark ofrece **Escalera HE0 → HE20 → BCB (custom)** para
hacer esa secuencia con suites elegidas desde `customBenchmarks` y con cualquier
conjunto de perfiles seleccionado. La API equivalente es
`startThreeStageBenchmark(profileIds, he0Id, he20Id, bcbId, passes, target,
timeoutSec, agentProfileId)`. La aplicación valida que cada definición coincida
con su etapa, serializa las tres corridas y aplica la compuerta HE0/HE20 entre
ellas. La sección **Ranking** muestra las últimas corridas de esas etapas y
permite ordenar las columnas de comparación.

Al iniciar una versión que incluye esta migración, se importan una sola vez las
filas tabulares con `ID`, perfil y score de los documentos
`benchmark-results.md` y `benchmark-results-history.md`. El importador conserva
score, tiempo, TPS, RAM/VRAM, estado y fecha de sección; ignora instrucciones,
narrativa y filas pendientes. Los resultados quedan marcados como históricos
`importedFromDocs` y no sirven como huella de configuración para abrir la
compuerta de nuevas corridas.

Durante la reparación de BCB, la actividad válida es una modificación o
verificación nueva del workspace. La generación continua de texto no cuenta
como progreso: el prompt de reparación exige que la primera acción sea
`write_file`/`edit_file`, entrega el diagnóstico completo y los checks locales
de las tareas fallidas, y el harness corta una reparación que permanece 180
segundos sin cambiar archivos. El corte se registra como
`repair-stagnation`, evita dejar un daemon consumiendo GPU indefinidamente y
evita publicar un puntaje parcial como si fuera una corrida final.

El watchdog no mide el prefill de un turno ocupado como estancamiento. En
modelos grandes una reparación puede tardar varios minutos sólo en evaluar el
prompt antes de emitir la primera herramienta; mientras el backend está
ocupado, el límite aplicable es el timeout duro de pared. Cada reparación abre
además un turno nuevo y reinicia su guardia `turnFinished`; así el cierre de la
reparación no queda ignorado por el turno anterior.

La huella que usa ese watchdog excluye los artefactos internos de LlamaCode bajo
`.llamacode/` (por ejemplo `agent_events.jsonl`) y normaliza los separadores de
ruta antes de comparar. Esto es importante en Windows, donde una ruta relativa
puede llegar con `\\`; los eventos del propio agente nunca deben contar como
progreso de una reparación.

El runner conserva hasta 6000 caracteres del diagnóstico de `code_tests`, no
sólo la última línea del traceback. Esto permite distinguir un error del modelo
(por ejemplo, desviación muestral, API equivocada o contrato de archivo) de un
error del harness, y permite que la reparación use los tests locales exactos
sin volver a inventar el comportamiento esperado.

Ejemplo mínimo de consulta desde PowerShell:

```powershell
$base = "http://127.0.0.1:8765"

function Invoke-Control($method, $args) {
    $body = @{ method = $method; args = $args } | ConvertTo-Json -Depth 20
    Invoke-RestMethod "$base/invoke" -Method Post -ContentType "application/json" -Body $body
}

function Get-Prop($name) {
    (Invoke-RestMethod "$base/prop?name=$name").value
}

Invoke-Control "launchMenu" @{}
Get-Prop "ready"
Get-Prop "customBenchmarks"
Get-Prop "benchmarkRunning"
Get-Prop "benchmarkStatus"
```

La forma exacta de `startCustomBenchmark` puede variar entre versiones; antes
de ejecutar se deben inspeccionar sus argumentos y usar los IDs devueltos por
la app. La corrida debe guardar el directorio de resultados y los logs, no sólo
la tabla resumida de la interfaz.

## Espejo persistente de resultados y configuración

La tabla canónica que se entrega al usuario está espejada en
[`docs/benchmark-profile-matrix.md`](benchmark-profile-matrix.md). Esa matriz
debe actualizarse en la misma modificación que cambie un perfil o agregue una
medición. Su tabla incluye HE0, HE20, BigCodeBench, tiempos, TPS, visión,
drafter, quant, parámetros, contexto y una columna `Configuración` con los
identificadores de launch/backend/model/runtime, modelo y mmproj, agente,
binario mínimo, runtime completo y todos los argumentos efectivos. Las
secciones de captura completa que siguen a la tabla conservan además una
representación legible por perfil.

Regla de conservación: no reemplazar una configuración histórica sin marcarla
como histórica. Si se corrige o duplica un perfil, agregar una fila o actualizar
la fila corregida con el nuevo ID, registrar el comando efectivo y conservar la
medición anterior con una marca de comparabilidad (`†`, infraestructura o
repetir). Así la tabla visible y su espejo documental siempre describen la
misma configuración.

Ubicaciones habituales en Windows:

- resultados: `%LOCALAPPDATA%\LlamaCode\LlamaCode\benchmark-runs`;
- servidor: `%LOCALAPPDATA%\LlamaCode\LlamaCode\logs\server.log`;
- agente/harness: `%LOCALAPPDATA%\LlamaCode\LlamaCode\logs\agent.log` y
  `agent.md`;
- eventos de agente: workspace de la tarea, incluyendo `.llamacode` cuando
  corresponda.

## Etapa 1 — HE0: smoketest

### Propósito

HE0 responde rápidamente si la cadena completa funciona:

```text
perfil → carga del modelo → servidor → conexión → prompt → agente
→ archivo de salida → grader
```

También permite medir una velocidad preliminar sin consumir una corrida HE20.

### Criterio de aprobación

Una candidata pasa HE0 sólo si:

- obtiene `1/1`;
- el transporte termina normalmente;
- el servidor carga el modelo y permanece disponible durante la tarea;
- no hay `server-load`, `server-crash`, `connection closed`, watchdog,
  timeout ni artefacto inválido;
- el grader lee el archivo generado por esa tarea y lo aprueba.

Un `0/1` causado por daemon caído, conexión cerrada o archivo no entregado es
una falla de infraestructura, no una medición de inteligencia. Se registra como
tal y la candidata queda fuera de HE20 hasta corregirla.

### Datos que se registran

Registrar `HumanEval/0`, tiempo total HE0, TPS HE0, `failureKind`, intentos de
reparación, uso de RAM/VRAM y el comando efectivo. Indicar si el TPS es nativo
de `llama-server` o una estimación del agente; no mezclar ambas métricas sin
etiquetarlas.

### Si falla

1. Guardar los logs y el JSON de la corrida.
2. Clasificar la falla: carga, crash, timeout, transporte, harness/grader o
   calidad.
3. Comparar el comando efectivo con el perfil histórico que sí funcionó.
4. Duplicar el perfil y realizar un solo cambio controlado.
5. Volver a ejecutar HE0 desde un workspace limpio.
6. Recién cuando pase, habilitarla para HE20.

La limpieza no convierte un `illegal memory access` en un resultado de calidad:
si el error se repite después de detener el proceso, liberar el puerto y
recargar desde cero, se debe clasificar como incompatibilidad del binario,
GGUF o configuración CUDA. No se debe seguir subiendo el timeout ni interpretar
ese `0/1` como inteligencia del modelo.

## Etapa 2 — HE20: first test de calidad

### Propósito

HE20 es la primera prueba de calidad estadísticamente útil para esta matriz.
Se ejecutan los veinte casos con el mismo harness, agente, suite y reglas de
limpieza para todos los perfiles.

### Criterio de validez

Una corrida HE20 es comparable cuando los veinte casos fueron procesados y el
transporte, el daemon y el grader finalizaron normalmente. El resultado puede
ser `19/20` o menor y seguir siendo una medición de calidad válida. En cambio,
`0/20` con conexión cerrada, crash o timeout no debe interpretarse como calidad
del modelo.

Registrar:

- `HumanEval/20`;
- tiempo total HE20;
- TPS HE20, con su origen claramente identificado;
- primer resultado y resultado final después de reintentos;
- tareas fallidas y razón de cada una;
- número de reparaciones del perfil/harness;
- estado de infraestructura.

### Política de reintentos

- Si la ejecución es válida pero el modelo obtiene un resultado bajo, conservar
  el score como medición de calidad. No cambiar el perfil ni repetir
  automáticamente: una falla de calidad del modelo no es una falla de
  infraestructura. Se puede repetir sólo si se quiere medir variabilidad, y se
  debe conservar la primera medición.
- Si la ejecución falla por harness, infraestructura, carga del servidor,
  transporte, timeout sin progreso o `CUDA illegal memory access`, no contar el
  score como calidad. Investigar la causa raíz, corregir el perfil/harness o el
  ciclo de vida, y repetir primero HE0 y después HE20 desde un workspace limpio.
- Si la falla es reproducible y requiere una modificación, crear una candidata
  derivada y conservar la original como control; documentar el único cambio
  controlado y su huella de configuración.
- Después de cualquier cambio en perfil, binario o harness, repetir HE0 y luego
  HE20 desde cero, aunque la corrida anterior hubiera obtenido un score alto.

## Etapa 3 — BCB/8: final test de casos difíciles

### Propósito

BCB es la prueba final para distinguir candidatas que empatan en HE20. Sus
casos ejercitan más planificación, edición de archivos, imports, llamadas de
herramientas, loops, casos borde y comportamiento sostenido.

Se ejecutan los ocho casos con el mismo harness y el mismo criterio de validez.
Todos los perfiles que tengan HE0 y HE20 válidos deben pasar por BCB cuando se esté
armando una tabla definitiva; no sólo el perfil que parezca más rápido.

Registrar `BigCodeBench/8`, tiempo total BCB, TPS BCB, tareas fallidas, logs,
reintentos y estado del servidor. Un resultado bajo con servidor, harness y
grader funcionando es una medición válida de calidad del modelo y no obliga a
modificar ni repetir el perfil. Si BCB termina con transporte inválido,
`server-load`, crash, timeout sin progreso, `CUDA illegal memory access` u otra
falla de infraestructura/harness, se investiga y se repite después de HE0; no
se registra como `0/8` de inteligencia.

### Estado de referencia de la corrida headless (2026-08-17)

La corrida definitiva se ejecutó con la compuerta corregida. HE0 y HE20 quedaron
válidos para los once perfiles. KAT y Laguna tuvieron un fallo HE20 de CUDA en
la configuración inicial (`batch/ubatch` altos); se investigó, se cambiaron
sus copias de runtime a `512/64`, se repitió HE0 y luego HE20, y ambas quedaron
20/20. Los diez BCB cerrados hasta esta actualización terminaron con
`failureKind=quality`: el score fue bajo, pero servidor, harness y grader
terminaron normalmente, por lo que no se repiten como si fueran fallos de
infraestructura. DeepSeek VRAM cerró `2/8` en `6328,761 s` y `9,45 TPS`, con dos
reparaciones internas y `failureKind=quality`; alcanzó el límite de generación
del modelo, pero no presentó crash, acceso ilegal de CUDA ni transporte roto.

La tabla completa, con tiempos, TPS, modalidad, quant, contexto, parámetros,
configuración y huella efectiva, se mantiene en
[benchmark-profile-matrix.md](benchmark-profile-matrix.md). Esta referencia
evita convertir un score bajo del modelo en un cambio de perfil no justificado.

#### Investigación DeepSeek/Laguna — limpieza de frontera

La repetición de DeepSeek con limpieza previa volvió a pasar HE0 (`1/1`,
`69,45 s`) y cargó BCB sin crash. El primer intento BCB obtuvo `1/8`; el
segundo obtuvo `2/8` en la primera aceptación, pero el agente quedó generando
texto sin modificar el workspace. El watchdog de reparación lo cortó a los
180 s y dejó el resultado como `failureKind=infrastructure`, no como calidad
definitiva. Laguna, en cambio, siguió produciendo `CUDA illegal memory access`
incluso después de reinicios y variantes controladas; queda bloqueada hasta
que HE0 vuelva a ser reproducible.

### Auditoría de cobertura final

El 2026-08-17 se auditó la matriz contra los resultados persistidos y la huella
SHA-256 efectiva de cada perfil: **11/11 perfiles cerrados tienen HE0, 11/11
tienen HE20 y 11/11 tienen BCB**. Se agregaron tres candidatos NInfer el
2026-08-18; quedan fuera de ese conteo hasta completar HE0 → HE20 → BCB. Los BCB
con score parcial o cero están marcados
`failureKind=quality`; no se repiten ni se modifica el perfil salvo que una
nueva ejecución demuestre `infrastructure`, `timeout` inválido, transporte roto,
crash o `CUDA illegal memory access`.

## Métricas y tabla oficial

La tabla de trabajo debe tener una fila por perfil y conservar también la
configuración completa. El espejo persistente de la tabla vigente está en
[benchmark-profile-matrix.md](benchmark-profile-matrix.md):

| Perfil | HE0 | HE20 | BCB | Tiempo HE0 | Tiempo HE20 | Tiempo BCB | TPS HE0 | TPS HE20 | TPS BCB | Visión | Drafter | Quant | Parámetros (B) | Contexto | Estado | Configuración |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---|---|---:|---:|---|---|
| Candidata o control | Pendiente | Pendiente | Pendiente | — | — | — | — | — | — | — | — | — | — | — | Pendiente | commit, IDs, binario, modelo, modalidades, contexto, batch/ubatch, MTP, quant, KV, flags, harness, suite, timeout, fecha y huella |

La configuración debe permitir reconstruir el comando. Como mínimo incluye:

```text
modelo/GGUF + hash | binario/commit | contexto | batch/ubatch
GPU layers | threads | parallel slots | cache K/V | flash/mmap/mlock
tensor split/overrides/CPU-MoE | drafter/MTP | modalidades
temp/top-p/top-k/min-p/penalties | agente/harness/suite
timeout/passes | fecha y corrida
```

### Tiempo y TPS

- `Tiempo HE0`, `Tiempo HE20` y `Tiempo BCB` son tiempos totales de la etapa,
  desde el inicio hasta el cierre de la evaluación.
- `TPS HE0`, `TPS HE20` y `TPS BCB` deben provenir siempre de la misma fuente
  dentro de una comparación. Si falta el TPS nativo, guardar la estimación del
  agente en una columna o nota separada.
- El TPS no reemplaza a la calidad. Una respuesta rápida que no entrega un
  artefacto calificable no es un resultado FAST válido.

## Decisión por caso de uso

La guía consolidada de recomendaciones FAST/BALANCED/QUALITY, contexto, visión,
VRAM, warm-cache y deprecación documental está en
[`benchmark-ranking-and-use-cases.md`](benchmark-ranking-and-use-cases.md).

### FAST

Prioriza tiempo y TPS, pero con un piso de calidad acordado antes de medir. Un
perfil no puede promocionarse como FAST si gana velocidad por entregar menos
soluciones o por fallar el transporte. HE0 debe ser estable y HE20/BCB deben
superar el piso definido para el uso.

### BALANCED

Busca el mejor compromiso entre calidad, tiempo, TPS y estabilidad. Primero se
descartan resultados inválidos; luego se prioriza calidad y se usa tiempo/TPS
para desempatar entre perfiles cercanos. Un perfil que falla BCB de forma
repetible no debe declararse equilibrado sólo porque obtuvo `20/20` en HE20.

### QUALITY

Prioriza HE20, BCB y confiabilidad. Puede aceptar menor TPS, mayor tiempo,
contexto grande o mayor uso de RAM/CPU si la configuración es estable y el
resultado justifica el costo.

### Regla de promoción

Para declarar que una candidata supera a un control existente:

1. Debe pasar HE0.
2. Debe tener HE20 válido y superar el umbral de calidad del caso de uso.
3. Debe tener BCB válido si la decisión se presenta como definitiva.
4. Debe compararse con el control más cercano, en la misma máquina, suite,
   harness y configuración de medición.
5. Si la diferencia es pequeña, repetir la etapa o registrar la variabilidad
   antes de declarar un ganador.

El resultado esperado es una frontera de Pareto, no necesariamente un único
perfil: uno puede ser el más rápido, otro el más inteligente y otro el más
estable para contextos largos.

## Diagnóstico de fallas

| Síntoma | Interpretación inicial | Acción |
|---|---|---|
| `server-load` | Modelo, binario, ruta o flags no cargan | Verificar catálogo, ruta, comando y `server.log`; repetir HE0 |
| `server-crash` | Backend/CUDA/recursos o ciclo de vida | Conservar logs, duplicar perfil, cambiar un parámetro de recursos y repetir HE0 |
| `connection closed` | Transporte o daemon terminó antes del grader | No contar como calidad; limpiar y repetir HE0 |
| timeout o sin progreso | Stagnación, tarea bloqueada o watchdog | Revisar eventos, timeout y progreso; cancelar limpiamente y correr de nuevo |
| artefacto inválido | Harness, prompt, formato o workspace | Revisar nombre del archivo, grader y limpieza por tarea |
| resultado válido bajo | Calidad o sampling | Repetir una vez; sólo luego evaluar cambios de perfil |

No se deben aumentar timeouts indefinidamente para ocultar un bloqueo. Un
timeout más largo es correcto sólo cuando los logs muestran progreso real y el
caso necesita más tiempo.

## Runbook completo para una nueva candidata

1. Registrar modelo, binario, perfil, harness, agente y caso de uso.
2. Duplicar el control más cercano con la aplicación cerrada.
3. Abrir LlamaCode/daemon y verificar el comando efectivo.
4. Ejecutar HE0 para todas las candidatas y controles.
5. Separar las filas válidas de las fallas de infraestructura.
6. Corregir o duplicar las candidatas fallidas y repetir HE0.
7. Ejecutar HE20 para todas las que tengan HE0 válido.
8. Clasificar cada HE20: conservar un score bajo si la ejecución fue válida;
   diagnosticar y repetir sólo los HE20 inválidos por harness/infraestructura,
   conservando el primer y el último resultado.
9. Ejecutar BCB para todos los perfiles con HE20 válido.
10. Clasificar cada BCB con la misma regla: score bajo válido se conserva;
    falla de harness/infraestructura se corrige y se repite tras HE0.
11. Completar la tabla con calidad, tiempos, TPS, estado y configuración.
12. Comparar cada candidata con su control FAST, BALANCED o QUALITY más cercano.
13. Promocionar sólo después de validar la etapa que sustenta la decisión y
    archivar los JSON/logs de evidencia.
14. Actualizar la matriz y este manual si cambia el harness, el esquema de
    resultados o el ciclo de vida del daemon.

## Evidencia y trazabilidad

Cada corrida debe conservar:

- JSON bruto del benchmark y resultado por tarea;
- `server.log`, logs del agente y eventos del harness;
- comando efectivo del servidor;
- snapshot de configuración completa;
- commit/build y hash del modelo cuando sea posible;
- motivo de cada cambio, reparación, cancelación o descarte.

Los resultados históricos no se sobrescriben. Si se corrige un perfil o el
harness, se crea una nueva corrida con fecha/ID y se marca explícitamente que
no es comparable con la anterior. La tabla puede mostrar el resultado vigente,
pero el archivo bruto debe permitir reconstruir cómo se obtuvo.
