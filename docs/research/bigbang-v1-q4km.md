# BigBang-v1 Q4_K_M

Perfil experimental para comparar `endless-frontier/BigBang-v1` contra los
perfiles KAT-Coder existentes. Usa el GGUF Q4_K_M de bartowski y el projector
bf16 publicado en el mismo repositorio.

## Variantes

- baseline: 131k, KV q8, sampling conservador;
- throughput: 131k, batch 8192 / ubatch 2048;
- contexto: 196k, KV q4;
- receta del post: `temp 0.70`, `top-p 0.08`.

La comparación debe ejecutarse con la misma suite, seed, contexto efectivo y
cantidad de repeticiones que KAT-Coder. El MTP está embebido en el GGUF y se
activa con `--spec-type draft-mtp`; requiere llama.cpp b10262+.

## Ranking actualizado: HumanEval/0, 3 pasadas

Corrida del 12 de agosto de 2026, con la reparación de paths y template de
tool-call aplicada. La suite tiene un solo ítem, por lo que esto es un smoke
test de ejecución y velocidad, no un ranking de capacidad. La clasificación usa
la mediana de las pasadas calientes (2/3 y 3/3); la primera pasada se informa
aparte como costo en frío.

| Puesto | Perfil | Calidad | T First caliente mediano | QPM caliente mediano | Frío / total | RAM | VRAM |
|---:|---|---:|---:|---:|---:|---:|---:|
| 1 | KAT-Coder-7-8-26 | 1/1 en 3/3 | **9,2 s** | **653,0** | 20,4 s / 9,2 s | 28,7–28,9 GB | 28.036 MB |
| 2 | BigBang 196k KV q4 · MTP | 1/1 en 3/3 | **9,4 s** | **640,6** | 10,3 s / 9,4 s | 35,2–35,3 GB | 31.207–31.263 MB |
| 3 | BigBang 131k · batch 8192/ubatch 2048 | 1/1 en 3/3 | **9,8 s** | **612,9** | 9,7 s / 9,3 s | 39,9 GB | 33.117–33.189 MB |
| 4 | BigBang 196k KV q4 · perfil 115 | 1/1 en 3/3 | **9,4 s** | **600,7** | 10,3 s / 9,4 s | 30,8 GB | 28.925–28.998 MB |
| 5 | BigBang MTP · batch alto | 1/1 en 3/3 | **11,0 s** | **545,7** | 11,5 s / 10,5 s | 61,2 GB | 33.949 MB |

### Lectura

- **Ganador práctico:** KAT-Coder, con 9,2 s calientes y el mejor QPM. La
  primera pasada fue anómalamente fría (20,4 s), de modo que no debe usarse para
  afirmar que el modelo sea intrínsecamente más lento.
- **Mejor alternativa BigBang:** 196k KV q4 con MTP, a sólo 0,2 s de KAT en la
  mediana caliente y con menor consumo que las variantes de batch alto.
- El batch 8192/ubatch 2048 no mejora la latencia de esta tarea; el batch alto
  aumenta mucho la RAM y queda último en QPM.
- Las diferencias entre KAT y BigBang 196k KV q4 son demasiado pequeñas para
  considerarlas concluyentes con un único ítem. Para un ranking de calidad hay
  que repetir con una suite de al menos 10–20 tareas.

## Referencia DeepSeek

Los perfiles DeepSeek disponibles no forman parte del ranking principal: fueron
corridas de una sola pasada, anteriores a la separación entre medición fría y
caliente. Sirven como referencia de orden de magnitud, no como comparación
estadística con las corridas nuevas.

| Perfil | Calidad | Mejor T First observado | QPM equivalente | RAM | VRAM | Estado |
|---|---:|---:|---:|---:|---:|---|
| **deepseek v4-antirez-10-8-26** | 1/1 | **70,5 s** | **85,1** | 85,5–85,6 GB | 31.272–31.613 MB | Mejor DeepSeek medido |
| DeepSeek V4-7-8-26 | 1/1 | 215,4 s | 27,9 | 101,7 GB | 30.944 MB | Muy lento |
| DeepSeek V4-7-8-26, segunda corrida | 1/1 | 765,2 s | 7,8 | 97,9 GB | 31.502 MB | Variación anómala |

El perfil DeepSeek genérico que tendría sentido reintentar es **antirez**, con
tres pasadas y el nuevo esquema de medición. Su mejor corrida genérica queda
unas 7,7 veces por encima de KAT-Coder en `T First`, y consume aproximadamente
tres veces más RAM. Las variantes `bench antirez` son mejores y se detallan
abajo. El DeepSeek V4 estándar sólo conviene repetirlo si se quiere investigar
la variación o evaluar calidad en una suite más difícil; no es un candidato
competitivo para este smoke test.

### Variantes `bench antirez`

La familia Antirez sí tiene varias configuraciones válidas adicionales. Este es
el ranking por la corrida única observada; el QPM es equivalente a `60 / T First
* 100`, no una mediana caliente.

| Puesto | Variante | Calidad | T First | QPM equiv. | Contexto / KV | RAM | VRAM |
|---:|---|---:|---:|---:|---|---:|---:|
| 1 | **prefill 32k · B8192 · U2048** | 1/1 | **54,5 s** | **110,1** | 32k / q4_0 | 85,8 GB | 32.061 MB |
| 2 | stress 32k · B8192 · U1024 | 1/1 | 61,2 s | 98,0 | 32k / q4_0 | 85,7 GB | 31.567 MB |
| 3 | stress 131k · B4096 · U1024 | 1/1 | 61,9 s | 96,9 | 131k / q4_0 | 86,0 GB | 32.363 MB |
| 4 | stress 64k · B4096 · U1024 | 1/1 | 62,5 s | 96,0 | 64k / q8_0 | 85,9 GB | 32.277 MB |
| 5 | 64k · B4096 · U512 · KV q4_0 | 1/1 | 71,0 s | 84,5 | 64k / q4_0 | 85,6 GB | 31.913 MB |
| 6 | 32k · B4096 · U512 · KV q4_0 | 1/1 | 72,3 s | 83,0 | 32k / q4_0 | 85,7 GB | 31.280 MB |

Por lo tanto, el mejor candidato DeepSeek para repetir es **Antirez prefill
32k/B8192/U2048/KV q4_0**. El segundo candidato sería **Antirez stress 131k**:
es apenas 7,4 s más lento en esta corrida y prueba un contexto mucho mayor.
Conviene repetir ambos con 3 pasadas para saber si la ventaja de prefill es real
o sólo efecto de una única medición.
