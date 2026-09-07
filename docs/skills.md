# Habilidades portables

LlamaCode incluye habilidades científicas y de coding versionadas, y descubre
habilidades declarativas adicionales sin inyectar todas sus instrucciones
en el contexto. El agente recibe sólo nombre, alcance y descripción mediante
`skill_list`; cuando una habilidad es relevante usa `skill_load` para leer el
cuerpo completo.

La habilidad bundled `autoprompt-coding` formaliza el loop de alcance, plan,
implementación, pruebas, revisión y reparación acotada. Su contrato `LC_GATE`
permite que un runner o una revisión humana distinga éxito verificable, fallo y
bloqueo externo. El preset nativo `autoprompt` de Tasks aplica el mismo contrato
con persistencia de estado y ramas read-only.

La disponibilidad de una skill portable se gobierna por el módulo `skills` del
`HarnessSpec` del agente: `include` funciona como allowlist (`"*"` habilita todo)
y `exclude` siempre gana. Si el módulo no está declarado, se conserva la
compatibilidad histórica y todas quedan habilitadas. Además, el harness debe
tener activas las tools `skill_list` y `skill_load`. Ver el inventario completo
en [`agent-harness-inventory.md`](agent-harness-inventory.md).

## Ubicaciones

- Global: `AppLocalData/LlamaCode/skills/<nombre>/SKILL.md`
- Proyecto: `<workspace>/.llamacode/skills/<nombre>/SKILL.md`
- Bundled: siete habilidades de coding y científicas incluidas en el ejecutable.

La precedencia es proyecto → global → bundled. El nombre
debe estar en kebab-case, coincidir con la carpeta y tener hasta 64 caracteres.

## Formato

```markdown
---
name: revision-cientifica
description: Revisa evidencia científica y conserva trazabilidad de las fuentes.
version: 1.0.0
author: UNLZ
---

# Procedimiento

1. Definir la pregunta y los criterios.
2. Buscar evidencia y registrar las fuentes.
3. Separar resultados observados de inferencias.
```

`name` y `description` son obligatorios. `version` y `author` son metadata
opcionales. `SKILL.md` tiene un límite de 256 KiB. Los scripts, referencias y
assets pueden convivir en la carpeta, pero no se ejecutan automáticamente: una
habilidad orienta al agente y nunca amplía los permisos de sus herramientas.

La vista **Agente → Skills** permite inspeccionar el catálogo efectivo y las
instrucciones cargadas. Los skills de Browser Teach continúan siendo secuencias
ejecutables separadas; no cambian de formato.
