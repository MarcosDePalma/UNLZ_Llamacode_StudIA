---
name: commit-conventions
description: Formato de commits del proyecto (Conventional Commits, imperativo, sin ruido).
---
COMMITS DE ESTE PROYECTO:
- Formato Conventional Commits: `tipo(alcance): resumen` en imperativo y minúsculas
  (`fix(agent): cerrar el stream al cancelar`). Tipos: feat, fix, docs, refactor,
  test, chore, perf.
- El resumen entra en 50 caracteres y describe QUÉ cambia, no qué archivos tocaste.
- El cuerpo sólo cuando el "por qué" no es obvio: qué se rompía antes, qué decisión
  se tomó y qué alternativa se descartó. Si el diff se explica solo, no hay cuerpo.
- Un commit = un cambio coherente. Si el resumen necesita un "y", son dos commits.
- Nunca commitees con el gate en rojo ni con archivos de otra tarea en el índice.
