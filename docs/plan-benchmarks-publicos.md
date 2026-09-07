# Plan de ejecución: benchmarks públicos y la pregunta del quant

Estado al 2026-08-08: fases 1, 2, 3, 5 y 6 ejecutadas. La fase 4 no se lanzó:
el empate 19/20 entre ThinkingCap y DeepSeek ya quedó resuelto por una diferencia
de tiempo de 7x entre ellos y 70x contra KAT; gastar otras ~5 h no puede cambiar
la decisión operativa. Resultados reproducibles en
`docs/benchmark-jerarquia-modelos.md`.

## Deuda reconocida

1. **No se ejecutó ningún benchmark descargado.** Se implementó el importador
   (`BenchmarkPack`) y ahí quedó: no se bajó ningún JSONL ni se corrió contra
   ningún modelo. El importador sin corrida no prueba nada.
2. **`code_tests` no puntúa.** `BenchmarkPack::grade()` devuelve `false` a
   propósito para ese tipo y `AppController` lo saltea (`graderType !=
   "code_tests"`). O sea **HumanEval importa 164 ítems y saca 0**. Es el pack más
   discriminante de los tres y es el único que no corre.
3. **Q2/Q3/Q4 de DeepSeek no se compararon.** Q4 no entra en la máquina (medido:
   ~170 GB contra ~138 utilizables). Q2 no se bajó por decisión propia — sin una
   tarea donde DeepSeek se despegue no había con qué medir la mejora. Eso sigue
   siendo cierto, pero hay un instrumento mejor que las tareas: la perplejidad.

---

## Fase 1 — Runner de `code_tests` (bloqueante)

Sin esto, HumanEval no sirve y el resto del plan se cae.

**Qué**: ensamblar `extractCode(response) + "\n" + item.tests`, escribir a un
directorio temporal, ejecutar `python`, exit 0 = pass.

Ya existe la pieza: `acceptance.commands` está en el schema y hay `QProcess` en el
path de eval. Es enganchar, no inventar.

**Requisitos no negociables**:

- **Timeout duro** (~20 s). Un modelo devuelve `while True:` tarde o temprano, y
  sin timeout el benchmark se cuelga sin diagnóstico.
- **cwd temporal y aislado**, borrado al terminar. El código viene de un LLM: no
  puede escribir sobre el workspace ni sobre el repo.
- **Sin red** en el proceso hijo, en lo que el SO permita.
- Capturar stderr: cuando falla hay que poder ver *por qué* (assert vs
  SyntaxError son diagnósticos distintos).

**Tests** (`tests/test_eval.cpp`, sin red):
- código correcto → pass
- código que compila pero falla el assert → fail
- código que no parsea (SyntaxError) → fail, con el error capturado
- `while True:` → fail por timeout, y el timeout **se respeta** (el test mide que
  no tarde más de lo pactado)
- respuesta sin bloque de código → fail sin crashear

**Criterio de éxito**: HumanEval con 10 ítems da un score distinto de 0 y de 10 en
al menos un modelo.

---

## Fase 2 — Bajar y correr los packs

**Por qué HumanEval y no GSM8K/MMLU**: GSM8K y MMLU son numérico y opción
múltiple, que es exactamente el tipo de tarea donde los tres modelos ya empataron
(ver `benchmark-jerarquia-modelos.md`). Van a volver a saturar. HumanEval falla
por razones de **capacidad real** — edge cases, off-by-one, firma mal leída — y el
grader es binario y ejecutado, sin substrings.

1. Script de descarga a `models-cache/benchmarks/`:
   - HumanEval (164 ítems, MIT) — el que importa
   - GSM8K test (1.319, MIT) — como control barato
   - MMLU subset (opcional)
2. Importar con `importBenchmarkPack(path, limit)`. Empezar con **limit 20** para
   validar el circuito completo antes de gastar horas.
3. Correr sobre los tres perfiles nombrados: `KAT-Coder-7-8-26`,
   `ThinkingCap+MTP-7-8-26`, `DeepSeek V4-7-8-26`.
4. Recién ahí, HumanEval completo (164) sobre los que sigan empatados.

**Costo estimado**: 164 ítems × ~600 tokens de salida. A 110 t/s (KAT) son ~15
min; a 7 t/s (DeepSeek) son ~4 h. Por eso el limit primero.

---

## Fase 3 — La pregunta del quant, con el instrumento correcto

La hipótesis abierta es si el **UD-IQ3_S** degradó a DeepSeek. El benchmark es el
instrumento caro y ruidoso para eso. El barato y directo es la **perplejidad**.

### 3a. Perplejidad comparada (viable hoy)

```bash
llama-perplexity -m ds4-IQ3_S.gguf -f wikitext-2-raw/wiki.test.raw --chunks 40
```

No necesita fp16 ni logits base: se compara el número de un quant contra el otro.
Si IQ2_M da una perplejidad mucho peor que IQ3_S, degradó; si dan parecido, el
quant no es el problema.

**Costo**: ~40 chunks de 512 tokens. A ~90 t/s de prefill, ~20 min por quant.

### 3b. KL-divergence (mejor, pero probablemente inviable)

```bash
llama-perplexity -m ds4-q3.gguf --kl-divergence-base ds4-f16.logits --kl-divergence
```

Es el instrumento correcto: mide cuánto se aleja la distribución del quant
respecto del modelo sin cuantizar, sin depender de ninguna tarea ni del parseo de
respuestas.

**El problema**: hay que generar los logits base con el **fp16**, y DeepSeek V4 en
fp16 son ~568 GB. No entra ni por chunks en esta máquina — habría que generar los
logits en otro lado y traer el archivo.

**Alternativa parcial**: usar IQ3_S como base y medir KL de IQ2_M contra él. No
dice cuánto se alejan de la verdad, pero sí cuánto se alejan **entre sí**: si el
KL es chico, bajar a IQ2_M es gratis en calidad y regala velocidad.

### 3c. Decisión

- KL sano / perplejidad pareja → DeepSeek empata **porque es así de bueno en
  estas tareas**, no por el quant. Veredicto cerrado: sale del roster.
- Perplejidad fea en IQ3_S → el empate era artefacto del quant, y vale bajar
  IQ2_M para confirmar en la otra dirección.

**Q4 queda descartado sin más**: ~170 GB contra ~138 GB utilizables. Requiere
pasar a 192 GB de RAM.

---

## Orden de ejecución

| # | tarea | bloquea a | costo |
|---|---|---|---|
| 1 | Runner de `code_tests` + tests | todo lo demás | ~1 h |
| 2 | Descargar HumanEval + GSM8K | 3 | minutos |
| 3 | HumanEval limit 20 en los 3 modelos | 4 | ~1 h |
| 4 | HumanEval completo si hay empate | — | ~5 h |
| 5 | Perplejidad IQ3_S (baseline) | 6 | ~20 min |
| 6 | Bajar IQ2_M + perplejidad + HumanEval | veredicto | 91 GB + ~2 h |

### Resultado ejecutado (2026-08-08)

| quant | WikiText-2 PPL (40 x 512) | HumanEval-20 | tiempo HumanEval |
|---|---:|---:|---:|
| UD-IQ3_S | 4,6923 ± 0,12246 | 19/20 | 1.597 s |
| UD-IQ2_M | 5,7034 ± 0,15557 | 18/20 | 1.621 s |

IQ2_M empeora la PPL **21,6%**, pierde un punto en HumanEval y no mejora el
decode observado (~7,25 t/s). Se descarta como perfil recomendado. Los dos fallos
de IQ2_M (`HumanEval/7` y `/10`) agotaron 1.200 tokens repitiendo razonamiento sin
entregar código ejecutable.

Durante la medición se detectó que llama.cpp b10228 conserva sólo el último
`-ot` cuando el flag se repite. Los perfiles y `EffectiveProfileBuilder` pasan
ahora todas las reglas en una única ocurrencia comma-separated; la calidad/PPL
anterior sigue siendo comparable. Un probe real posterior al fix confirmó una
sola regla `-ot`, score 1/1 y **6,78 t/s**: activar CUDA1 no rescata el quant y en
ese caso empeora el decode frente a los ~7,25 t/s de la corrida anterior.

### Extensión ejecutada: subset HumanEval difícil y corto

Como los ids de HumanEval no codifican dificultad, no se tomó simplemente la cola
del pack. `tools/select_humaneval_hard.py` selecciona 12 ítems no vistos mediante
complejidad AST de la solución canónica, sin incluirla en los prompts. Se hicieron
tres pasadas por perfil: KAT 33/36, ThinkingCap+MTP 30/36 y DeepSeek IQ3_S 29/36.
DeepSeek promedió 457,9 s por pasada contra 41,0 s de KAT. El resultado descarta
una ventaja de DeepSeek en funciones algorítmicas aisladas; la hipótesis agentic
requiere otro instrumento con repositorio, tools y estado acumulado.

### Extensión ejecutada: BigCodeBench-Hard

Se generó una muestra determinista de ocho tareas BigCodeBench-Hard v0.1.4 con
`tools/prepare_bigcodebench_hard.py`. El selector excluye tareas de red/procesos,
comprueba dependencias y exige que la solución canónica pase los tests oficiales
en el mismo entorno aislado. Dos pasadas dieron exactamente el mismo resultado:
KAT 3/8, ThinkingCap+MTP 3/8, Fable Fusion Q6 MTP3 32k 3/8, Laguna S 2.1 Q2 sobre 2×3090 2/8,
DeepSeek IQ3_S conservador 1/8 y MiniMax M2.7 Q3_K_S 1/8. MiniMax fue estable
con b10331 y repitió sólo `/906`, pero tardó 1.783,8 s por pasada. El perfil DeepSeek
48 GB solicitado crasheó dos veces en el primer prompt con acceso CUDA ilegal; sus
0/8 sin tokens se descartaron. Ver el detalle y tiempos en
`docs/benchmark-jerarquia-modelos.md`.

Los pasos 1–4 responden "¿alguno de los tres es mejor?". Los pasos 5–6 responden
"¿el quant arruinó a DeepSeek?". Son independientes: si el 3 muestra que DeepSeek
gana algo, el 6 pasa a ser prioritario; si vuelve a empatar, el 6 es lo único que
falta para cerrar el tema.
