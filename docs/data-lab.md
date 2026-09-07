# Data Lab

Data Lab es el núcleo local para convertir documentos en datos estructurados y
verificables. El job persiste en `AppLocalData/LlamaCode/data-lab/jobs/` y no
envía documentos fuera de la máquina por sí mismo.

## Flujo actual

1. Crear un job con nombre, esquema JSON y archivos.
2. Extraer texto mediante `DocumentExtractor`.
3. Generar un prompt estricto por documento con `DataLabStore::extractionPrompt`.
4. Ejecutar `runExtraction` contra un endpoint OpenAI-compatible local.
5. Parsear la respuesta, reintentar errores transitorios y reparar una respuesta
   que no sea JSON válido.
6. Validar la respuesta JSON con `validateRecord` o un array con
   `validateRecords`.
7. Exportar el job completo desde la UI a JSON, CSV o SQLite.

`routeStage` clasifica el documento como `DATA-FAST`, `DATA-QUALITY` o
`DATA-REPAIR`. La clasificación es determinística y no depende de nombres de
aplicaciones ni de coordenadas. `arbitrateCandidates` compara dos candidatos,
acepta acuerdo exacto y envía conflictos a `needs_review`.

El extractor conserva el hash del archivo, el estado, el texto extraído y los
errores. La validación es determinística y admite `string`, `number`, `date`,
`enum` y `boolean`. Los registros inválidos quedan en `needs_review`; nunca se
convierten silenciosamente en datos válidos.

## Esquema mínimo

```json
{
  "fields": {
    "cliente": { "type": "string", "required": true },
    "importe": { "type": "number", "required": true },
    "moneda": { "type": "enum", "values": ["ARS", "USD", "EUR"] }
  }
}
```

La extracción con modelo y el arbitraje entre perfiles se ejecutan sobre este
contrato; el modelo no define por sí solo qué columnas son aceptables. Una
respuesta puede ser un objeto único o un array de objetos para documentos con
múltiples registros.

## Pruebas headless

Las pruebas de Data Lab no crean ventanas, no mueven el mouse, no requieren
Internet ni cargan un modelo real. `test_datalab` incluye un servidor HTTP falso
local para probar la llamada OpenAI-compatible, parseo, persistencia y estado
final del registro. El servidor secuencia respuestas para cubrir reintento HTTP
5xx y reparación de JSON inválido; también se verifican arrays de registros,
evidencia de campos y exportación SQLite.

```powershell
.\tests.bat Debug

ctest --test-dir build_tests -C Debug `
  -R test_datalab --output-on-failure

ctest --test-dir build_tests -C Debug `
  -R "test_datalab|test_document_extractor" --output-on-failure

ctest --test-dir build_tests -C Debug `
  -R "qml_" --output-on-failure
```

`qml_datalab` carga `DataLabPage.qml` con un `DataLab` falso y
`QT_QPA_PLATFORM=offscreen`; verifica la selección inicial de job/documento sin
ventanas ni mouse. Si el build existente todavía no lo registró, regenerar y
construir el harness antes de ejecutar CTest:

```powershell
cmake --build build_tests --config Debug --target qml_harness -- /m:1
ctest --test-dir build_tests -C Debug -R '^qml_datalab$' --output-on-failure
```

Las pruebas con un modelo real son E2E optativas y no forman parte de `ctest`.
Las pruebas de automatización visual tampoco forman parte del gate headless.
