import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// LcHarnessEditor — editor del HARNESS MODULAR de un perfil de agente.
//
// Un perfil deja de ser un preset cerrado: acá se componen sus módulos
// (tools/prompt/loop/contexto/permisos/escalación/protocolo). Lo que no se toca
// se hereda del `extends`; lo que se toca aparece en el diff. Ver docs/harness.md.
//
// El componente NO conoce a App: todo entra por propiedades y sale por señales.
// Esa es la razón de existir del archivo — dentro de SettingsPage.qml esta
// lógica no se podía testear (la página depende de context properties de C++).
// Los helpers spec* son la lógica real y viven acá, con el componente.
Item {
    id: root

    // --- Entrada -----------------------------------------------------------
    property string profileId: ""
    property bool   readOnly: false          // preset de sistema = sólo lectura
    property var    spec: ({})               // HarnessSpec resuelto (JSON editable)
    property var    summary: ({})            // {toolCount, skillCount, approxTokens, promptChars, warnings}
    property var    diff: []                 // [{module, field, base, value}]
    property var    packs: []                // catálogo de packs de tools
    property var    skills: []               // catálogo de skills portables
    property var    directives: []           // directivas .md descubiertas
    property var    engines: []              // catálogo de contratos legacy/next
    property var    parents: []              // [{profileId, name}] candidatos a `extends`
    // Hechos disponibles para el gate `when` de una directiva. Se muestran en la
    // UI: sin esto el usuario tiene que adivinarlos.
    property var    directiveFacts: ["tools.desktop", "tools.shell", "tools.web",
                                     "tools.task", "vision", "super", "project.hasGit"]
    // Los módulos sólo los aplica el agente nativo; con otro backend el editor
    // estaría prometiendo algo que no pasa (ver F5 en docs/plan-harness-cierre.md).
    property bool   specApplies: true
    property string specAppliesNote: ""

    // --- Salida ------------------------------------------------------------
    signal specEdited(var spec)              // el usuario cambió algo (no persiste)
    signal saveRequested()
    signal directiveSaveRequested(string name, string description, string when,
                                  string body, string scope)
    signal directiveRemoveRequested(string name, string scope)
    // Pedido de abrir una directiva existente. El componente no lee del disco:
    // el caller resuelve el cuerpo y responde llamando a editDirective().
    signal directiveOpenRequested(string name)

    implicitHeight: col.implicitHeight
    implicitWidth: col.implicitWidth

    // --- Helpers de spec (puros; son lo que testea tst_harness_editor) ------
    // Regla: SIEMPRE clonar antes de tocar. Mutar `spec` en el lugar no dispara
    // los bindings de QML y, peor, pisaría el objeto que nos pasó el caller.
    function specModule(name) {
        return (spec && spec[name]) ? spec[name] : ({})
    }
    function specValue(moduleName, field, fallback) {
        var m = specModule(moduleName)
        return (m[field] === undefined) ? fallback : m[field]
    }
    function specSet(moduleName, field, value) {
        if (readOnly) return false
        var s = JSON.parse(JSON.stringify(spec || {}))
        if (!s[moduleName]) s[moduleName] = {}
        s[moduleName][field] = value
        spec = s
        specEdited(s)
        return true
    }
    function specHasListItem(moduleName, field, item) {
        return (specModule(moduleName)[field] || []).indexOf(item) >= 0
    }
    function specToggleListItem(moduleName, field, item, on) {
        var arr = (specModule(moduleName)[field] || []).slice()
        var i = arr.indexOf(item)
        if (on && i < 0) arr.push(item)
        else if (!on && i >= 0) arr.splice(i, 1)
        return specSet(moduleName, field, arr)
    }
    function skillIsOn(name) {
        var m = specModule("skills")
        var include = m.include || []
        var exclude = m.exclude || []
        if (exclude.indexOf(name) >= 0) return false
        return Object.keys(m).length === 0
            || include.indexOf("*") >= 0
            || include.indexOf(name) >= 0
    }
    function setSkillOn(name, on) {
        if (readOnly) return false
        var s = JSON.parse(JSON.stringify(spec || {}))
        var hadPolicy = s.skills && Object.keys(s.skills).length > 0
        // Sin módulo declarado, el estado histórico es "todas activas". Para
        // apagar una sola hay que materializar esa intención como "* menos X";
        // un objeto vacío declarado significa, en cambio, "ninguna".
        if (!hadPolicy) {
            if (on) return true
            s.skills = { include: ["*"], exclude: [name] }
            spec = s
            specEdited(s)
            return true
        }
        var include = (s.skills.include || []).slice()
        var exclude = (s.skills.exclude || []).slice()
        var i
        if (include.indexOf("*") >= 0) {
            i = exclude.indexOf(name)
            if (on && i >= 0) exclude.splice(i, 1)
            else if (!on && i < 0) exclude.push(name)
        } else {
            i = include.indexOf(name)
            if (on && i < 0) include.push(name)
            else if (!on && i >= 0) include.splice(i, 1)
        }
        s.skills.include = include
        s.skills.exclude = exclude
        spec = s
        specEdited(s)
        return true
    }
    function enableAllSkills() {
        if (readOnly) return false
        var s = JSON.parse(JSON.stringify(spec || {}))
        s.skills = { include: ["*"], exclude: [] }
        spec = s
        specEdited(s)
        return true
    }
    function setRuntimeEngine(id, version) {
        if (readOnly) return false
        var s = JSON.parse(JSON.stringify(spec || {}))
        if (!s.runtime) s.runtime = {}
        s.runtime.engine = id
        s.runtime.version = version || 1
        s.runtime.fallbackEngine = "legacy"
        s.runtime.experimental = id === "next"
        spec = s
        specEdited(s)
        return true
    }
    // Import de un spec pegado a mano. Devuelve "" si entró; el mensaje de error
    // si no. Un JSON roto NO pisa el spec bueno: es la diferencia entre un typo y
    // perder el perfil.
    function applySpecJson(text) {
        if (readOnly) return "perfil de sistema: sólo lectura"
        try {
            var parsed = JSON.parse(text)
            if (typeof parsed !== "object" || parsed === null || Array.isArray(parsed))
                return "el JSON debe ser un objeto"
            spec = parsed
            specEdited(parsed)
            return ""
        } catch (e) {
            return String(e)
        }
    }
    function exportSpecJson() { return JSON.stringify(spec || {}, null, 2) }

    ColumnLayout {
        id: col
        anchors.fill: parent
        spacing: 12

        Text {
            text: "HARNESS MODULAR"
            color: Theme.accent; font.pixelSize: 11; font.bold: true
        }

        // Aviso cuando el backend activo no consume el spec: el editor no debe
        // prometer lo que no se aplica.
        Rectangle {
            Layout.fillWidth: true
            visible: !root.specApplies && root.specAppliesNote.length > 0
            color: Theme.inputBg; border.color: Theme.warnText; radius: 8
            implicitHeight: appliesNote.implicitHeight + 16
            Text {
                id: appliesNote
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 10 }
                text: root.specAppliesNote
                color: Theme.warnText; font.pixelSize: 11; wrapMode: Text.WordWrap
            }
        }

        RowLayout {
            Layout.fillWidth: true; spacing: 8
            Text { text: "Hereda de"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcComboBox {
                id: extendsCombo
                objectName: "extendsCombo"
                Layout.fillWidth: true
                enabled: !root.readOnly
                textRole: "name"; valueRole: "profileId"
                model: root.parents
                // `extends` no vive en un módulo: se lee con currentExtends() al
                // guardar. Acá sólo avisamos que hubo edición (refresca el diff).
                onActivated: root.specEdited(root.spec)
                background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                contentItem: Text {
                    text: extendsCombo.displayText || "(defaults del harness)"
                    color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 10
                    verticalAlignment: Text.AlignVCenter
                }
            }
            // Presupuesto de contexto COMPLETO: schemas de tools + system prompt.
            // Mostrar sólo las tools era media foto.
            Text {
                objectName: "budgetLabel"
                text: (root.summary.toolCount || 0) + " tools · "
                      + (root.summary.skillCount || 0) + " skills · ~"
                      + (root.summary.approxTokens || 0) + " tok schemas · ~"
                      + Math.round((root.summary.promptChars || 0) / 4) + " tok prompt"
                color: Theme.textMuted; font.pixelSize: 11
            }
            LcButton {
                text: "Guardar harness"; secondary: true
                enabled: !root.readOnly && root.profileId.length > 0
                onClicked: root.saveRequested()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.engines.length > 0
            Text { text: "Motor del harness"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcComboBox {
                id: engineCombo
                objectName: "engineCombo"
                Layout.fillWidth: true
                enabled: !root.readOnly
                textRole: "name"; valueRole: "id"
                model: root.engines
                currentIndex: Math.max(0, indexOfValue(
                    root.specValue("runtime", "engine", "legacy")))
                onActivated: {
                    var selected = root.engines[currentIndex] || ({})
                    root.setRuntimeEngine(currentValue || "legacy", selected.version || 1)
                }
                background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                contentItem: Text {
                    text: engineCombo.displayText || "Legacy"
                    color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 10
                    verticalAlignment: Text.AlignVCenter
                }
                ToolTip.visible: hovered
                ToolTip.text: {
                    var selected = root.engines[currentIndex] || ({})
                    return selected.description || "Contrato de ejecución del harness"
                }
            }
            Text {
                text: root.specValue("runtime", "engine", "legacy") === "next"
                      ? "experimental · sesiones separadas" : "compatibilidad histórica"
                color: root.specValue("runtime", "engine", "legacy") === "next"
                       ? Theme.warnText : Theme.textMuted
                font.pixelSize: 11
            }
        }

        // Lane externa opcional. builtin conserva el loop histórico; Node/Python
        // sólo se activa cuando el perfil lo declara explícitamente.
        Rectangle {
            Layout.fillWidth: true
            color: Theme.inputBg; border.color: Theme.borderColor; radius: 8
            implicitHeight: workerCol.implicitHeight + 20
            ColumnLayout {
                id: workerCol
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                spacing: 7
                Text {
                    text: "Worker externo (opcional)"
                    color: Theme.textSecondary; font.pixelSize: 12; font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: "Builtin mantiene el flujo actual. Node/Python ejecuta un plugin "
                          + "supervisado y expone worker_call con aprobación."
                    color: Theme.textMuted; font.pixelSize: 11; wrapMode: Text.WordWrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "Lane"; color: Theme.textSecondary; font.pixelSize: 12 }
                    LcComboBox {
                        id: workerLaneCombo
                        objectName: "workerLaneCombo"
                        Layout.fillWidth: true; enabled: !root.readOnly
                        textRole: "label"; valueRole: "key"
                        model: [
                            { key: "builtin", label: "Integrado (legacy)" },
                            { key: "node", label: "Node.js / TypeScript" },
                            { key: "python", label: "CPython" }
                        ]
                        currentIndex: Math.max(0, indexOfValue(
                            root.specValue("worker", "lane", "builtin")))
                        onActivated: root.specSet("worker", "lane", currentValue)
                        background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                        contentItem: Text {
                            text: workerLaneCombo.displayText || "Integrado (legacy)"
                            color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 10
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
                GridLayout {
                    Layout.fillWidth: true
                    visible: root.specValue("worker", "lane", "builtin") !== "builtin"
                    columns: 2; columnSpacing: 10; rowSpacing: 6
                    Text { text: "Entrypoint"; color: Theme.textSecondary; font.pixelSize: 12 }
                    LcTextField {
                        objectName: "workerEntrypointField"
                        Layout.fillWidth: true; placeholderText: "worker.mjs / worker.py"
                        text: String(root.specValue("worker", "entrypoint", ""))
                        onEditingFinished: root.specSet("worker", "entrypoint", text.trim())
                    }
                    Text { text: "Sandbox OS"; color: Theme.textSecondary; font.pixelSize: 12 }
                    LcComboBox {
                        id: workerSandboxCombo
                        objectName: "workerSandboxCombo"
                        Layout.fillWidth: true; enabled: !root.readOnly
                        textRole: "label"; valueRole: "key"
                        model: [
                            { key: "none", label: "Sin sandbox OS" },
                            { key: "process", label: "Proceso supervisado" },
                            { key: "strong", label: "Strong (bubblewrap / Unix)" }
                        ]
                        currentIndex: Math.max(0, indexOfValue(
                            root.specValue("worker", "sandbox", "none")))
                        onActivated: root.specSet("worker", "sandbox", currentValue)
                        background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                        contentItem: Text {
                            text: workerSandboxCombo.displayText || "Sin sandbox OS"
                            color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 10
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    Text { text: "Red del worker"; color: Theme.textSecondary; font.pixelSize: 12 }
                    LcSwitch {
                        objectName: "workerNetworkSwitch"
                        enabled: !root.readOnly
                        checked: root.specValue("worker", "allowNetwork", false)
                        onToggled: root.specSet("worker", "allowNetwork", checked)
                    }
                    Text { text: "Capabilities read-only"; color: Theme.textSecondary; font.pixelSize: 12 }
                    LcTextField {
                        objectName: "workerCapabilitiesField"
                        Layout.fillWidth: true; placeholderText: "fs.read, graph.read"
                        text: (root.specValue("worker", "requestedCapabilities", []) || []).join(", ")
                        onEditingFinished: root.specSet("worker", "requestedCapabilities",
                            text.split(",").map(function(v) { return v.trim() }).filter(function(v) { return v.length > 0 }))
                    }
                    Text { text: "Timeout de llamada (ms)"; color: Theme.textSecondary; font.pixelSize: 12 }
                    LcTextField {
                        objectName: "workerCallTimeoutField"
                        Layout.fillWidth: true; placeholderText: "120000"
                        text: String(root.specValue("worker", "callTimeoutMs", 120000))
                        onEditingFinished: root.specSet("worker", "callTimeoutMs",
                            Math.max(100, parseInt(text) || 120000))
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: root.specValue("worker", "lane", "builtin") !== "builtin"
                    text: "Strong se rechaza explícitamente en Windows si no hay un boundary "
                          + "OS compatible; el perfil no cae silenciosamente a builtin."
                    color: Theme.warnText; font.pixelSize: 11; wrapMode: Text.WordWrap
                }
            }
        }

        // Advertencias de dependencias (git/embeddings/escritorio/correo/MCP) y
        // de directivas rotas. No bloquean: informan y dicen qué hacer.
        Rectangle {
            Layout.fillWidth: true
            visible: (root.summary.warnings || []).length > 0
            color: Theme.inputBg; border.color: Theme.warnText; radius: 8
            implicitHeight: warnCol.implicitHeight + 16
            ColumnLayout {
                id: warnCol
                objectName: "warningsColumn"
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
                spacing: 2
                Repeater {
                    model: root.summary.warnings || []
                    delegate: Text {
                        required property var modelData
                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                        text: "⚠ " + modelData
                        color: Theme.warnText; font.pixelSize: 11
                    }
                }
            }
        }

        // Diff contra el perfil del que hereda: lo que hace mantenible un perfil
        // propio ("cambia 6 cosas vs Avanzado").
        Rectangle {
            Layout.fillWidth: true
            visible: (root.diff || []).length > 0
            color: Theme.inputBg; border.color: Theme.borderColor; radius: 8
            implicitHeight: diffCol.implicitHeight + 16
            ColumnLayout {
                id: diffCol
                objectName: "diffColumn"
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
                spacing: 2
                Text {
                    objectName: "diffHeader"
                    text: "Cambia " + (root.diff || []).length + " ajuste(s) respecto de "
                          + (extendsCombo.displayText || "los defaults")
                    color: Theme.textSecondary; font.pixelSize: 11; font.bold: true
                }
                Repeater {
                    objectName: "diffRepeater"
                    model: root.diff || []
                    delegate: Text {
                        required property var modelData
                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                        text: modelData.module + "." + modelData.field + ": "
                              + modelData.base + " → " + modelData.value
                        color: Theme.textMuted; font.pixelSize: 11
                    }
                }
            }
        }

        // Packs de tools (grupos + compuestos). `exclude` gana siempre.
        Text { text: "Packs de tools"; color: Theme.textSecondary; font.pixelSize: 12 }
        Flow {
            Layout.fillWidth: true; spacing: 6
            Repeater {
                model: root.packs
                delegate: LcButton {
                    required property var modelData
                    text: (root.specHasListItem("tools", "packs", modelData.key) ? "✓ " : "")
                          + modelData.name
                    secondary: !root.specHasListItem("tools", "packs", modelData.key)
                    enabled: !root.readOnly
                    ToolTip.visible: hovered
                    ToolTip.text: modelData.description
                    onClicked: root.specToggleListItem(
                        "tools", "packs", modelData.key,
                        !root.specHasListItem("tools", "packs", modelData.key))
                }
            }
        }

        // Skills portables: el harness decide qué slugs puede descubrir/cargar.
        RowLayout {
            Layout.fillWidth: true
            Text { text: "Skills portables por harness"; color: Theme.textSecondary; font.pixelSize: 12 }
            Item { Layout.fillWidth: true }
            LcButton {
                text: "Habilitar todas"
                secondary: true
                enabled: !root.readOnly
                onClicked: root.enableAllSkills()
            }
        }
        Text {
            Layout.fillWidth: true
            visible: root.skills.length === 0
            text: "No hay skills portables descubiertas en el alcance actual."
            color: Theme.textMuted; font.pixelSize: 11
        }
        Flow {
            Layout.fillWidth: true; spacing: 6
            visible: root.skills.length > 0
            Repeater {
                model: root.skills
                delegate: LcButton {
                    required property var modelData
                    text: (root.skillIsOn(modelData.name) ? "✓ " : "") + modelData.name
                    secondary: !root.skillIsOn(modelData.name)
                    enabled: !root.readOnly
                    ToolTip.visible: hovered
                    ToolTip.text: modelData.description || "Skill portable"
                    onClicked: root.setSkillOn(modelData.name, !root.skillIsOn(modelData.name))
                }
            }
        }

        // Directivas de usuario (.md). Se eligen acá y se editan abajo.
        RowLayout {
            Layout.fillWidth: true
            Text { text: "Directivas propias (.md)"; color: Theme.textSecondary; font.pixelSize: 12 }
            Item { Layout.fillWidth: true }
            LcButton {
                text: directiveEditor.visible ? "Cerrar editor" : "Nueva directiva"
                secondary: true
                enabled: !root.readOnly
                onClicked: {
                    if (directiveEditor.visible) { directiveEditor.visible = false; return }
                    directiveEditor.loadNew()
                }
            }
        }
        Text {
            Layout.fillWidth: true
            visible: root.directives.length === 0
            wrapMode: Text.WordWrap
            text: "No hay directivas propias todavía. Son archivos .md con instrucciones "
                  + "que se suman al system prompt; creá una con «Nueva directiva»."
            color: Theme.textMuted; font.pixelSize: 11
        }
        Flow {
            Layout.fillWidth: true; spacing: 6
            visible: root.directives.length > 0
            Repeater {
                model: root.directives
                // Dos controles por directiva: el chip la marca/desmarca para el
                // perfil, y el lápiz la ABRE para editar o borrar. Sin el segundo
                // se podían crear directivas y después no tocarlas nunca más.
                delegate: Row {
                    required property var modelData
                    spacing: 2
                    LcButton {
                        objectName: "directiveChip_" + modelData.name
                        text: (root.specHasListItem("prompt", "custom", modelData.name) ? "✓ " : "")
                              + modelData.name
                        secondary: !root.specHasListItem("prompt", "custom", modelData.name)
                        enabled: !root.readOnly
                        ToolTip.visible: hovered
                        ToolTip.text: modelData.description
                                      + (modelData.when ? ("\ncondición: " + modelData.when) : "")
                        onClicked: root.specToggleListItem(
                            "prompt", "custom", modelData.name,
                            !root.specHasListItem("prompt", "custom", modelData.name))
                    }
                    LcButton {
                        objectName: "directiveEdit_" + modelData.name
                        text: "✎"
                        secondary: true
                        enabled: !root.readOnly
                        ToolTip.visible: hovered
                        ToolTip.text: "Editar o borrar esta directiva"
                        // El catálogo no trae el cuerpo (list() es sólo metadata):
                        // el caller lo resuelve y responde con editDirective().
                        onClicked: root.directiveOpenRequested(modelData.name)
                    }
                }
            }
        }

        // Editor de una directiva propia (alta/edición/baja).
        Rectangle {
            id: directiveEditor
            objectName: "directiveEditor"
            Layout.fillWidth: true
            visible: false
            color: Theme.inputBg; border.color: Theme.borderColor; radius: 8
            implicitHeight: dirCol.implicitHeight + 24

            property string editingName: ""

            function loadNew() {
                editingName = ""
                dirName.text = ""; dirDesc.text = ""; dirWhen.text = ""; dirBody.text = ""
                dirScope.currentIndex = 0
                visible = true
            }
            // Baja de la directiva abierta. Existe como funcion (y no solo como
            // onClicked) para que el test entre por el mismo camino que el boton.
            function removeCurrent() {
                if (editingName.length === 0) return false
                root.directiveRemoveRequested(editingName, dirScope.currentValue || "global")
                visible = false
                return true
            }
            function loadExisting(d) {
                editingName = d.name || ""
                dirName.text = editingName
                dirDesc.text = d.description || ""
                dirWhen.text = d.when || ""
                dirBody.text = d.body || ""
                dirScope.currentIndex = Math.max(0, dirScope.indexOfValue(d.scope || "global"))
                visible = true
            }

            ColumnLayout {
                id: dirCol
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
                spacing: 8

                GridLayout {
                    Layout.fillWidth: true; columns: 2; columnSpacing: 10; rowSpacing: 6
                    Text { text: "Nombre (kebab-case)"; color: Theme.textSecondary; font.pixelSize: 12 }
                    LcTextField { id: dirName; objectName: "dirName"; Layout.fillWidth: true; placeholderText: "mis-convenciones" }
                    Text { text: "Descripción"; color: Theme.textSecondary; font.pixelSize: 12 }
                    LcTextField { id: dirDesc; objectName: "dirDesc"; Layout.fillWidth: true; placeholderText: "una línea; la ve el editor" }
                    Text { text: "Condición (when)"; color: Theme.textSecondary; font.pixelSize: 12 }
                    LcTextField { id: dirWhen; objectName: "dirWhen"; Layout.fillWidth: true; placeholderText: "vacío = siempre" }
                    Text { text: "Alcance"; color: Theme.textSecondary; font.pixelSize: 12 }
                    LcComboBox {
                        id: dirScope
                        objectName: "dirScope"
                        Layout.fillWidth: true
                        textRole: "label"; valueRole: "key"
                        model: [{ key: "global", label: "Global (todas las máquinas de este usuario)" },
                                { key: "project", label: "Proyecto (.llamacode/directives)" }]
                        background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                        contentItem: Text { text: dirScope.displayText; color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
                    }
                }
                // Hechos disponibles para `when`: enumerarlos evita que el usuario
                // los adivine (un hecho desconocido NO cumple, así que una
                // directiva mal escrita nunca se inyecta).
                Text {
                    objectName: "directiveFactsLabel"
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                    text: "Hechos para `when` (coma = Y, ! = negación): " + root.directiveFacts.join(", ")
                    color: Theme.textMuted; font.pixelSize: 11
                }
                TextArea {
                    id: dirBody
                    objectName: "dirBody"
                    Layout.fillWidth: true; Layout.minimumHeight: 90
                    wrapMode: TextArea.Wrap; color: Theme.textPrimary
                    placeholderText: "Cuerpo de la directiva: se inyecta tal cual en el system prompt."
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 8
                    LcButton {
                        text: "Guardar directiva"; secondary: true
                        enabled: dirName.text.trim().length > 0 && dirDesc.text.trim().length > 0
                                 && dirBody.text.trim().length > 0
                        onClicked: {
                            root.directiveSaveRequested(dirName.text.trim(), dirDesc.text.trim(),
                                                        dirWhen.text.trim(), dirBody.text,
                                                        dirScope.currentValue || "global")
                            directiveEditor.visible = false
                        }
                    }
                    LcButton {
                        text: "Borrar"; danger: true
                        enabled: directiveEditor.editingName.length > 0
                        onClicked: directiveEditor.removeCurrent()
                    }
                    Item { Layout.fillWidth: true }
                }
            }
        }

        // Módulos numéricos/booleanos. Sin tocar = heredado.
        GridLayout {
            Layout.fillWidth: true; columns: 4
            rowSpacing: 8; columnSpacing: 10
            enabled: !root.readOnly

            Text { text: "Repetición de tool"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcTextField {
                objectName: "sameCallLimitField"
                Layout.fillWidth: true; placeholderText: "3"
                text: String(root.specValue("loop", "sameCallLimit", 3))
                onEditingFinished: root.specSet("loop", "sameCallLimit", parseInt(text) || 3)
                ToolTip.visible: hovered
                ToolTip.text: "Llamadas idénticas consecutivas antes de bloquear y forzar replanteo."
            }
            Text { text: "Reintentos de transporte"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcTextField {
                Layout.fillWidth: true; placeholderText: "60"
                text: String(root.specValue("loop", "transportRetries", 60))
                onEditingFinished: root.specSet("loop", "transportRetries", parseInt(text) || 60)
                ToolTip.visible: hovered
                ToolTip.text: "Reintentos HTTP mientras llama-server reinicia."
            }

            Text { text: "Timeout tool web (s)"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcTextField {
                Layout.fillWidth: true; placeholderText: "180"
                text: String(root.specValue("loop", "webToolTimeoutSec", 180))
                onEditingFinished: root.specSet("loop", "webToolTimeoutSec", parseInt(text) || 180)
            }
            Text { text: "Stream inactivo (s)"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcTextField {
                Layout.fillWidth: true; placeholderText: "0 = env / 3600"
                text: String(root.specValue("loop", "streamIdleTimeoutSec", 0))
                onEditingFinished: root.specSet("loop", "streamIdleTimeoutSec", parseInt(text) || 0)
                ToolTip.visible: hovered
                ToolTip.text: "Aborta sólo si el stream no emite nada. 0 = usar LLAMACODE_STREAM_IDLE_TIMEOUT o 3600s."
            }

            Text { text: "Compactar contexto"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcSwitch {
                checked: root.specValue("context", "compaction", true)
                onToggled: root.specSet("context", "compaction", checked)
            }
            Text { text: "Umbral (frac. de n_ctx)"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcTextField {
                Layout.fillWidth: true; placeholderText: "0.90"
                text: String(root.specValue("context", "compactionTrigger", 0.90))
                onEditingFinished: root.specSet("context", "compactionTrigger", parseFloat(text) || 0.90)
            }

            Text { text: "Poda determinista"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcSwitch {
                checked: root.specValue("context", "prune", true)
                onToggled: root.specSet("context", "prune", checked)
            }
            Text { text: "Capturas a conservar"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcTextField {
                objectName: "keepLastImagesField"
                Layout.fillWidth: true; placeholderText: "1"
                text: String(root.specValue("context", "keepLastImages", 1))
                onEditingFinished: root.specSet("context", "keepLastImages", parseInt(text) || 0)
                ToolTip.visible: hovered
                ToolTip.text: "Con mmproj cada captura son miles de tokens de prefill."
            }

            Text { text: "Dedup de lecturas"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcSwitch {
                checked: root.specValue("context", "readDedup", true)
                onToggled: root.specSet("context", "readDedup", checked)
            }
            Text { text: "Preflight de contexto"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcSwitch {
                checked: root.specValue("context", "preflight", false)
                onToggled: root.specSet("context", "preflight", checked)
                ToolTip.visible: hovered
                ToolTip.text: "Inyecta un slice de archivos candidatos antes del primer request."
            }
            Text { text: "Índice de contexto"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcComboBox {
                id: contextIndexPolicy
                Layout.fillWidth: true
                textRole: "label"; valueRole: "key"
                model: [
                    { key: "off", label: "Desactivado" },
                    { key: "lazy", label: "Lazy (recomendado)" },
                    { key: "eager", label: "Eager" }
                ]
                currentIndex: Math.max(0, indexOfValue(root.specValue("context", "indexPolicy", "lazy")))
                onActivated: root.specSet("context", "indexPolicy", currentValue)
                background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                contentItem: Text { text: contextIndexPolicy.displayText; color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
            }
            Text { text: "Budget del scout (tokens)"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcTextField {
                Layout.fillWidth: true; placeholderText: "700"
                text: String(root.specValue("context", "scoutBudget", 700))
                onEditingFinished: root.specSet("context", "scoutBudget", Math.max(64, parseInt(text) || 700))
            }
            Text { text: "Rangos iniciales (k)"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcTextField {
                Layout.fillWidth: true; placeholderText: "8"
                text: String(root.specValue("context", "scoutK", 8))
                onEditingFinished: root.specSet("context", "scoutK", Math.max(1, Math.min(15, parseInt(text) || 8)))
            }
            Text { text: "Expandir vecinos del grafo"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcSwitch {
                checked: root.specValue("context", "graphExpansion", true)
                onToggled: root.specSet("context", "graphExpansion", checked)
            }

            Text { text: "Knowledge packet automático"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcSwitch {
                objectName: "knowledgeEnabledSwitch"
                checked: root.specValue("knowledge", "enabled", false)
                onToggled: root.specSet("knowledge", "enabled", checked)
                ToolTip.visible: hovered
                ToolTip.text: "Combina memoria y relaciones del grafo con fuentes, sin reemplazar el índice de contexto."
            }
            Text { text: "Knowledge en preflight"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcSwitch {
                checked: root.specValue("knowledge", "preflight", false)
                onToggled: root.specSet("knowledge", "preflight", checked)
                ToolTip.visible: hovered
                ToolTip.text: "Incluye el paquete durable antes del primer request."
            }
            Text { text: "Citar fuentes"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcSwitch {
                checked: root.specValue("knowledge", "citeSources", true)
                onToggled: root.specSet("knowledge", "citeSources", checked)
            }
            Text { text: "Máx. hechos / edges"; color: Theme.textSecondary; font.pixelSize: 12 }
            RowLayout {
                Layout.fillWidth: true; spacing: 6
                LcTextField {
                    Layout.fillWidth: true; placeholderText: "8"
                    text: String(root.specValue("knowledge", "maxFacts", 8))
                    onEditingFinished: root.specSet("knowledge", "maxFacts", Math.max(0, parseInt(text) || 0))
                }
                LcTextField {
                    Layout.fillWidth: true; placeholderText: "12"
                    text: String(root.specValue("knowledge", "maxEdges", 12))
                    onEditingFinished: root.specSet("knowledge", "maxEdges", Math.max(0, parseInt(text) || 0))
                }
            }
            Text { text: "Tope knowledge (chars)"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcTextField {
                Layout.fillWidth: true; placeholderText: "12000"
                text: String(root.specValue("knowledge", "maxChars", 12000))
                onEditingFinished: root.specSet("knowledge", "maxChars", Math.max(512, parseInt(text) || 12000))
            }

            Text { text: "Protocolo de tools"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcComboBox {
                id: protocolCombo
                Layout.fillWidth: true
                textRole: "label"; valueRole: "key"
                model: [
                    { key: "auto",   label: "Auto (nativo + fallback texto)" },
                    { key: "native", label: "Nativo (sin fallback)" },
                    { key: "text",   label: "Texto (TOOL_CALL)" }
                ]
                currentIndex: Math.max(0, indexOfValue(root.specValue("protocol", "toolProtocol", "auto")))
                onActivated: root.specSet("protocol", "toolProtocol", currentValue)
                background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                contentItem: Text { text: protocolCombo.displayText; color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
            }
            Text { text: "Sub-agentes en paralelo"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcTextField {
                Layout.fillWidth: true; placeholderText: "5"
                text: String(root.specValue("escalation", "maxParallelSubagents", 5))
                onEditingFinished: root.specSet("escalation", "maxParallelSubagents", parseInt(text) || 5)
                ToolTip.visible: hovered
                ToolTip.text: "Techo propio; el cap real es el menor entre esto, el adaptativo por VRAM y 5."
            }

            Text { text: "Aislar sub-agentes"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcSwitch {
                checked: root.specValue("escalation", "isolateSubagents", true)
                onToggled: root.specSet("escalation", "isolateSubagents", checked)
                ToolTip.visible: hovered
                ToolTip.text: "Cada sub-agente en su git worktree. Apagado: comparten el cwd."
            }
            Text { text: "Tope de prompt (chars)"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcTextField {
                Layout.fillWidth: true; placeholderText: "24000"
                text: String(root.specValue("prompt", "maxChars", 24000))
                onEditingFinished: root.specSet("prompt", "maxChars", parseInt(text) || 24000)
                ToolTip.visible: hovered
                ToolTip.text: "Avisa por log si el system prompt compuesto lo supera. No trunca."
            }
        }

        // Memoria/RAG y modo Chat: los dos módulos que el spec no cubría. La
        // memoria es lo primero que uno recorta en un perfil al límite de
        // contexto, y el Chat seguía usando los ajustes globales aunque el
        // perfil dijera otra cosa.
        Text { text: "Memoria inyectada"; color: Theme.textSecondary; font.pixelSize: 12 }
        GridLayout {
            Layout.fillWidth: true; columns: 4
            rowSpacing: 8; columnSpacing: 10
            enabled: !root.readOnly

            Text { text: "Hechos estructurados"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcTextField {
                objectName: "structuredFactsField"
                Layout.fillWidth: true; placeholderText: "12"
                text: String(root.specValue("memory", "structuredFacts", 12))
                onEditingFinished: root.specSet("memory", "structuredFacts", parseInt(text) || 0)
                ToolTip.visible: hovered
                ToolTip.text: "Cuántos hechos vigentes de la memoria se inyectan. 0 = ninguno."
            }
            Text { text: "Memoria del proyecto"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcSwitch {
                objectName: "projectMemorySwitch"
                checked: root.specValue("memory", "projectMemory", true)
                onToggled: root.specSet("memory", "projectMemory", checked)
                ToolTip.visible: hovered
                ToolTip.text: ".llamacode/memory.md o AGENTS.md al system prompt."
            }

            Text { text: "Tope memoria proyecto"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcTextField {
                Layout.fillWidth: true; placeholderText: "65536"
                text: String(root.specValue("memory", "projectMemoryMaxChars", 65536))
                onEditingFinished: root.specSet("memory", "projectMemoryMaxChars", parseInt(text) || 0)
            }
            Text { text: "Consolidar al salir"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcSwitch {
                checked: root.specValue("memory", "consolidateOnLeave", true)
                onToggled: root.specSet("memory", "consolidateOnLeave", checked)
                ToolTip.visible: hovered
                ToolTip.text: "Extrae hechos durables del transcript al dejar la sesión."
            }
        }

        Text { text: "Modo Chat (sin tools)"; color: Theme.textSecondary; font.pixelSize: 12 }
        GridLayout {
            Layout.fillWidth: true; columns: 4
            rowSpacing: 8; columnSpacing: 10
            enabled: !root.readOnly

            Text { text: "Razonar"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcSwitch {
                objectName: "chatThinkingSwitch"
                checked: root.specValue("chat", "thinking", false)
                onToggled: root.specSet("chat", "thinking", checked)
            }
            Text { text: "Temperatura"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcTextField {
                Layout.fillWidth: true; placeholderText: "vacío = heredar"
                text: root.specValue("chat", "temperature", -1) >= 0
                      ? String(root.specValue("chat", "temperature", -1)) : ""
                onEditingFinished: root.specSet("chat", "temperature",
                                                text.trim().length ? parseFloat(text) : -1)
            }

            Text { text: "top-P"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcTextField {
                Layout.fillWidth: true; placeholderText: "vacío = heredar"
                text: root.specValue("chat", "topP", -1) >= 0
                      ? String(root.specValue("chat", "topP", -1)) : ""
                onEditingFinished: root.specSet("chat", "topP",
                                                text.trim().length ? parseFloat(text) : -1)
            }
            Text { text: "Persona diseñadora"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcSwitch {
                checked: root.specValue("chat", "designerPersona", false)
                onToggled: root.specSet("chat", "designerPersona", checked)
                ToolTip.visible: hovered
                ToolTip.text: "Preamble para diagramas/mockups (bloques mermaid/svg)."
            }
        }
        TextArea {
            objectName: "chatSystemExtraField"
            Layout.fillWidth: true; Layout.minimumHeight: 50
            enabled: !root.readOnly
            wrapMode: TextArea.Wrap; color: Theme.textPrimary
            placeholderText: "Instrucciones persistentes del modo Chat (opcional)"
            text: root.specValue("chat", "systemExtra", "")
            onEditingFinished: root.specSet("chat", "systemExtra", text)
        }

        Text {
            text: "Reglas de permiso (una por línea: allow|deny|ask [kind:]<glob>)"
            color: Theme.textSecondary; font.pixelSize: 12
        }
        TextArea {
            Layout.fillWidth: true; Layout.minimumHeight: 60
            enabled: !root.readOnly
            wrapMode: TextArea.Wrap; color: Theme.textPrimary
            placeholderText: "deny write:**/.env\nallow read:src/**"
            text: (root.specModule("permissions").rules || []).join("\n")
            onEditingFinished: root.specSet(
                "permissions", "rules",
                text.split("\n").filter(function (l) { return l.trim().length > 0 }))
        }

        // Import / export del spec: compartir un harness entre máquinas o pegarlo
        // en un issue.
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            LcButton {
                text: "Exportar spec"; secondary: true
                enabled: root.profileId.length > 0
                onClicked: specJsonField.text = root.exportSpecJson()
            }
            LcButton {
                text: "Importar spec"; secondary: true
                enabled: !root.readOnly && specJsonField.text.trim().length > 0
                onClicked: {
                    var err = root.applySpecJson(specJsonField.text)
                    if (err.length > 0) specJsonField.text = "// JSON inválido: " + err + "\n" + specJsonField.text
                    else root.saveRequested()
                }
            }
        }
        TextArea {
            id: specJsonField
            objectName: "specJsonField"
            Layout.fillWidth: true; Layout.minimumHeight: 70
            wrapMode: TextArea.WrapAnywhere; color: Theme.textPrimary
            placeholderText: "JSON del HarnessSpec (queda local)"
        }
    }

    // API para el caller: qué `extends` quedó elegido, y setearlo al cargar.
    function currentExtends() { return extendsCombo.currentValue || "" }
    function setExtends(id) {
        var i = extendsCombo.indexOfValue(id || "")
        extendsCombo.currentIndex = i >= 0 ? i : 0
    }
    function editDirective(d) { directiveEditor.loadExisting(d) }
}
