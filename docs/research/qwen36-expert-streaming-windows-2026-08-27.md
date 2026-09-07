# Qwen3.6 y carga híbrida en Windows — 2026-08-27

## Objetivo

Verificar en esta PC si la idea de TurboFieldfare —mantener sólo una parte
de un MoE residente y cargar el resto desde almacenamiento— tiene un camino
útil para LlamaCode. Esta nota registra una medición de baseline; no cambia el
runtime ni promueve un perfil de benchmark.

## Estado de la máquina y reproducibilidad

- Host: Windows, 16 procesadores lógicos y 127,2 GB de RAM.
- GPU: 2× RTX 3090 de 24 GB. Antes de comenzar no había `LlamaCode`,
  `llama-server`, `llama-cli` ni puertos de inferencia ocupados.
- Modelo: `Qwen3.6-35B-A3B-UD-IQ4_XS.gguf`, 16,96 GB.
- Backend: llama.cpp b10331 (`7ba604f1c`), build CUDA 12.4.
- Ejecutable validado: `build/Debug/LlamaCode.exe`, 24,90 MB, generado el
  2026-08-27 a las 13:19:29.
- `tests.bat Debug`: OK; reutilizó el resultado compartido vigente.
- `build.bat Debug NOPAUSE`: OK; reutilizó el build compartido vigente.

El working tree ya tenía cambios ajenos en `.gitignore`, `CMakeLists.txt`,
`README.md`, `assets/update/latest.json`, `profiles/launches.json`,
`src/AppController.h`, `src/main.cpp`, `tests/test_tuner.cpp` y los directorios
`build_tests_aux/`/`build_tests_final/`. No se modificaron en esta investigación.

## Configuración común

Las corridas usaron dos GPU, `--n-gpu-layers 999`, `--n-cpu-moe 24`, contexto
4096, batch/ubatch 512/128, Flash Attention, KV `q4_0`, ocho hilos, sin
warmup, `--reasoning off`, semilla 42 y sampling conservador:

```text
--temp 0.6 --top-p 0.95 --top-k 20 --min-p 0.0
--repeat-penalty 1.0 --presence-penalty 0.0
```

Se compararon `--load-mode mmap` y `--load-mode none`. En b10331, `none` es
la alternativa actual a `mmap`/`--no-mmap`; no es un caché de expertos tipo
TurboFieldfare.

## Resultados directos

Prompt de aproximadamente 30 tokens, generación de 128 tokens, una pasada por
configuración:

| Carga | Tiempo total | Prefill | Decode | Resultado |
|---|---:|---:|---:|---|
| `mmap`, primera pasada | 15,61 s | 30,1 t/s | 19,6 t/s | OK, terminó en `DONE` |
| `none`, primera pasada | 11,88 s | 42,8 t/s | 35,7 t/s | OK, terminó en `DONE` |
| `mmap`, repetición | 12,39 s | 35,5 t/s | 30,1 t/s | OK, terminó en `DONE` |

La comparación no es todavía un benchmark estadístico: el estado de la caché
del sistema y de las GPU no fue reiniciado entre las pasadas. Sirve como
baseline operativo y no como conclusión de superioridad.

## Barrido de longitud con `mmap`

Con 64 tokens de salida y el mismo runtime, el barrido completado fue:

| Prompt | Tiempo total | Prefill | Decode |
|---|---:|---:|---:|
| Corto | 12,74 s | 44,0 t/s | 24,6 t/s |
| Medio | 15,28 s | 121,8 t/s | 27,3 t/s |
| Largo | 21,35 s | 215,8 t/s | 37,4 t/s |

El intento de repetir el barrido en frío con `load-mode none` no fue costeable:
el primer caso no entregó resumen tras 337,7 s aunque el proceso seguía
respondiendo. Llegó aproximadamente a 20,0 GB de memoria privada y 10,2 GB de
working set. Se detuvo de forma intencional para liberar la máquina; el caso no
se cuenta como resultado de rendimiento.

## Verificación del camino que usa LlamaCode

Se levantó temporalmente `llama-server.exe` en `127.0.0.1:18080` con el mismo
modelo y configuración base. La prueba pasó:

- `GET /health` → `{"status":"ok"}`.
- `POST /v1/chat/completions` → respuesta válida que terminó en `DONE`.
- Uso reportado: 26 tokens de prompt, 34 de completion, 60 totales.
- El servidor y el puerto fueron cerrados y verificados libres al finalizar.

## Conclusión para LlamaCode

La integración actual de LlamaCode ya puede consumir un backend
OpenAI-compatible, por lo que un futuro servidor especializado podría
conectarse sin rediseñar la UI. La ejecución local confirma también que
`--n-cpu-moe` y `load-mode none` son controles de colocación/carga de llama.cpp,
pero no implementan por sí mismos un caché de expertos con `pread`, lecturas
concurrentes y solapamiento de expertos compartidos.

En esta PC, `mmap` ofrece un camino estable y de memoria contenida para el
baseline; `none` puede producir una pasada rápida cuando está caliente, pero
su carga en frío tiene un coste de memoria y latencia demasiado alto para
usarlo como sustituto automático del enfoque de TurboFieldfare. No se agrega
ninguna variante a la tabla competitiva porque no se ejecutaron HE0/HE20/BCB.

## Próximo experimento recomendado

Si se decide avanzar, el siguiente paso debe ser un prototipo aislado del
loader/cache de expertos —preferentemente detrás de un backend externo o una
capacidad opt-in— que mida hit-rate, lecturas aleatorias, working set y
prefill/decode por separado. Después conviene repetir cada modo varias veces
con la misma presión de caché y un modelo MoE compatible. No conviene inferir
la ganancia de TurboFieldfare a partir de `--no-mmap` solamente.
