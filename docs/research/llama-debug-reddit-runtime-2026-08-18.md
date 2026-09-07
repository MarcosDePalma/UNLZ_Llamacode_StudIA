# Experimento `llama-debug`: parámetros del post de DeepSeek-V4

Fecha: 2026-08-18  
Equipo: 2× RTX 3090 de 24 GiB, 127 GiB RAM  
Ejecutable de la aplicación: `build/Debug/LlamaCode.exe`  
Binario compatible usado para medir: BeeLlama v0.3.1 CUDA 13.1, build `10096 (15d22acc8)`  
Modelo: `ThinkingCap-Qwen3.6-27B-Q3_K_M-MTP.gguf` (12,56 GiB)

## Qué se tomó del texto pegado

Sirven como hipótesis generales `--parallel 1`, `flash-attn`, ajustar `batch/ubatch`
y mantener el muestreo conservador. No se copiaron `--n-cpu-moe`, `--tensor-split`
ni los valores de 1M de contexto: pertenecen al DeepSeek-V4, a su fork CUDA y a
otras combinaciones de VRAM/RAM. `mmap+mlock` quedó fuera por su riesgo de commit
de memoria en Windows.

## Copias creadas en perfiles de usuario

El perfil existente `106_MAX-Q ThinkingCap Q3_K_M MTP` no fue modificado. Se
duplicó desde el Debug mediante `ProfileManager.duplicateLaunchProfile` y se
crearon estas copias editables:

| Variante | Launch ID | Runtime | Configuración |
|---|---|---|---|
| `llama-debug UBATCH128` | `c3a3851d-c3a0-4dc8-8018-1c408f017a95` | `fd7abae4-87b3-4001-b3a4-5a294651dbce` | batch 512, ubatch 128 |
| `llama-debug UBATCH128_BATCH1024` | `d805e63a-f4df-4b99-86b3-5472f8998d63` | `d1764c50-81cc-4fa2-b6bc-aeff51910e3f` | batch 1024, ubatch 128 |

Ambas conservan contexto 262k, `parallel=1`, flash attention, KV `q4_0`, 8
hilos y muestreo `temp=0.60`, `top-p=0.95`, `top-k=20`, `min-p=0.0`,
`repeat-penalty=1.0`, `presence-penalty=0.0`. Sus copias de backend apuntan al
binario compatible BeeLlama; el backend y runtime del perfil original siguen
intactos.

## Medición corta

Se usó `llama-bench`, 128 tokens de prompt + 32 de generación, una repetición,
2×GPU con `tensor-split 1,1`, mmap desactivado, KV K/V `q4_0`, flash attention
activo. La salida mostró dos muestras internas por modalidad; se informa el
rango observado.

| Configuración | Prefill pp128 | Decode tg32 | Resultado |
|---|---:|---:|---|
| baseline batch 512 / ubatch 64 | 413–437 t/s | 16,16–16,20 t/s | control |
| batch 512 / ubatch 128 | 438–535 t/s | 15,93–16,22 t/s | mejor prefill, decode equivalente |
| batch 1024 / ubatch 128 | 438–521 t/s | 16,07–16,15 t/s | no mejora frente a batch 512 |

Además, la variante `UBATCH128` arrancó correctamente desde `LlamaCode.exe`
Debug con `startServerAndAgent` y quedó en ejecución; luego se detuvo de forma
limpia. El binario histórico b9045 del perfil original no pudo cargar el GGUF
actual (`missing tensor ... ssm_conv1d`), por lo que no se lo considera baseline
válido de rendimiento.

## Conclusión

La única mejora respaldada por esta prueba es `ubatch=128` manteniendo
`batch=512`: aproximadamente +13–15% de prefill en esta muestra, sin ganancia
medible de decode. `batch=1024` no justifica el consumo adicional. No se cambia
ningún perfil existente ni se promociona la variante a BEST; falta repetir con
HE0/HE20/BCB si se quiere afirmar una mejora de calidad agentica.

## HE0 ejecutado después

El 2026-08-18 se ejecutó `HumanEval (1 ítems)` en target `agent`, una pasada por
variante, con `agent-chat` (`Chat liviano`). Ambas copias pasaron la compuerta
HE0 (`1/1`, `failed=false`, `failureKind=none`, sin timeout, sin reparación y
sin fallo de transporte):

| Variante | Tiempo total | Primer intento | Generación | VRAM pico | RAM pico |
|---|---:|---:|---:|---:|---:|
| `UBATCH128` | 26,242 s | 21,240 s | 7,270 s | 24.963 MB | 26.054 MB |
| `UBATCH128_BATCH1024` | 18,760 s | 14,108 s | 0,734 s | 24.910 MB | 26.099 MB |

La segunda terminó más rápido en esta pasada, pero no se promociona como mejor
runtime: la diferencia de generación es demasiado grande para una única corrida
y el benchmark no fue repetido. Los artefactos quedaron en
`C:/Users/cristian/AppData/Local/LlamaCode/LlamaCode/benchmark-runs/HumanEval_1_tems__20260818_160208`.
HE20 todavía no se ejecutó.
