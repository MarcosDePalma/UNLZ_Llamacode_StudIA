# Inventario de agentes, harnesses y skills

> Auditoría local y de documentación: 2026-08-20.
>
> Este documento describe qué está instalado o implementado, qué queda visible
> en cada runtime y qué condición lo activa. La presencia de un archivo en una
> caché no equivale a que una skill, plugin, agente o backend esté activo en
> una sesión concreta.

## 1. Modelo mental: capas que no deben mezclarse

| Capa | Qué controla | Ejemplo en este entorno | Cómo se activa |
|---|---|---|---|
| Instrucciones | Reglas durables de trabajo y alcance | AGENTS.md, README.md, CLAUDE.md | Se leen al iniciar el agente que reconoce esa convención; las reglas más cercanas al archivo tienen precedencia |
| Perfil | Presupuesto, tools, directivas, permisos y modelo | agent-intermedio, agent-rpa | Se selecciona o hereda al crear/activar una sesión |
| Harness engine | Contrato, almacenamiento y namespace de sesión | legacy/v1, next/v2 | Lo resuelve HarnessSpec.runtime |
| Adapter/backend | Loop concreto que ejecuta mensajes y tools | LlamaAgentBackend, OpencodeBackend, RawChatBackend | Lo selecciona AppController según el adapter y el flujo |
| Skill | Procedimiento reutilizable, cargado progresivamente | autoprompt-coding, review-agent | Invocación explícita o coincidencia semántica con la tarea |
| Agent/subagent | Identidad operativa y delegación | Agent Rooms, default/worker/explorer | Creación o delegación explícita; no aparece por existir en disco |
| MCP | Tools y contexto de servicios externos | servidores declarados en .codex/config.toml o configuración de LlamaCode | Servidor habilitado + tools permitidas + sesión que las expone |
| Hook/trigger | Automatización por evento | Codex hooks, TriggerManager, .claude/settings.json | Evento coincidente, no una simple presencia del archivo |

La regla transversal es que una skill orienta al agente, pero no puede ampliar
por sí misma sus permisos, el alcance de filesystem, el acceso a red ni el
catálogo de tools.

## 2. Qué existe en cada superficie

### 2.1 Proyecto LlamaCode

- AGENTS.md: instrucciones activas para agentes que trabajen en el repo.
- README.md: documentación funcional y de arquitectura; se leyó junto con
  AGENTS.md antes de esta auditoría.
- assets/skills/: 7 skills portables bundled dentro de LlamaCode.
- .llamacode/: estado de memoria y eventos; no es un catálogo de skills.
- .agents/: directorio presente, pero sin archivos de agentes declarativos.
- .claude/: configuración y una skill para Claude Code; no es el mecanismo
  de skills del agente nativo ni de Codex.
- AppLocalData/LlamaCode/agents/: definiciones de agentes creadas por el
  usuario, persistidas fuera del repo cuando la aplicación las genera.

### 2.1.1 Perfiles de sistema de LlamaCode

Los perfiles son presets inmutables de AgentProfile. El predeterminado es
agent-intermedio. El catálogo actual es:

| ID | Tools/capacidades principales | Directivas, modelo y límites |
|---|---|---|
| agent-chat | read_file, list_dir, grep, write_file, edit_file | approval ask, sin MCP ni thinking; 6 créditos, máximo 10, stopAfter 4 |
| agent-basico | core: lectura, glob, grep, escritura, edición y run_shell | approval ask, sin thinking; sin directivas especiales |
| agent-intermedio | core + search_docs, memory y code_hotspots | approval ask, discipline; sin thinking |
| agent-avanzado | intermedio + web, semantic/hybrid search, repo/context tools, verify_claims, graph y browser_network_discover | approval ask, thinking activo; discipline, testNet, projectContext, efficiency y style; 12 créditos, máximo 24 |
| agent-maximo | catálogo de tools y directivas completo, expandido desde * | approval super, thinking activo; 16 créditos, máximo 32, replanning 5, stopAfter 8 |
| agent-browser | packs core, web y browser; skills portables apagadas para aislar benchmarks | approval ask, thinking activo; timeout web 300 s, 12/24 créditos, replanning 4, stopAfter 8 |
| agent-minimal | read/list/grep/write/edit/run_shell, local-first | approval ask, sin MCP, efficiency/style; sin imágenes, sin preflight, prompt máx. 8000 caracteres, 6/12 créditos, sameCallLimit 2, stopAfter 4 |
| agent-rpa | packs core + rpa para desktop/UI Automation | approval ask, HITL para destructivas, quickToolTimeout 30 s, keepLastImages 2 |
| agent-intermedio-next | mismo punto de partida que intermedio | Harness next v2 experimental con fallback legacy |
| agent-artifact-local | pack core, sin MCP/web/browser; crea y valida entregables locales | approval ask, sin thinking; contrato privado/local, manifiesto y validación antes de cerrar |
| agent-artifact-publisher | packs core + web + browser, MCP disponible bajo demanda | approval ask, thinking activo, 12/24 créditos; separa preparar/stash de publicar y exige confirmación explícita |

El perfil no es una autorización autónoma: el catálogo final también depende
de HarnessSpec, permisos de la tarea, workspace, fase y aprobación humana.

### 2.2 Codex local

- C:\Users\cristian\.codex\skills: 10 manifests locales/globales: 6 de
  sistema y 4 skills adicionales (caveman, hatch-pet, pdf, playwright).
- C:\Users\cristian\.codex\plugins\cache: 56 manifests de plugins,
  incluidos duplicados por versión y skills de templates.
- C:\Users\cristian\.codex\config.toml: configuración global, plugins
  habilitados, modelo, MCP y políticas de la instalación de Codex.
- C:\Users\cristian\.codex\agents: no existe; no hay agentes custom TOML
  locales en esa ruta.
- En la sesión actual están disponibles los built-ins y herramientas de
  subagentes del runtime de Codex. Los plugins recomendados que figuran en la
  interfaz están disponibles para instalación, pero no se consideran activos
  hasta instalarlos explícitamente.

### 2.3 Claude Code del proyecto

La superficie Claude contiene:

- .claude/skills/gate/SKILL.md: quality gate de Claude, activado antes de
  commit o por gate o /gate; ejecuta tests.bat Release, detecta dirty tree y
  no hace commit por sí mismo.
- .claude/commands/coverage-loop.md: comando explícito /coverage-loop para
  iterar cobertura y gate.
- .claude/settings.json: hooks SessionStart y PreToolUse para proteger
  edición y comandos Git mediante tools/session_guard.ps1.
- .claude/settings.local.json: permisos locales adicionales de Claude; no se
  deben interpretar como permisos del agente nativo ni de Codex.

Estos archivos se desactivan efectivamente cuando la tarea corre fuera de
Claude Code. No se importan automáticamente a PortableSkillStore,
HarnessSpec ni al catálogo de skills de Codex.

## 3. Skills portables de LlamaCode

Estas son las skills que el agente nativo puede descubrir desde bundled,
global o proyecto. skill_list expone metadata; skill_load carga el cuerpo
completo sólo cuando el agente decide usarla.

| Skill | Capacidad | Activación típica |
|---|---|---|
| autoprompt-coding | Alcance, plan, implementación mínima, pruebas, revisión y reparación acotada; contrato LC_GATE con PASS, FAIL o BLOCKED | Tarea compleja de coding o workflow autoprompt |
| citation-verification | Verifica fuentes primarias, DOI, pasajes, retractaciones, duplicados y clasifica cada afirmación | Pedido de verificar citas o evidencia |
| critical-paper-reading | Lee pregunta, diseño, población, variables, controles, análisis, sesgos, validez y conflictos; exige ubicación de evidencia | Lectura crítica de paper |
| experimental-design | Formula hipótesis falsable, variables, controles, confusores, aleatorización, cegamiento, potencia, preregistro y criterios de éxito | Diseño o revisión de experimento |
| literature-review | Define alcance reproducible, fechas, idioma, inclusión, matriz de evidencia, DOI, contradicciones y referencias no inventadas | Revisión bibliográfica |
| peer-review | Evalúa contribución, novedad, claridad, método, reproducibilidad, ética, análisis y conclusiones; separa mayores/menores | Pedido de revisión por pares |
| reproducible-data-analysis | Proveniencia, hash, schema, unidades, faltantes, duplicados, anomalías, transformaciones deterministas, semillas, versiones y README | Análisis de datos reproducible |

### Descubrimiento, precedencia y apagado

Ubicaciones y precedencia: proyecto (<workspace>/.llamacode/skills) → global
(AppLocalData/LlamaCode/skills) → bundled. Una skill de proyecto puede
reemplazar el mismo nombre, pero no ampliar permisos. El nombre debe ser
kebab-case y el SKILL.md no puede superar 256 KiB.

El módulo skills de HarnessSpec se resuelve así:

    {
      "skills": {
        "include": ["*"],
        "exclude": ["autoprompt-coding"]
      }
    }

1. Módulo ausente: se conserva compatibilidad legacy y quedan habilitadas las
   skills descubiertas.
2. include ["*"]: habilita todo lo descubierto.
3. include ["a", "b"]: funciona como allowlist.
4. exclude siempre gana.
5. Si skill_list o skill_load no están en tools, la skill no puede llegar al
   modelo aunque el store la conozca.
6. Una fase puede sobrescribir la selección para plan, exec, verify o
   goalCheck.
7. Una skill deja de estar disponible si el archivo no se descubre, el perfil
   la excluye, la fase la excluye, el adapter no implementa ese módulo o el
   plugin que la aporta está deshabilitado.

Browser Teach usa grabaciones ejecutables separadas (browser_skill_list,
browser_skill_replay); no es una skill portable. OpenCode tampoco consume
este módulo: conserva sus propios skills y commands.

## 4. Skills de Codex y plugins instalados

La instalación local contiene 66 manifests SKILL.md en total: 10 locales y
56 dentro de cachés de plugins. En la sesión actual el runtime anuncia un
catálogo normalizado de skills disponibles; las versiones duplicadas de una
misma skill no deben contarse como capacidades distintas.

### Skills de sistema o personales de Codex

| Skill | Capacidad y activación |
|---|---|
| imagegen | Genera o edita imágenes raster; se activa ante pedidos de crear/modificar una imagen |
| openai-docs | Consulta documentación oficial para Codex, OpenAI, APIs, skills, plugins, MCP y configuración; se activa ante preguntas de esos productos |
| plugin-creator | Crea o actualiza plugins con .codex-plugin/plugin.json; se activa al crear/scaffold de plugin |
| review-agent | Revisión de código defect-first y de solo lectura; se activa ante review explícito o cuando se solicita inspección sin cambios |
| skill-creator | Crea o actualiza una skill Codex; se activa al pedir una skill reusable |
| skill-installer | Lista o instala skills curadas o desde GitHub; se activa al pedir instalación |
| caveman | Modo de comunicación ultracompacto; se activa con “caveman mode”, “less tokens”, “be brief” o equivalentes |
| hatch-pet | Crea, repara, valida y empaqueta pets animados v2; se activa ante un pet/mascota Codex |
| pdf | Lee, crea, renderiza e inspecciona PDFs con QA visual; se activa si el artefacto PDF importa |
| playwright | Automatiza un navegador real desde terminal; se activa ante flujos browser que requieren ejecución real |

### Skills de plugins por capacidad

Browser, desktop y sitios:

- control-in-app-browser: navegador embebido, navegación, inspección,
  clicks, tipeo, screenshots y pruebas web locales.
- control-chrome: tabs, sesiones logueadas, extensiones y estado del Chrome
  del usuario.
- computer-use: control de aplicaciones Windows.
- sites-building: sitios, landings, dashboards, portales y herramientas
  internas; obligatorio si el proyecto contiene .openai/hosting.json.
- sites-hosting: publicación, hosting y despliegue de Sites; se usa después
  de sites-building o cuando existe hosting.json.
- visualize: simuladores, mapas, gráficos, herramientas interactivas y
  mockups visuales.

Data Analytics:

- index: enruta la solicitud al workflow analítico correcto.
- gather-business-context: reúne contexto antes del análisis cuando falta.
- analyze-data-quality: evalúa confiabilidad, definiciones y conflictos de
  métricas.
- build-dashboard: construye dashboards o scorecards con filtros, fuentes y
  QA.
- build-report: construye reportes ejecutivos, de producto o técnicos.
- create-data-context: crea o repara una capa semántica persistente.
- design-kpis: define KPIs, drivers, guardrails, targets y medición.
- jupyter-notebooks: crea o valida notebooks SQL/Python reproducibles.
- kpi-reporting: prepara WBR/MBR/QBR y lecturas ejecutivas de KPIs.
- market-sizing: estima TAM/SAM/SOM u oportunidades con incertidumbre.
- metric-diagnostics: diagnostica movimientos, anomalías o brechas de métricas.
- product-business-analysis: analiza datos para decisiones de producto/negocio.
- publish-artifact-to-sites: publica reportes o dashboards validados en Sites.
- validate-data: valida cálculos, metodología, visuales, caveats y conclusiones.
- visualize-data: diseña, construye o audita gráficos cuantitativos.
- report-to-google-doc, report-to-google-slides, report-to-pdf: convierten
  un reporte existente sólo cuando se pide explícitamente esa conversión.

Artefactos y documentos:

- documents: crea/edita/redlinea .docx y verifica el render.
- presentations: crea/edita PowerPoint o Google Slides.
- spreadsheets: crea/edita/analiza .xlsx, .xls, .csv y .tsv como archivos;
  no controla una sesión viva de Excel.
- excel-live-control: controla un workbook abierto de Excel; no reemplaza
  spreadsheets para archivos independientes.
- template-creator: crea o actualiza una skill reusable de templates desde
  un documento, presentación, planilla, imagen, email, Slack o Site.

GitHub y plugins:

- github: orientación y operaciones generales de GitHub.
- gh-address-comments: atiende comentarios de un pull request.
- gh-fix-ci: investiga y corrige fallos de GitHub Actions.
- yeet: publica cambios locales mediante commit/push y PR cuando corresponde.
- plugin-management: inspecciona, sugiere, instala, conecta o elimina plugins
  y sus dependencias.

Templates disponibles:

Hay 20 skills de templates; se activan cuando el usuario elige el template o
el tipo de artefacto correspondiente, no para cualquier documento genérico:

artifact-template-analytics-dashboard, artifact-template-business-review,
artifact-template-design-report, artifact-template-experiment-analysis,
artifact-template-financial-budget, artifact-template-investment-committee-memo,
artifact-template-legal-memorandum, artifact-template-market-trends-report,
artifact-template-minimal-letterhead, artifact-template-operating-calendar,
artifact-template-operating-review, artifact-template-project-kickoff,
artifact-template-project-tracker, artifact-template-sales-pipeline,
artifact-template-simple-dark-mode, artifact-template-simple-light-mode,
artifact-template-strategy-memorandum, artifact-template-system-design,
artifact-template-team-alignment, artifact-template-three-statement-forecast.

Una skill de Codex se carga progresivamente: primero metadata, luego
SKILL.md, y finalmente referencias/scripts/assets sólo si son necesarios.
Puede activarse por nombre explícito o por coincidencia con la tarea. No se
arrastra automáticamente a una tarea posterior si dejó de ser relevante; se
desactiva de hecho al no seleccionarse, al cambiar de tarea, al excluirse o al
deshabilitar/desinstalar el plugin. Los plugins recomendados en la interfaz
son candidatos no instalados: no cuentan como activos en este repo.

### Plugins recomendados pero no activos por defecto

La sesión también entrega un catálogo de plugins recomendados para instalar,
separado de las skills disponibles. No se instaló ninguno durante esta
auditoría. La lista recibida fue:

`Airtable`, `Alpaca`, `Apollo.io`, `Spotify`, `Apple Music`, `LONA Trading
Assistant`, `SciSpace`, `Tarot`, `Todoist`, `Consensus`, `Sider Scholar`,
`True Sky`, `Bigdata.com`, `Gamma`, `Tredict`, `Maersk`, `Dropbox`, `Parqet`,
`Interactive Brokers (IBKR)`, `Financial Datasets`, `Fathom`, `vidIQ`,
`TickTick`, `Plaud`, `Wolfram`, `Runway`, `Caliber`, `COROS`,
`TradingCursor`, `CoinMarketCap`, `Trello`, `Longbridge`, `freddy`,
`Higgsfield`, `Stocktwits`, `CoinGecko`, `Asana`, `Atlassian Rovo`, `Base44`,
`Binance`, `Box`, `Canva`, `ClickUp`, `Cloudflare`, `Codex Security`, `Figma`,
`GitHub`, `Gmail`, `Google Calendar` y `Google Drive`.

Sus capacidades potenciales abarcan datos/CRM, trading y mercados, música,
investigación académica, tareas/calendario, logística, video, fitness,
productividad, archivos, diseño, infraestructura y correo. Sin instalación,
conexión y autorización del servicio no hay tools utilizables. GitHub aparece
en este catálogo de recomendados y también en config.toml como plugin habilitado;
es una discrepancia de estado que debe resolverse consultando el runtime antes
de asumir que cualquier otro recomendado está activo.

## 5. Agentes y delegación

### Codex

La documentación oficial define agentes built-in default, worker y
explorer. Los agentes custom se declaran en ~/.codex/agents/ o
.codex/agents/ con name, description y developer_instructions, y
opcionalmente modelo, razonamiento, sandbox, MCP y skills. En esta máquina no
se encontró ninguna definición custom en esas ubicaciones.

Un subagente se activa sólo cuando se lo solicita, cuando una instrucción o
skill aplicable lo pide, o cuando el runtime decide delegar una tarea
compatible. Consume presupuesto adicional y es más apropiado para tareas
independientes en paralelo. Se desactiva al no crearse, al completar, al
cerrarse o al abortarse; no es una identidad permanente del proyecto.

### Definiciones persistentes de LlamaCode

AgentDefinitionStore guarda en
AppLocalData/LlamaCode/agents/agents.json las definiciones creadas por el
usuario. Cada una puede referenciar:

- nombre, descripción, profileId, launchProfileId y workspace;
- instrucciones propias;
- skills y servidores MCP;
- permisos de tools;
- Tasks y triggers;
- revisiones inmutables, duplicación, restauración y feedback supervisado.

Activar una definición carga su perfil e instrucciones y persiste el agente
activo. Desactivarla o cambiar de definición cambia el perfil/instrucciones
efectivos de nuevas operaciones; la definición no crea un engine nuevo.

### Agent Rooms

Los roles efímeros son coordinator, implementer, reviewer, verifier,
perspective-a, perspective-b, researcher-a, researcher-b y
citation-checker. Los presets son:

| Preset | Composición y finalidad |
|---|---|
| review | implementer escribe; reviewer lee y ejecuta validaciones |
| autoprompt | implementer, reviewer y verifier con gate LC_GATE |
| council | dos perspectivas y verifier |
| research | dos investigadores y citation-checker |

El humano conserva el control. Los grants se normalizan para read, write,
shell, network, externalWrite y destructive; una actualización sólo puede
estrechar permisos. Los presets ponen externalWrite y destructive en falso
antes de ejecutar. La sala se desactiva al terminar/cerrar la sesión; su
timeline append-only queda persistido para auditoría.

## 6. Harnesses y adapters implementados

### Engines de HarnessSpec

| Engine | Namespace | Estado | Almacenamiento/fallback |
|---|---|---|---|
| legacy v1 | agent_llamaagent | Estable y compatible | Comportamiento histórico; fallback a sí mismo |
| next v2 | agent_harness_next | Experimental/A-B | Almacenamiento aislado; fallback a legacy |

HarnessEngine.effectiveId y effectiveVersion convierten valores vacíos o
desconocidos a legacy. El fingerprint de la spec se calcula con SHA-256. El
cambio de engine recrea el backend para no mezclar sesiones de contratos
distintos.

### Adapters/backend

| Adapter | Implementación | HarnessSpec | Activación y límites |
|---|---|---|---|
| llamaagent | LlamaAgentBackend | Sí, es el harness nativo completo | Camino principal; ReAct streaming, tools, safety, memoria, knowledge, skills y phases |
| opencode | OpencodeBackend + opencode serve | No; usa configuración propia | Backend externo secundario/legacy; sus skills y commands no provienen de HarnessSpec |
| raw | RawChatBackend | No | Chat directo sin tools ni loop de agente |
| pi | Adapter genérico de proceso estructurado | No | Print-mode por mensaje, sesiones en AppData/pi-sessions; oculto del selector principal |
| smallcode | Adapter genérico stdin/proceso | No | Usa SMALLCODE_API_URL y variables relacionadas; oculto del selector principal |

En el camino principal, valores vacíos/none y ciertos usos de opencode se
normalizan a llamaagent; esto no elimina el OpencodeBackend explícito que
todavía existe como integración secundaria. Por eso no debe leerse la
referencia histórica a OpenCode como “default del harness nativo”.

aider y goose son identificadores o referencias históricas sin backend
dedicado vigente. none tampoco es un harness adicional. Las lanes de worker
builtin, node y python son extensiones supervisadas del harness nativo, no
engines nuevos.

## 7. Módulos de HarnessSpec y reglas de activación

La spec se calcula por capas: defaults → padre extends → hijo → override de
fase (plan, exec, verify, goalCheck). Un módulo ausente hereda; un perfil no
puede elevar privilegios por encima de sus permisos efectivos.

| Módulo | Qué gobierna | Cómo se desactiva o restringe |
|---|---|---|
| runtime | engine, versión, fallback y experimental | engine incompatible/fallback; cambio recrea backend |
| worker | lane Node/Python, entrypoint, sandbox, timeouts, límites y capabilities solicitadas | sin lane/entrypoint no arranca; timeout o capability inválida detiene/deniega |
| tools | packs, include/exclude y MCP tools | tool omitida, excluida, pack ausente o MCP no habilitado |
| skills | allowlist/denylist de skills portables | reglas de la sección 3 |
| prompt | prompt builtin/custom, extras y límite | prompt vacío o fase que lo reemplace |
| loop | créditos, replanning, anti-loop, retries, watchdogs y timeouts | agotamiento de créditos, espiral de fallos o límite de llamadas |
| context | compaction, tail, dedupe, preflight, scout, graph y warmup | límites de contexto o flags de preflight/index apagados |
| permissions | aprobación, reglas, filesystem, destructive guard y mail | modo ask/manual, scope más estrecho o guardia destructiva |
| escalation | paralelismo, aislamiento, master, router y fallback | límites de paralelismo, aprobación o subagente no permitido |
| memory | facts, memoria de proyecto y consolidación | structuredEnabled/consolidation apagados |
| knowledge | preflight, citas, facts, edges y graph | enabled o preflight falsos |
| chat | thinking, razonamiento, temperatura, top-p/top-k y persona | flags del perfil o del proveedor |
| protocol | native/text/auto, leak guard, budget y reasoning | protocolo elegido o guardia de leakage |
| phases | overrides por etapa | fase sin override hereda la spec efectiva |

Invariantes importantes:

- La intersección del filesystem efectivo nunca supera el scope de la tarea.
- El guardia destructivo sólo se relaja en el modo super y según la política de
  aprobación; una skill no lo puede relajar.
- requestedCapabilities de un worker nunca otorga autoridad. El supervisor
  sólo entrega handles para capacidades permitidas, con generación y nonce.
- Si cambia la generación, se revocan los handles anteriores. El handshake del
  worker exige framing llamacode-worker-v1 y nonce válido.
- Un worker con timeout se detiene. En Windows, la sandbox strong se rechaza
  cuando no puede demostrar un límite de red efectivo.
- Las tools deshabilitadas no se ofrecen al modelo ni se guardan en el contexto.
- MCP separa “servidor puede correr” de “sus tools se inyectan”; la anotación de
  seguridad faltante se trata conservadoramente.

### Ciclo de vida resumido

1. Se resuelve definición/perfil, herencia y fase.
2. Se valida engine y se construye el backend adecuado.
3. Se calcula el catálogo efectivo de tools, permisos, MCP y skills.
4. skill_list expone metadata; skill_load carga instrucciones sólo si la
   tarea lo justifica.
5. El loop aplica presupuesto, compaction, watchdogs, approval y guardias.
6. Los workers reciben handshake/capabilities; una denegación o revocación
   impide la llamada.
7. Al finalizar se consolidan memoria/knowledge según la spec y se cierra la
   sesión; las sesiones persistentes y las de Tasks tienen ciclos separados.

## 8. Triggers, workflows y automatización

TriggerManager admite filesystem, webhook, appEvent y manual. Cada trigger
tiene enabled, configuración, taskId y debounce de hasta 600000 ms. Un trigger
deshabilitado no dispara; para filesystem requiere watcher y para web/app event
una coincidencia de ruta, clave, nombre y filtros.

Los workflows de Tasks son investigate, qa, document-audit, review, autoprompt
y release-check. Los modos de perfil son normal, investigation, guarded y
production. release-check no hace commit/push por sí solo: exige aprobación y
gates. autoprompt usa LC_GATE y permite hasta tres reparaciones antes de quedar
en FAIL o BLOCKED.

Esto es diferente de los hooks de Codex/Claude: un trigger inicia trabajo por
evento de producto, mientras un hook intercepta un evento del ciclo de la
sesión o de una tool (PreToolUse, PostToolUse, SessionStart, etc.).

## 9. Comparación Codex ↔ LlamaCode

| Concepto | Codex | LlamaCode |
|---|---|---|
| Instrucciones durables | AGENTS.md, configuración de proyecto | AGENTS.md, perfiles y prompt de HarnessSpec |
| Skills | manifests globales/proyecto/plugin, activación por match | PortableSkillStore, include/exclude y carga por skill_load |
| Plugins | unidad instalable que empaqueta skills, MCP y workflows | adaptadores/backends y skills bundled; no equivale uno a uno |
| MCP | STDIO/HTTP, server y tool policy separados | MCP JSON-RPC con gates, hashes, idempotencia y seguridad conservadora |
| Subagentes | built-ins/custom TOML y delegación aislada | task/SubAgentRunner, rooms, router, master y worktrees |
| Hooks | interceptan ciclo de sesión/tools y pueden denegar/rewrite | triggers de Tasks, guards de permisos y hooks internos del backend |
| Automatización de escritorio | skill/conector computer-use o browser según superficie | tools desktop, Browser Teach y preset agent-rpa |
| Estado/memoria | depende de la superficie/configuración de Codex | MemoryStore, GraphStore, ProjectBrain y event log propios |

No hay una equivalencia directa entre skill de Codex y skill portable de
LlamaCode: se cargan en runtimes diferentes, con contratos y permisos
distintos. Tampoco una skill de Codex activa automáticamente una tool nativa de
LlamaCode.

## 10. Hallazgos, diferencias y deuda documental

1. El inventario anterior decía seis skills científicas; el contenido actual es
   siete porque también incluye autoprompt-coding. docs/skills.md y el README
   deben usar siete.
2. .agents no aporta agentes declarativos hoy. Las definiciones de usuario
   viven en AppLocalData, no en el repo.
3. .claude es una superficie separada. Su gate, /coverage-loop, hooks y
   permisos no se activan dentro de Codex ni del agente nativo.
4. Hay 66 manifests de Codex local/plugin en disco, pero sólo los plugins
   habilitados y el catálogo expuesto por el runtime están disponibles en una
   sesión; caché, instalación y activación son estados distintos.
5. docs/agent.md y docs/plan_harness.md todavía usan CustomBackend y describen
   OpenCode como default histórico. docs/harness.md es la referencia vigente y
   llama al backend nativo LlamaAgentBackend.
6. aider y goose permanecen como referencias históricas; no deben presentarse
   como backends actuales sin implementar su adapter.
7. pi y smallcode existen como adapters genéricos secundarios, pero están
   ocultos del selector principal y no consumen HarnessSpec.

## 11. Fuentes para mantener el inventario

### Código y documentación del repo

- src/core/profiles/HarnessSpec.h y HarnessSpec.cpp
- src/core/profiles/ProfileTypes.cpp
- src/core/profiles/HarnessEngine.cpp
- src/core/agent/LlamaAgentBackend.*
- src/core/agent/HarnessWorkerProtocol.*
- src/core/agent/AgentDefinitionStore.*
- src/core/agent/AgentRoomStore.*
- src/core/agent/TriggerManager.*
- docs/harness.md (referencia técnica vigente)
- docs/skills.md (contrato de skills portables)
- docs/agent-workflows.md (workflows de Tasks)

### Superficie Codex auditada

- C:\Users\cristian\.codex\AGENTS.md
- C:\Users\cristian\.codex\config.toml
- C:\Users\cristian\.codex\skills\
- C:\Users\cristian\.codex\plugins\cache\
- C:\Users\cristian\.codex\agents\ (ausente)

La documentación oficial que define las capas de personalización, skills,
plugins, MCP, hooks y subagentes es [Personalización de Codex](https://learn.chatgpt.com/docs/customization/overview),
[Subagents](https://learn.chatgpt.com/docs/agent-configuration/subagents),
[Hooks](https://learn.chatgpt.com/docs/hooks),
[MCP](https://learn.chatgpt.com/docs/extend/mcp) y
[Build plugins](https://learn.chatgpt.com/docs/build-plugins).
