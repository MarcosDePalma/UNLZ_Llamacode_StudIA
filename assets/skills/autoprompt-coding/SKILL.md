---
name: autoprompt-coding
description: Resuelve tareas complejas de código con alcance, plan, implementación, pruebas, revisión independiente y reparaciones acotadas.
version: 1.0.0
author: UNLZ
---

# Autoprompt coding

Usá este procedimiento cuando el objetivo tenga varios requisitos, archivos o
riesgo de regresión. La habilidad orienta el trabajo; no agrega permisos ni
reemplaza las aprobaciones del workspace.

## Bucle de trabajo

1. **Alcance.** Leé `AGENTS.md`, `README.md`, el estado de Git y sólo los
   módulos relevantes. Convertí el objetivo en requisitos verificables, riesgos,
   archivos candidatos y criterios de aceptación. No edites durante esta fase.
2. **Plan.** Proponé pasos pequeños, dependencias, pruebas de camino feliz y
   bordes, y una verificación final reproducible. Si falta autoridad, acceso o
   una decisión del usuario, detenete y explicá el bloqueo.
3. **Implementación.** Aplicá el cambio mínimo que cubra el plan. Preservá
   cambios ajenos, respetá los límites del módulo y actualizá documentación si
   cambia el comportamiento.
4. **Pruebas.** Ejecutá primero las pruebas específicas y después los gates del
   proyecto que correspondan. No declares éxito por compilar solamente: incluí
   la evidencia y el artefacto validado cuando exista.
5. **Revisión.** Revisá el diff contra el objetivo desde cero: corrección,
   regresiones, seguridad, alcance, documentación y cobertura. Una revisión
   paralela debe ser de sólo lectura; el verificador puede ejecutar pruebas si
   el workspace lo permite, pero no debe editar ni producir efectos externos.
6. **Reparación.** Corregí sólo hallazgos concretos, agregá una regresión cuando
   sea viable y repetí pruebas. Permití como máximo tres ciclos de reparación;
   si persiste un fallo, informá la evidencia en lugar de entrar en un loop.

## Contrato de resultado

En cada fase crítica, la primera línea de la respuesta debe ser exactamente una
de estas tres:

```text
LC_GATE: PASS
LC_GATE: FAIL
LC_GATE: BLOCKED
```

Usá `PASS` sólo con evidencia concreta de los criterios de aceptación. Usá
`FAIL` cuando falte un requisito o una prueba. Usá `BLOCKED` cuando no puedas
continuar sin permiso, credenciales, dependencia o decisión externa. El resumen
final debe listar cambios, pruebas ejecutadas, reparaciones, limitaciones y
artefactos relevantes.
