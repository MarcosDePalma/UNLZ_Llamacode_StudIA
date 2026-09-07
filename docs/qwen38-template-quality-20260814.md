# Qwen3.8 · validación de plantilla safe-v2 · 2026-08-14

Se probó la plantilla comunitaria `qwen3.8-safe-v2` de Chromix_, incorporada en
`assets/chat-templates/qwen38-tools-fixed.jinja`. El smoke test confirmó parseo
en llama.cpp b10331, MMPROJ cargado y MTP activo (`195/237` tokens draft
aceptados en una consulta).

Con Qwen3.8 Q4_K_M, MTP4, 2× RTX 3090, `reasoning off`, temperatura 0,6 y seis
prompts deterministas repetidos dos veces:

| Resultado | Valor |
|---|---:|
| Correctos | **12/12** |
| Pasadas | 2 |
| Tareas por pasada | 6 |
| Presupuesto máximo | 768 tokens |
| Promedio generado | 339 tokens |

La corrida anterior con la plantilla simplificada y un límite de 256/384 tokens
no era una medición de calidad válida: varias respuestas correctas fueron
truncadas antes de emitir el marcador final. El cambio de plantilla aporta
robustez real para roles `developer`, historial `thinking`, multimodalidad y
tool calls XML/JSON; el resultado 12/12 además elimina el artefacto de truncado
en esta batería, aunque no constituye todavía un benchmark amplio de coding.

Fuente del template: [post de Chromix_ en LocalLLaMA](https://www.reddit.com/r/LocalLLaMA/comments/1voha70/fixedimproved_jinja_chat_template_for_qwen_38/).
