# Perfiles de personalidad y estilo

LlamaCode permite asociar preferencias reutilizables de expresión a cualquier
perfil de agente. Se guardan localmente en `persona_styles.json` dentro de la
carpeta de perfiles configurada por `LLAMACODE_PROFILES_DIR`.

## Tipos

- `personality`: tono conversacional, iniciativa, nivel de detalle y forma de
  acompañar al usuario.
- `writing-style`: ritmo, longitud de frases, vocabulario, metáforas, diálogos y
  ejemplos de referencia.

Estos perfiles sólo afectan la forma de responder. No conceden permisos, no
activan tools y no pueden reemplazar instrucciones de seguridad u operación.

## Uso

Desde Ajustes → Perfiles de agente se puede seleccionar una personalidad y un
estilo. El perfil activo conserva únicamente los IDs de esos artefactos. El
backend inyecta la ficha y, opcionalmente, hasta dos ejemplos dentro de un
límite de contexto configurable.

Los selectores están tipados: una personalidad no aparece como estilo de
escritura ni al revés, y ambos incluyen `Ninguno` para limpiar la asociación.
Al guardar un perfil asociado, el backend activo reconstruye el system prompt
inmediatamente; no hace falta cambiar de modelo ni reiniciar la sesión.

La ficha debe describir patrones observables y no repetir datos privados. Desde
Ajustes se puede usar la heurística local o **Analizar con modelo**. Esta última
envía una petición JSON al backend activo, valida la respuesta y sólo entonces
actualiza el perfil. Si no hay backend disponible, la heurística sigue siendo
offline. También se puede usar `buildStyleAnalysisPrompt()` desde ControlApi.

La edición permite conservar varias muestras: separalas con la línea
`--- EJEMPLO ---`. Al guardar y volver a abrir el perfil, la UI conserva cada
muestra como un elemento independiente del JSON.

Los ejemplos se recuperan con ranking local por términos de la consigna actual y
se recortan al presupuesto del perfil. Esto funciona sin embeddings ni red; si
en el futuro hay un índice semántico disponible, debe conservar este ranking
como fallback determinista.

## Importación y exportación

Los perfiles se representan como JSON versionado. Al importar, LlamaCode genera
un nuevo ID y nunca sobrescribe el perfil existente. Las muestras quedan en la
máquina local y se recortan al límite configurado antes de entrar al prompt.

## Precedencia

La personalidad y el estilo son preferencias de expresión. La precedencia es:

1. sistema y seguridad;
2. permisos, aprobaciones y herramientas;
3. instrucciones del agente;
4. personalidad y estilo;
5. pedido concreto del usuario.

Si el usuario pide otro tono o formato para una respuesta puntual, esa petición
debe prevalecer sobre el estilo persistente siempre que no contradiga una regla
superior.

## Validación headless

La funcionalidad no depende de QML. El test unitario usa `QTemporaryDir`, no
inicia ventanas ni servidores de modelos y cubre serialización, CRUD,
importación, límites, perfiles deshabilitados y la regla de que `personality` no
inyecta ejemplos de escritura.

Con el árbol configurado y Qt disponible:

```powershell
cmake -S . -B build_tests -A x64 -DBUILD_TESTS=ON `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\msvc2022_64" `
  -DFETCHCONTENT_UPDATES_DISCONNECTED=ON
cmake --build build_tests --config Debug --target test_agent_profiles
$env:QT_QPA_PLATFORM = "offscreen"
ctest --test-dir build_tests -C Debug -R test_agent_profiles --output-on-failure
```

La forma recomendada, que además coordina builds concurrentes, es:

```powershell
.\tests.bat Debug
```

Para probar el mismo contrato a través de ControlApi sin GUI, iniciar el daemon:

```powershell
$env:LLAMACODE_CONTROL_PORT = "8876"
$env:LLAMACODE_PROFILES_DIR = "$pwd\headless-profile-test"
.\build\Debug\LlamaCode.exe --agent-daemon
```

Luego descubrir el sub-target y crear/editar un perfil:

```powershell
Invoke-RestMethod "http://127.0.0.1:8876/methods?target=profileManager"
$body = @{ method = "addPersonaStyleProfile"; args = @("CI style", "writing-style") } |
  ConvertTo-Json -Compress
$created = Invoke-RestMethod "http://127.0.0.1:8876/invoke?target=profileManager" `
  -Method Post -ContentType application/json -Body $body

# Después de asociar el styleProfileId a un AgentProfile, probar el prompt final
$preview = @{ method = "previewPersonaStylePrompt"; args = @("<agentProfileId>", "código del compilador") } |
  ConvertTo-Json -Compress
Invoke-RestMethod "http://127.0.0.1:8876/invoke?target=profileManager" `
  -Method Post -ContentType application/json -Body $preview
```

Los smoke tests headless deben usar un directorio de perfiles temporal y cerrar
el daemon al terminar. La prueba no debe depender de un modelo descargado, de
una GPU ni del estado visual de Windows.

El smoke reproducible de ControlApi está en `tests/headless_persona_styles.ps1`:

```powershell
.\tests\headless_persona_styles.ps1 -Exe .\build\Debug\LlamaCode.exe -Port 8896
```

El script crea un directorio temporal, inicia el daemon oculto, espera `/health`
y valida creación/edición de perfiles, rechazo de análisis inválido, aplicación
de análisis JSON válido, asociación a un agente, ranking del ejemplo pertinente,
límite de ejemplos e importación/exportación con ID nuevo.
Al finalizar detiene el proceso y elimina el directorio temporal. No carga un
modelo y no requiere GPU, QML, red externa ni interacción con el escritorio.
El análisis asistido por modelo sigue siendo un smoke opt-in separado: necesita
un backend local o cloud real y se debe verificar por
`personaStyleAnalysisStatus` (`running` → `ready`/`error`).
