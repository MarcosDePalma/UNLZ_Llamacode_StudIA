import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Test del editor del harness modular (parte QML). Corre con el runtime `qml` en
// offscreen (ver add_test qml_harness_editor en CMakeLists.txt); LcHarnessEditor
// es el archivo REAL, copiado al lado de los stubs de Theme/App/Lc* para que
// resuelva sus nombres.
//
// Lo que se cubre es la lógica de edición del spec, que es donde una regresión
// silenciosa cuesta un perfil: clonar mal el objeto borra módulos enteros.
// Sale 0 si todo pasa, 1 si algo falla.
ApplicationWindow {
    id: win
    width: 900; height: 700
    visible: true

    property int fails: 0
    function check(cond, msg) {
        console.log((cond ? "  PASS " : "  FAIL ") + msg)
        if (!cond) win.fails++
    }

    property int editedCount: 0
    property int saveCount: 0
    property var lastDirectiveSave: null
    property var lastDirectiveRemove: null
    property string lastDirectiveOpen: ""

    LcHarnessEditor {
        id: editor
        anchors.fill: parent
        profileId: "ap-1"
        packs: [
            { key: "core", name: "Core", description: "archivos + shell" },
            { key: "rag",  name: "RAG",  description: "busqueda semantica" }
        ]
        skills: [
            { name: "autoprompt-coding", description: "loop de coding" },
            { name: "literature-review", description: "revision bibliografica" }
        ]
        directives: [
            { name: "mis-convenciones", description: "convenciones", when: "" }
        ]
        parents: [
            { profileId: "", name: "(defaults del harness)" },
            { profileId: "agent-avanzado", name: "Avanzado" }
        ]
        summary: ({ toolCount: 7, approxTokens: 640, promptChars: 800, warnings: [] })
        diff: []
        onSpecEdited: function (s) { win.editedCount++ }
        onSaveRequested: win.saveCount++
        onDirectiveSaveRequested: function (name, description, when, body, scope) {
            win.lastDirectiveSave = { name: name, description: description,
                                      when: when, body: body, scope: scope }
        }
        onDirectiveRemoveRequested: function (name, scope) {
            win.lastDirectiveRemove = { name: name, scope: scope }
        }
        // El caller resuelve el cuerpo (el catalogo es solo metadata) y responde
        // con editDirective: eso es lo que hace SettingsPage con App.harnessDirective.
        onDirectiveOpenRequested: function (name) {
            win.lastDirectiveOpen = name
            editor.editDirective({ name: name, description: "convenciones",
                                   when: "project.hasGit", body: "no romper",
                                   scope: "project" })
        }
    }

    function findChild(objName) {
        function walk(item) {
            if (item.objectName === objName) return item
            for (var i = 0; i < item.children.length; ++i) {
                var found = walk(item.children[i])
                if (found) return found
            }
            return null
        }
        return walk(editor)
    }

    Component.onCompleted: {
        // --- Toggle de pack: sólo toca tools.packs ---------------------------
        editor.spec = { loop: { sameCallLimit: 2 }, context: { keepLastImages: 0 } }
        var editsBefore = win.editedCount
        editor.specToggleListItem("tools", "packs", "core", true)
        check(editor.specHasListItem("tools", "packs", "core"), "el pack queda marcado")
        check(win.editedCount === editsBefore + 1, "togglear emite specEdited una vez")
        check(editor.specValue("loop", "sameCallLimit", 99) === 2,
              "el toggle NO pisa loop: " + editor.specValue("loop", "sameCallLimit", 99))
        check(editor.specValue("context", "keepLastImages", 99) === 0,
              "el toggle NO pisa context")

        editor.specToggleListItem("tools", "packs", "core", false)
        check(!editor.specHasListItem("tools", "packs", "core"), "destildar saca el pack")
        check((editor.specModule("tools").packs || []).length === 0, "queda la lista vacia")

        // --- Skills: el harness puede dejar una sola activa ------------------
        check(editor.skillIsOn("autoprompt-coding"), "sin modulo todas las skills están activas")
        editor.setSkillOn("autoprompt-coding", false)
        check(!editor.skillIsOn("autoprompt-coding"), "desactivar skill agrega una denylist")
        check(editor.skillIsOn("literature-review"), "desactivar una skill no apaga las demás")
        editor.setSkillOn("autoprompt-coding", true)
        check(editor.skillIsOn("autoprompt-coding"), "la skill vuelve a activarse")

        // --- Editar un número no borra otros módulos -------------------------
        editor.specSet("loop", "transportRetries", 12)
        check(editor.specValue("loop", "sameCallLimit", 99) === 2,
              "editar transportRetries conserva sameCallLimit")
        check(editor.specValue("context", "keepLastImages", 99) === 0,
              "editar loop conserva context (regresion del copiar-objeto-entero)")
        check(editor.specValue("loop", "transportRetries", 0) === 12, "el valor nuevo entro")

        // El objeto que nos pasaron no se muta en el lugar: specSet clona.
        var external = { loop: { sameCallLimit: 5 } }
        editor.spec = external
        editor.specSet("loop", "sameCallLimit", 9)
        check(external.loop.sameCallLimit === 5,
              "specSet NO muta el objeto del caller: " + external.loop.sameCallLimit)

        // --- readOnly: un preset de sistema no se edita ----------------------
        editor.readOnly = true
        var editsRO = win.editedCount
        var okSet = editor.specSet("loop", "sameCallLimit", 42)
        check(!okSet, "readOnly rechaza specSet")
        check(editor.specValue("loop", "sameCallLimit", 0) === 9, "el spec no cambio en readOnly")
        check(win.editedCount === editsRO, "readOnly no emite specEdited")
        check(editor.applySpecJson('{"loop":{"credits":3}}').length > 0,
              "readOnly rechaza el import")
        editor.readOnly = false

        // --- Import de JSON: invalido NO pisa el spec bueno ------------------
        editor.spec = { loop: { credits: 7 } }
        var err = editor.applySpecJson("{ esto no es json ")
        check(err.length > 0, "JSON roto devuelve error: " + err)
        check(editor.specValue("loop", "credits", 0) === 7,
              "un JSON roto no pisa el spec bueno")
        err = editor.applySpecJson('["no","es","un","objeto"]')
        check(err.length > 0, "un array no es un spec valido")
        check(editor.specValue("loop", "credits", 0) === 7, "el array tampoco pisa nada")
        err = editor.applySpecJson('{"context":{"keepLastImages":3}}')
        check(err.length === 0, "un JSON valido entra sin error")
        check(editor.specValue("context", "keepLastImages", 0) === 3, "y reemplaza el spec")
        check(editor.exportSpecJson().indexOf("keepLastImages") > 0,
              "exportar devuelve el spec actual")

        // --- Diff: el contador coincide con las filas ------------------------
        editor.diff = [
            { module: "loop", field: "sameCallLimit", base: 3, value: 2 },
            { module: "context", field: "keepLastImages", base: 1, value: 0 }
        ]
        var header = findChild("diffHeader")
        check(header !== null, "el encabezado del diff existe")
        check(header.text.indexOf("2 ajuste") >= 0, "el contador dice 2: " + header.text)
        var diffCol = findChild("diffColumn")
        check(diffCol !== null && diffCol.visible !== undefined, "la caja del diff se instancia")

        // --- Presupuesto: tools + prompt, no sólo tools ----------------------
        var budget = findChild("budgetLabel")
        check(budget !== null, "la etiqueta de presupuesto existe")
        check(budget.text.indexOf("7 tools") >= 0, "muestra las tools: " + budget.text)
        check(budget.text.indexOf("640") >= 0, "muestra los tokens de schemas")
        check(budget.text.indexOf("200") >= 0,
              "muestra el prompt estimado (800 chars ~ 200 tok): " + budget.text)

        // --- extends: se lee del combo, no del spec --------------------------
        editor.setExtends("agent-avanzado")
        check(editor.currentExtends() === "agent-avanzado",
              "setExtends/currentExtends hacen round-trip: " + editor.currentExtends())
        editor.setExtends("no-existe")
        check(editor.currentExtends() === "", "un padre inexistente cae a 'sin herencia'")

        // --- Directivas: el camino del USUARIO ------------------------------
        // Antes esto se probaba llamando editDirective() a mano, y el test pasaba
        // mientras la funcion era inalcanzable desde la UI. Ahora se entra por el
        // control real: el lapiz al lado de cada chip.
        var dirEditor = findChild("directiveEditor")
        check(dirEditor !== null, "el editor de directivas existe")
        check(!dirEditor.visible, "arranca cerrado")

        var pencil = findChild("directiveEdit_mis-convenciones")
        check(pencil !== null, "cada directiva tiene su boton de editar")
        var chip = findChild("directiveChip_mis-convenciones")
        check(chip !== null, "y su chip de seleccion")
        pencil.clicked()
        check(win.lastDirectiveOpen === "mis-convenciones",
              "el lapiz pide abrir esa directiva: " + win.lastDirectiveOpen)
        check(dirEditor.visible, "y el editor queda abierto")
        check(findChild("dirName").text === "mis-convenciones", "carga el nombre")
        check(findChild("dirBody").text === "no romper", "carga el cuerpo")
        check(findChild("dirWhen").text === "project.hasGit", "carga la condicion")

        // Con una directiva abierta, borrar sale por senal con su scope.
        dirEditor.removeCurrent()
        check(win.lastDirectiveRemove !== null, "borrar emite la senal")
        check(win.lastDirectiveRemove.name === "mis-convenciones", "con el nombre correcto")
        check(win.lastDirectiveRemove.scope === "project", "y el scope de donde vino")

        // El chip NO abre el editor: sigue siendo el toggle de seleccion.
        var editsBeforeChip = win.editedCount
        chip.clicked()
        check(win.editedCount === editsBeforeChip + 1, "el chip togglea la seleccion")

        // Los hechos de `when` se listan para no adivinarlos.
        editor.directiveFacts = ["vision", "project.hasGit"]
        var factsLabel = findChild("directiveFactsLabel")
        check(factsLabel !== null && factsLabel.text.indexOf("project.hasGit") > 0,
              "la UI enumera los hechos disponibles")

        // --- Memoria y Chat: los modulos nuevos editan su propio modulo ------
        editor.spec = { loop: { credits: 4 } }
        var factsField = findChild("structuredFactsField")
        check(factsField !== null, "el campo de hechos de memoria existe")
        check(factsField.text === "12", "arranca en el default historico: " + factsField.text)
        editor.specSet("memory", "structuredFacts", 3)
        check(editor.specValue("memory", "structuredFacts", 0) === 3, "el valor entra")
        check(editor.specValue("loop", "credits", 0) === 4, "editar memory no pisa loop")

        var projSwitch = findChild("projectMemorySwitch")
        check(projSwitch !== null && projSwitch.checked === true,
              "la memoria de proyecto arranca encendida")

        var knowledgeSwitch = findChild("knowledgeEnabledSwitch")
        check(knowledgeSwitch !== null && knowledgeSwitch.checked === false,
              "knowledge arranca apagado para no cambiar el contexto por defecto")
        editor.specSet("knowledge", "enabled", true)
        editor.specSet("knowledge", "preflight", true)
        editor.specSet("knowledge", "maxEdges", 6)
        check(editor.specValue("knowledge", "enabled", false) === true,
              "knowledge.enabled entra en su modulo")
        check(editor.specValue("knowledge", "preflight", false) === true,
              "knowledge.preflight entra en su modulo")
        check(editor.specValue("knowledge", "maxEdges", 0) === 6,
              "knowledge.maxEdges entra en su modulo")
        check(editor.specValue("loop", "credits", 0) === 4,
              "editar knowledge no pisa loop")

        // --- Lane externa: queda configurada por perfil y no por globals -----
        editor.spec = { memory: { structuredFacts: 3 },
                        knowledge: { enabled: true, preflight: true, maxEdges: 6 },
                        worker: { lane: "node", entrypoint: "worker.mjs",
                                  sandbox: "process", allowNetwork: false,
                                  requestedCapabilities: ["fs.read"] } }
        var workerLane = findChild("workerLaneCombo")
        check(workerLane !== null && workerLane.currentValue === "node",
              "el editor refleja la lane Node del worker")
        var workerEntry = findChild("workerEntrypointField")
        check(workerEntry !== null && workerEntry.text === "worker.mjs",
              "el entrypoint del worker se refleja")
        var workerCaps = findChild("workerCapabilitiesField")
        check(workerCaps !== null && workerCaps.text === "fs.read",
              "las capabilities del worker se reflejan")
        editor.specSet("worker", "callTimeoutMs", 5000)
        check(editor.specValue("worker", "callTimeoutMs", 0) === 5000,
              "el timeout del worker queda en su módulo")
        check(editor.specValue("worker", "entrypoint", "") === "worker.mjs",
              "editar el timeout no pisa el entrypoint")

        editor.specSet("chat", "systemExtra", "una linea")
        check(editor.specValue("chat", "systemExtra", "") === "una linea", "chat.systemExtra entra")
        check(editor.specValue("memory", "structuredFacts", 0) === 3,
              "editar chat no pisa memory")
        var chatExtra = findChild("chatSystemExtraField")
        check(chatExtra !== null && chatExtra.text === "una linea",
              "el textarea del chat refleja el spec")

        // readOnly tambien cubre los modulos nuevos.
        editor.readOnly = true
        check(!editor.specSet("memory", "structuredFacts", 99), "readOnly frena memory")
        check(!editor.specSet("knowledge", "maxEdges", 99), "readOnly frena knowledge")
        check(!editor.specSet("chat", "thinking", true), "readOnly frena chat")
        editor.readOnly = false

        // --- Guardar: el caller decide qué hacer -----------------------------
        var savesBefore = win.saveCount
        editor.saveRequested()
        check(win.saveCount === savesBefore + 1, "saveRequested llega al caller")

        console.log(win.fails === 0 ? "TODO OK" : (win.fails + " FALLAS"))
        Qt.exit(win.fails === 0 ? 0 : 1)
    }
}
