# Protocolo A/B de Laguna S 2.1

Estas variantes no reemplazan los perfiles Laguna históricos. Sirven para separar
el efecto del contexto, el template y el modo de razonamiento antes de atribuir
los loops al modelo o a la infraestructura.

## Variantes

| Hardware | Template | Perfil | Contexto | Batch / ubatch |
|---|---|---|---:|---:|
| 24 GB + RAM | oficial llama.cpp | `sys-bench-laguna-s-2-1-q2-24gb-32k-official` | 32k | 512 / 128 |
| 24 GB + RAM | v24 loop-guard | `sys-bench-laguna-s-2-1-q2-24gb-32k-v24` | 32k | 512 / 128 |
| 48 GB dual | oficial llama.cpp | `sys-bench-laguna-s-2-1-q2-48gb-32k-official` | 32k | 512 / 64 |
| 48 GB dual | v24 loop-guard | `sys-bench-laguna-s-2-1-q2-48gb-32k-v24` | 32k | 512 / 64 |

Todas reutilizan `UD-Q2_K_XL`, KV `q4_0`, sampling conservador y el reparto de
GPU del perfil padre. Las variantes quitan `reasoning-format` y
`reasoning-preserve`; antes de iniciar cada corrida hay que poner **Pensamiento del
servidor: OFF** en la UI. El toggle global de la app es la fuente de verdad y no
debe mezclarse entre corridas.

## Orden de prueba

1. En el mismo hardware, correr primero oficial y v24 con la misma tarea.
2. Repetir cada variante tres veces: una carga fría y dos cargas calientes.
3. Registrar HE0/HE20 y luego BCB sólo si el servidor termina sin crash,
   `Connection closed` ni timeout de infraestructura.
4. Comparar calidad final, primer intento, loops/reparaciones, TTFT, tokens/s y
   tiempo total. No mezclar 24 GB con 48 GB en la comparación de template.

Interpretación rápida:

- Oficial mejora claramente a v24: el loop-guard/instrucciones extra de v24 está
  interfiriendo.
- Ambas mejoran a 32k pero siguen fallando igual: el contexto/offload era parte
  del problema, pero queda una limitación del modelo o del quant Q2.
- Oficial y v24 fallan igual en 48 GB con thinking OFF: no conviene promover
  Laguna como ejecutor; conservarlo como planner/reviewer.
