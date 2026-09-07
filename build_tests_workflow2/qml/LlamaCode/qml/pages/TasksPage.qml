import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import LlamaCode 1.0

Item {
    id: root

    // tab 0 = Procesos (definiciones) · tab 1 = Automatizaciones (proceso + horario)
    property int currentTab: 0

    // El agente está arrancando o el server/modelo todavía no quedó listo. Mismo
    // criterio que AgentPage: hasta que esté listo no se puede ejecutar nada y el
    // aviso de tool-calling sería un falso positivo.
    readonly property bool agentLoading: App.agentStarting
                                         || (App.agentRunning && (!App.serverRunning || !App.serverReady))

    // Editor de Proceso (definición). editId vacío = creando.
    property string editId: ""
    // Editor de Automatización. autoEditId vacío = creando.
    property string autoEditId: ""
    property string teachTaskId: ""
    property var teachTargets: []
    ListModel { id: workflowVisualModel }

    // ── Editor de Proceso ──
    function openEditor(id) {
        editId = id
        stepsModel.clear()
        previewBox.visible = false
        previewArea.text = ""
        permFoldersModel.clear()
        if (id.length > 0) {
            const t = App.taskStore.get(id)
            nameField.text = t.name || ""
            descField.text = t.description || ""
            prePromptField.text = t.prePrompt || ""
            postPromptField.text = t.postPrompt || ""
            workflowJsonField.text = Object.keys(t.workflow || {}).length > 0
                ? JSON.stringify(t.workflow, null, 2) : ""
            workflowJsonError.text = ""
            silentUnlessError.checked = t.silentUnlessError || false
            loopEnabled.checked = t.loopEnabled || false
            loopGoalField.text = t.loopGoal || ""
            loopMaxIterations.value = t.loopMaxIterations || 5
            loopMaxSeconds.value = t.loopMaxSeconds || 0
            executionMode.currentIndex = Math.max(0, ["auto", "desktop", "browserBackground"].indexOf(t.executionMode || "auto"))
            approvalPolicy.currentIndex = Math.max(0, ["always", "sensitive", "autonomous"].indexOf(t.approvalPolicy || "sensitive"))
            safetyProfile.currentIndex = Math.max(0, ["normal", "investigation", "guarded", "production"].indexOf(t.safetyProfile || "normal"))
            trainingType.currentIndex = (t.trainingType === "adaptive") ? 1 : 0
            timeoutSec.value = t.timeoutSec || 300
            maxActions.value = t.maxActions || 50
            maxRetries.value = t.maxRetries === undefined ? 2 : t.maxRetries
            datasetInlineField.text = t.datasetInline || ""
            datasetFormatCombo.currentIndex = Math.max(0, ["", "csv", "json"].indexOf((t.datasetFormat || "").toLowerCase()))
            datasetOnErrorCombo.currentIndex = (t.datasetOnError === "abort") ? 1 : 0
            triggerTypeCombo.currentIndex = (t.triggerType === "fileWatch") ? 1 : ((t.triggerType === "hotkey") ? 2 : 0)
            triggerPathField.text = t.triggerPath || ""
            triggerDebounce.value = t.triggerDebounceMs === undefined ? 1500 : t.triggerDebounceMs
            triggerHotkeyField.text = t.triggerHotkey || ""
            verifyOtherModel.checked = !!(t.verifyProfileId)
            verifyProfileCombo.selectProfile(t.verifyProfileId || "")
            autoDifficultyRouting.checked = !!t.autoDifficultyRouting
            loadPermissions(t.permScope || "project", t.permFolders || [])
            const steps = t.steps || []
            for (var i = 0; i < steps.length; ++i)
                stepsModel.append({ kind: steps[i].kind || "instruction",
                                    intent: steps[i].intent || "",
                                    ref: steps[i].ref || "" })
            profileCombo.selectProfile(t.profileId || "")
        } else {
            nameField.text = ""
            descField.text = ""
            prePromptField.text = ""
            postPromptField.text = ""
            workflowJsonField.text = ""
            workflowJsonError.text = ""
            silentUnlessError.checked = false
            loopEnabled.checked = false
            loopGoalField.text = ""
            loopMaxIterations.value = 5
            loopMaxSeconds.value = 0
            executionMode.currentIndex = 0
            approvalPolicy.currentIndex = 1
            safetyProfile.currentIndex = 0
            trainingType.currentIndex = 0
            timeoutSec.value = 300
            maxActions.value = 50
            maxRetries.value = 2
            datasetInlineField.text = ""
            datasetFormatCombo.currentIndex = 0
            datasetOnErrorCombo.currentIndex = 0
            triggerTypeCombo.currentIndex = 0
            triggerPathField.text = ""
            triggerDebounce.value = 1500
            triggerHotkeyField.text = ""
            verifyOtherModel.checked = false
            verifyProfileCombo.currentIndex = 0
            autoDifficultyRouting.checked = false
            loadPermissions("project", [])
            profileCombo.currentIndex = 0
        }
        editor.open()
        editor.refreshVisualWorkflow()
    }

    // ── Editor de Automatización ──
    function openAutoEditor(id) {
        autoEditId = id
        if (id.length > 0) {
            const a = App.automationStore.get(id)
            autoNameField.text = a.name || ""
            autoProcessCombo.selectProcess(a.processId || "")
            autoSilent.checked = a.silentUnlessError || false
            loadScheduleSpec(a.scheduleSpec || {}, a.scheduleCron || "")
        } else {
            autoNameField.text = ""
            autoProcessCombo.currentIndex = 0
            autoSilent.checked = false
            loadScheduleSpec({}, "")
        }
        autoEditor.open()
    }

    // ── Permisos ──
    function loadPermissions(scope, folders) {
        const idx = scope === "folder" ? 1 : (scope === "full" ? 2 : 0)
        permScopeCombo.currentIndex = idx
        permFoldersModel.clear()
        for (var i = 0; i < (folders ? folders.length : 0); ++i)
            permFoldersModel.append({ path: folders[i] })
    }
    function collectPermScope() {
        return ["project", "folder", "full"][permScopeCombo.currentIndex] || "project"
    }
    function collectPermFolders() {
        const arr = []
        for (var i = 0; i < permFoldersModel.count; ++i)
            arr.push(permFoldersModel.get(i).path)
        return arr
    }

    // ── Schedule builder (vive en el editor de Automatización) ──
    // Modos: 0 Diario · 1 Semanal · 2 Mensual · 3 Cada N meses · 4 Avanzado (cron)
    function loadScheduleSpec(spec, cronFallback) {
        const mode = (spec && spec.mode) ? spec.mode : (cronFallback ? "cron" : "daily")
        const modeIdx = { daily: 0, weekly: 1, monthly: 2, everyNMonths: 3, cron: 4 }[mode]
        freqCombo.currentIndex = modeIdx === undefined ? 0 : modeIdx
        schedHour.value = spec && spec.hour !== undefined ? spec.hour : 9
        schedMinute.value = spec && spec.minute !== undefined ? spec.minute : 0
        for (var d = 0; d < 7; ++d)
            weekdayChecks.itemAt(d).checked = spec && spec.weekdays ? (spec.weekdays.indexOf(d) >= 0) : (d === 1)
        monthlyKindCombo.currentIndex = (spec && spec.monthlyKind === "nthWeekday") ? 1 : 0
        monthDaySpin.value = spec && spec.monthDay ? spec.monthDay : 1
        nthCombo.currentIndex = spec && spec.nth ? (spec.nth - 1) : 0
        nthWeekdayCombo.currentIndex = spec && spec.nthWeekday !== undefined ? spec.nthWeekday : 1
        everyNSpin.value = spec && spec.everyN ? spec.everyN : 2
        startMonthSpin.value = spec && spec.startMonth ? spec.startMonth : 1
        cronField.text = (spec && spec.mode === "cron" && spec.cron) ? spec.cron : (cronFallback || "")
    }
    function buildScheduleSpec() {
        const mode = ["daily", "weekly", "monthly", "everyNMonths", "cron"][freqCombo.currentIndex]
        if (mode === "cron") return { mode: "cron", cron: cronField.text }
        const s = { mode: mode, hour: schedHour.value, minute: schedMinute.value }
        if (mode === "weekly") {
            const wd = []
            for (var d = 0; d < 7; ++d) if (weekdayChecks.itemAt(d).checked) wd.push(d)
            s.weekdays = wd
        } else if (mode === "monthly") {
            if (monthlyKindCombo.currentIndex === 1) {
                s.monthlyKind = "nthWeekday"
                s.nth = nthCombo.currentIndex + 1
                s.nthWeekday = nthWeekdayCombo.currentIndex
            } else {
                s.monthlyKind = "date"
                s.monthDay = monthDaySpin.value
            }
        } else if (mode === "everyNMonths") {
            s.everyN = everyNSpin.value
            s.monthDay = monthDaySpin.value
            s.startMonth = startMonthSpin.value
        }
        return s
    }

    // Resumen legible del schedule para la fila de la lista (espejo de TaskSchedule::describe).
    function scheduleSummary(spec, cron) {
        if (!spec || !spec.mode) return cron || "cron"
        const dn = ["Dom", "Lun", "Mar", "Mié", "Jue", "Vie", "Sáb"]
        const hh = ("0" + (spec.hour || 0)).slice(-2) + ":" + ("0" + (spec.minute || 0)).slice(-2)
        if (spec.mode === "cron") return "cron · " + (spec.cron || cron || "")
        if (spec.mode === "daily") return "Diario · " + hh
        if (spec.mode === "weekly") {
            const days = (spec.weekdays || []).map(function(d) { return dn[d] })
            return "Semanal · " + days.join(", ") + " · " + hh
        }
        if (spec.mode === "monthly") {
            if (spec.monthlyKind === "nthWeekday") {
                const ord = ["", "primer", "segundo", "tercer", "cuarto", "último"]
                return "Mensual · " + ord[spec.nth || 1] + " " + dn[spec.nthWeekday || 0] + " · " + hh
            }
            return "Mensual · día " + (spec.monthDay || 1) + " · " + hh
        }
        if (spec.mode === "everyNMonths")
            return "Cada " + (spec.everyN || 1) + " mes(es) · día " + (spec.monthDay || 1) + " · " + hh
        return cron || "cron"
    }

    function processName(pid) {
        const p = App.taskStore.get(pid)
        return (p && p.name) ? p.name : "(proceso eliminado)"
    }

    function processTypeLabel(mode) {
        return mode === "desktop" ? "Escritorio foreground"
             : (mode === "browserBackground" ? "Navegador background" : "Auto")
    }

    function collectSteps() {
        const arr = []
        for (var i = 0; i < stepsModel.count; ++i) {
            const s = stepsModel.get(i)
            arr.push({ kind: s.kind, intent: s.intent, ref: s.ref })
        }
        return arr
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            Layout.fillWidth: true
            title: (App.langV, App.l("nav.tasks"))
            subtitle: root.currentTab === 0
                ? App.taskStore.count + " proceso(s) · definiciones reutilizables que el agente ejecuta y adapta"
                : App.automationStore.count + " programación(es) · procesos programados con fecha/hora"
            action2Label: root.currentTab === 0 ? "Enseñar tarea" : ""
            actionLabel: root.currentTab === 0 ? "Nuevo proceso" : "Nueva programación"
            onAction2Clicked: {
                root.teachTaskId = ""
                teachMode.currentIndex = 0
                teachPopup.open()
            }
            onActionClicked: {
                if (root.currentTab === 0) root.openEditor("")
                else root.openAutoEditor("")
            }
        }

        // Selector de pestañas (segmented control). Respeta el tema activo.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: Theme.navBg
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.divider }

            Row {
                anchors { left: parent.left; leftMargin: 24; verticalCenter: parent.verticalCenter }
                spacing: 6

                Repeater {
                    model: ["Procesos", "Programaciones"]
                    delegate: Rectangle {
                        width: tabLabel.implicitWidth + 32
                        height: 34
                        radius: 8
                        property bool active: root.currentTab === index
                        color: active ? Theme.accent
                                      : (tabMouse.containsMouse ? Theme.surfaceBg : "transparent")
                        border.color: active ? Theme.accent : Theme.borderColor
                        border.width: active ? 0 : 1

                        Text {
                            id: tabLabel
                            anchors.centerIn: parent
                            text: modelData
                            font { pixelSize: 13; bold: parent.active }
                            color: parent.active ? Theme.btnPrimaryText : Theme.textSecondary
                        }
                        MouseArea {
                            id: tabMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.currentTab = index
                        }
                    }
                }
            }
        }

        // Barra de herramientas de Procesos (teach / importar skill).
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            visible: root.currentTab === 0
            color: Theme.navBg
            RowLayout {
                anchors { fill: parent; leftMargin: 24; rightMargin: 16 }
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    text: "Un proceso define el objetivo, perfil, modo y permisos. Teach aprende una demostración como receta adaptable. Programalos en la pestaña Programaciones."
                    color: Theme.textMuted
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
                LcButton {
                    text: "Importar skill"
                    secondary: true
                    onClicked: importPopup.open()
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.divider }
        }

        // Presets de ingeniería: son Tasks declarativas sobre el motor de
        // workflows, reutilizables para cualquier workspace autorizado.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            visible: root.currentTab === 0
            color: Theme.surfaceBg
            RowLayout {
                anchors { fill: parent; leftMargin: 24; rightMargin: 16 }
                spacing: 8
                Text {
                    text: "Workflows:"
                    color: Theme.textSecondary
                    font { pixelSize: 12; bold: true }
                }
                Repeater {
                    model: App.engineeringWorkflows()
                    delegate: LcButton {
                        text: modelData.name
                        secondary: true
                        ToolTip.visible: hovered
                        ToolTip.text: modelData.description
                        onClicked: {
                            const id = App.installEngineeringWorkflow(modelData.id)
                            if (id) root.openEditor(id)
                        }
                    }
                }
                Item { Layout.fillWidth: true }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.divider }
        }

        // Toggle del scheduler (pestaña Automatizaciones).
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            visible: root.currentTab === 1
            color: Theme.navBg
            RowLayout {
                anchors { fill: parent; leftMargin: 24; rightMargin: 16 }
                spacing: 10
                LcSwitch {
                    id: schedSwitch
                    text: ""
                    Layout.preferredWidth: 44
                    Layout.alignment: Qt.AlignVCenter
                    checked: App.tasksSchedulerEnabled
                    onToggled: App.tasksSchedulerEnabled = checked
                }
                Text {
                    text: "Scheduler cron " + (App.tasksSchedulerEnabled ? "activo" : "apagado")
                    color: App.tasksSchedulerEnabled ? Theme.accent : Theme.textSecondary
                    font.pixelSize: 12
                    verticalAlignment: Text.AlignVCenter
                    Layout.alignment: Qt.AlignVCenter
                }
                Text {
                    Layout.fillWidth: true
                    text: "Dispara las Programaciones mientras la app esté abierta. Si el agente ya corre lo usa; si no, lo auto-inicia, ejecuta y lo apaga al terminar."
                    color: Theme.textMuted
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.divider }
        }

        // Aviso de tool-calling del perfil activo (deriva del cookbook + chat-template).
        // Mientras el agente está cargando el cookbook/chat-template todavía no está
        // resuelto: el aviso daría un falso positivo, así que se oculta (ver overlay).
        ToolSupportBanner {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.topMargin: 12
            visible: !root.agentLoading
            support: App.activeProfileToolSupport
        }

        // ── Lista de Procesos ──
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.currentTab === 0
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: parent.width
                spacing: 10
                anchors { topMargin: 16; leftMargin: 24; rightMargin: 24 }

                Item { Layout.preferredHeight: 8; Layout.fillWidth: true }

                Text {
                    visible: App.taskStore.count === 0
                    Layout.leftMargin: 24
                    text: "No hay procesos todavía. Creá uno: definí un objetivo en lenguaje natural\ny pasos de referencia; el agente los ejecuta y se adapta si algo cambió."
                    color: Theme.textMuted
                    font.pixelSize: 13
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    Layout.rightMargin: 24
                    Layout.preferredHeight: approvalText.implicitHeight + 36
                    visible: Object.keys(App.workflowApproval || {}).length > 0
                    radius: 8
                    color: Theme.surfaceBg
                    border.color: Theme.accent

                    RowLayout {
                        anchors { fill: parent; margins: 12 }
                        spacing: 10
                        Text {
                            id: approvalText
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Theme.textPrimary
                            text: (App.workflowApproval.prompt || "El workflow requiere aprobación.")
                                + "\nPaso: " + (App.workflowApproval.stepId || "—")
                        }
                        LcButton {
                            text: "Rechazar"
                            secondary: true
                            onClicked: App.approveTaskWorkflow("reject", "")
                        }
                        LcButton {
                            text: "Aprobar"
                            onClicked: App.approveTaskWorkflow("accept", "")
                        }
                    }
                }

                Text {
                    visible: App.taskAbRunning || (App.taskAbStatus || "").length > 0
                    Layout.fillWidth: true
                    Layout.leftMargin: 24
                    Layout.rightMargin: 24
                    text: App.taskAbStatus
                    color: Theme.accent
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }

                Repeater {
                    model: App.taskStore
                    delegate: Rectangle {
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        Layout.preferredHeight: 88
                        radius: 8
                        color: Theme.surfaceBg
                        border.color: Theme.borderColor
                        property string taskId: model.id || ""
                        property string taskName: model.name || ""

                        RowLayout {
                            anchors { fill: parent; leftMargin: 16; rightMargin: 12 }
                            spacing: 12

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                Text {
                                    text: model.name || "(sin nombre)"
                                    color: Theme.textPrimary
                                    font { pixelSize: 15; bold: true }
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: (model.description || "").length > 0 ? model.description : "—"
                                    color: Theme.textSecondary
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: model.stepCount + " paso(s)"
                                        + "  ·  " + root.processTypeLabel(model.executionMode)
                                        + (model.automationStatus ? "  ·  " + model.automationStatus : "")
                                        + (model.lastRunStatus ? "  ·  última: " + model.lastRunStatus : "")
                                    color: Theme.textMuted
                                    font.pixelSize: 11
                                }
                                Text {
                                    visible: App.runningTaskId === taskId
                                    text: App.runningTaskPhase === "verificando"
                                          ? "Verificando con postprompt..."
                                          : App.runningTaskPhase === "aprobación"
                                            ? "Esperando aprobación..."
                                            : App.runningTaskPhase.indexOf("workflow:") === 0
                                              ? "Workflow · " + App.runningTaskPhase.substring(9)
                                              : "Ejecutando..."
                                    color: Theme.accent
                                    font.pixelSize: 11
                                }
                            }

                            LcButton {
                                text: App.runningTaskId === taskId
                                      ? "Ejecutando..."
                                      : (!App.canRunTask && (App.agentStarting || (App.serverRunning && !App.serverReady))
                                         ? "Cargando..."
                                         : "▶ Ejecutar")
                                enabled: App.canRunTask
                                onClicked: App.runTask(taskId)
                            }
                            LcButton {
                                text: "A/B"
                                secondary: true
                                enabled: App.canRunTask && !App.taskAbRunning
                                onClicked: App.runTaskAB(taskId)
                            }
                            LcButton {
                                text: model.teachArtifactId ? "Reentrenar" : "Teach"
                                secondary: true
                                onClicked: {
                                    root.teachTaskId = taskId
                                    teachMode.currentIndex = model.executionMode === "browserBackground" ? 1 : 0
                                    teachPopup.open()
                                }
                            }
                            LcButton {
                                text: "Plantillas"
                                secondary: true
                                visible: !!model.teachArtifactId
                                onClicked: templatePopup.openFor(model.teachArtifactId, taskName)
                            }
                            LcButton {
                                text: "Red"
                                secondary: true
                                visible: !!model.teachArtifactId
                                onClicked: networkPopup.openFor(model.teachArtifactId, taskName)
                            }
                            LcButton {
                                text: "Detener"
                                secondary: true
                                visible: App.runningTaskId === taskId
                                onClicked: App.stopAutomation()
                            }
                            LcButton {
                                text: "Historial"
                                secondary: true
                                onClicked: historyDialog.openFor(taskId, taskName)
                            }
                            LcButton {
                                text: "Editar"
                                secondary: true
                                onClicked: root.openEditor(taskId)
                            }
                            LcButton {
                                text: "✕"
                                secondary: true
                                onClicked: { delConfirm.taskId = taskId; delConfirm.taskName = taskName; delConfirm.open() }
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true; Layout.fillWidth: true }
            }
        }

        // ── Lista de Automatizaciones ──
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.currentTab === 1
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: parent.width
                spacing: 10
                anchors { topMargin: 16; leftMargin: 24; rightMargin: 24 }

                Item { Layout.preferredHeight: 8; Layout.fillWidth: true }

                Text {
                    visible: App.automationStore.count === 0
                    Layout.leftMargin: 24
                    text: App.taskStore.count === 0
                        ? "Primero creá un proceso en la pestaña Procesos. Luego volvé acá\npara programarlo con su fecha/hora."
                        : "No hay programaciones todavía. Creá una: elegí un proceso existente\ny programá cuándo debe ejecutarse."
                    color: Theme.textMuted
                    font.pixelSize: 13
                }

                Repeater {
                    model: App.automationStore
                    delegate: Rectangle {
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        Layout.preferredHeight: 88
                        radius: 8
                        color: Theme.surfaceBg
                        border.color: Theme.borderColor
                        property string autoId: model.id || ""
                        property string autoName: model.name || ""
                        property string linkedProcessId: model.processId || ""

                        RowLayout {
                            anchors { fill: parent; leftMargin: 16; rightMargin: 12 }
                            spacing: 12

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                Text {
                                    text: model.name || "(sin nombre)"
                                    color: Theme.textPrimary
                                    font { pixelSize: 15; bold: true }
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: "Proceso: " + root.processName(linkedProcessId)
                                    color: Theme.textSecondary
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: (model.scheduleEnabled ? "⏱ " : "⏸ apagada · ")
                                        + root.scheduleSummary(model.scheduleSpec, model.scheduleCron)
                                        + (model.lastRunStatus ? "  ·  última: " + model.lastRunStatus : "")
                                    color: Theme.textMuted
                                    font.pixelSize: 11
                                }
                            }

                            LcButton {
                                text: App.runningTaskId === linkedProcessId
                                      ? "Ejecutando..."
                                      : (!App.canRunTask && (App.agentStarting || (App.serverRunning && !App.serverReady))
                                         ? "Cargando..."
                                         : "▶ Ejecutar")
                                enabled: App.canRunTask
                                onClicked: App.runAutomation(autoId)
                            }
                            LcButton {
                                text: "Historial"
                                secondary: true
                                onClicked: historyDialog.openFor(autoId, autoName)
                            }
                            LcButton {
                                text: "Editar"
                                secondary: true
                                onClicked: root.openAutoEditor(autoId)
                            }
                            LcButton {
                                text: "✕"
                                secondary: true
                                onClicked: { delAutoConfirm.autoId = autoId; delAutoConfirm.autoName = autoName; delAutoConfirm.open() }
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true; Layout.fillWidth: true }
            }
        }
    }

    // Overlay "Cargando agente" — mismo patrón que AgentPage. Mientras el agente
    // arranca, tapa la lista para evitar ejecutar/leer estado inconsistente.
    Rectangle {
        anchors.centerIn: parent
        z: 50
        visible: root.agentLoading
        radius: 10
        color: Theme.surfaceBg
        border.color: Theme.borderColor
        implicitWidth: tasksLoadingRow.implicitWidth + 32
        implicitHeight: tasksLoadingRow.implicitHeight + 24
        Row {
            id: tasksLoadingRow
            anchors.centerIn: parent
            spacing: 10
            Rectangle {
                width: 10; height: 10; radius: 5
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.warnText
                SequentialAnimation on opacity {
                    running: parent.parent.visible
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.3; duration: 600 }
                    NumberAnimation { to: 1.0; duration: 600 }
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: App.agentStarting
                    ? "Iniciando agente..."
                    : !App.serverRunning
                    ? "Servidor no disponible. Iniciá el modelo en Lanzar."
                    : "Cargando modelo..."
                color: Theme.textSecondary; font.pixelSize: 14
            }
        }
    }

    ListModel { id: stepsModel }
    ListModel { id: permFoldersModel }

    FolderDialog {
        id: permFolderDlg
        title: "Elegir carpeta permitida para el proceso"
        onAccepted: permFoldersModel.append({ path: selectedFolder.toString().replace("file:///", "") })
    }

    Popup {
        id: teachPopup
        modal: true
        parent: Overlay.overlay
        width: Math.min(680, root.width - 80)
        height: Math.min(620, root.height - 80)
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        padding: 18
        // Reentrenar un proceso existente (botón "Teach" de la card) vs crear uno
        // nuevo (botón "Enseñar tarea" del header). Enseñar = crear proceso.
        property bool isNewProcess: root.teachTaskId.length === 0
        onOpened: {
            teachError.text = ""
            teachName.text = ""
            teachObjective.text = ""
            refreshTargets()
        }
        function refreshTargets() {
            root.teachTargets = scopeKind.currentIndex === 0
                ? App.automationScreens() : App.automationWindows()
            targetCombo.currentIndex = root.teachTargets.length > 0 ? 0 : -1
        }
        background: Rectangle {
            color: Theme.popupBg; radius: 12
            border.color: Theme.popupBorderColor
        }
        contentItem: ColumnLayout {
            spacing: 10
            Text {
                text: "Enseñar tarea"
                color: Theme.textPrimary
                font { pixelSize: 16; bold: true }
            }
            Text {
                Layout.fillWidth: true
                text: "Elegí la modalidad, mostrá la tarea paso a paso y al finalizar se guarda como un proceso con receta semántica adaptable."
                wrapMode: Text.Wrap
                color: Theme.textMuted
                font.pixelSize: 11
            }

            // Reentrenamiento: el proceso ya existe, solo se muestra cuál.
            Text {
                visible: !teachPopup.isNewProcess
                text: "Reentrenando: " + root.processName(root.teachTaskId)
                color: Theme.textSecondary; font.pixelSize: 12
            }

            // Nuevo proceso: nombre + objetivo.
            Text { visible: teachPopup.isNewProcess; text: "Nombre del proceso"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcTextField {
                id: teachName
                visible: teachPopup.isNewProcess
                Layout.fillWidth: true
                placeholderText: "Ej: Extraer cotización del dólar"
            }
            Text { visible: teachPopup.isNewProcess; text: "Objetivo de la tarea"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcTextField {
                id: teachObjective
                visible: teachPopup.isNewProcess
                Layout.fillWidth: true
                placeholderText: "Qué tiene que lograr el agente al ejecutar este proceso"
            }

            Text { text: "Modalidad"; color: Theme.textSecondary; font.pixelSize: 12 }
            LcComboBox {
                id: teachMode
                Layout.fillWidth: true
                model: ["Escritorio foreground", "Navegador background"]
            }
            // Explicación de cada modalidad.
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.textMuted
                font.pixelSize: 11
                text: teachMode.currentIndex === 0
                    ? "Escritorio foreground: F8 captura 72×72 bajo el cursor. F9 abre un selector rectangular seguro: arrastrá sobre cualquier objetivo gráfico; Esc cancela."
                    : "Navegador background: corre en un navegador headless por detrás, sin tomar tu pantalla. Podés seguir usando la PC mientras se ejecuta. Sirve para tareas 100% web (abrir una URL, extraer datos, completar formularios)."
            }
            RowLayout {
                Layout.fillWidth: true
                visible: teachMode.currentIndex === 0
                LcComboBox {
                    id: scopeKind
                    Layout.preferredWidth: 150
                    model: ["Pantalla", "Ventana"]
                    onActivated: teachPopup.refreshTargets()
                }
                LcComboBox {
                    id: targetCombo
                    Layout.fillWidth: true
                    model: root.teachTargets
                    textRole: "label"
                    valueRole: "id"
                }
                LcButton { text: "Actualizar"; secondary: true; onClicked: teachPopup.refreshTargets() }
            }
            LcTextField {
                id: teachUrl
                Layout.fillWidth: true
                visible: teachMode.currentIndex === 1
                placeholderText: "URL inicial (https://...)"
            }
            CheckBox {
                id: discoverNetwork
                visible: teachMode.currentIndex === 1
                checked: true
                text: "Descubrir y guardar contratos de red durante la ejecución"
                contentItem: Text {
                    text: discoverNetwork.text
                    color: Theme.textSecondary
                    font.pixelSize: 11
                    leftPadding: discoverNetwork.indicator.width + 6
                    verticalAlignment: Text.AlignVCenter
                }
            }
            Text {
                Layout.fillWidth: true
                visible: teachMode.currentIndex === 1 && discoverNetwork.checked
                text: "Observa tráfico ya generado por Playwright, elimina queries, headers, cookies y cuerpos, y lo deja pendiente de revisión. No reejecuta endpoints."
                color: Theme.textMuted
                font.pixelSize: 10
                wrapMode: Text.Wrap
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }
            Text {
                text: "Estado: " + App.teachState
                color: App.teachState === "failed" ? Theme.btnDangerBg : Theme.accent
                font.pixelSize: 12
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: App.teachTimeline
                delegate: Text {
                    width: ListView.view.width
                    text: (index + 1) + ". [" + (modelData.kind || "paso") + "] "
                          + (modelData.intent || "")
                    color: Theme.textSecondary
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }
            }
            Text {
                id: teachError
                Layout.fillWidth: true
                visible: text.length > 0 || App.teachError.length > 0
                text: text.length > 0 ? text : App.teachError
                color: Theme.btnDangerBg
                wrapMode: Text.Wrap
                font.pixelSize: 11
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                LcButton {
                    text: "Iniciar"
                    visible: App.teachState !== "recording" && App.teachState !== "paused"
                    enabled: (teachPopup.isNewProcess
                                ? teachObjective.text.trim().length > 0
                                : root.teachTaskId.length > 0)
                             && (teachMode.currentIndex === 1 || targetCombo.currentIndex >= 0)
                    onClicked: {
                        // Enseñar = crear el proceso. Si es nuevo, se guarda ahora con
                        // nombre + objetivo + modalidad y se usa su id para grabar.
                        let pid = root.teachTaskId
                        if (teachPopup.isNewProcess) {
                            pid = App.taskStore.save("", {
                                "name": teachName.text.trim().length > 0
                                    ? teachName.text.trim()
                                    : (teachObjective.text.trim().substring(0, 60) || "Tarea enseñada"),
                                "description": teachObjective.text.trim(),
                                "prePrompt": teachObjective.text.trim(),
                                "executionMode": teachMode.currentIndex === 0 ? "desktop" : "browserBackground",
                                "discoverNetwork": teachMode.currentIndex === 1 && discoverNetwork.checked
                            })
                            if (!pid || pid.length === 0) {
                                teachError.text = "No se pudo crear el proceso."
                                return
                            }
                            root.teachTaskId = pid
                        }
                        const err = teachMode.currentIndex === 0
                            ? App.startDesktopTeach(pid,
                                scopeKind.currentIndex === 0 ? "screen" : "window",
                                targetCombo.currentValue)
                            : App.startBrowserTeach(pid, teachUrl.text, discoverNetwork.checked)
                        teachError.text = err
                    }
                }
                LcButton {
                    text: App.teachState === "paused" ? "Continuar" : "Pausar"
                    secondary: true
                    visible: App.teachState === "recording" || App.teachState === "paused"
                    onClicked: App.pauseTeach(App.teachState !== "paused")
                }
                LcButton {
                    text: "Capturar imagen"
                    secondary: true
                    visible: teachMode.currentIndex === 0
                          && (App.teachState === "recording" || App.teachState === "paused")
                    onClicked: {
                        const ref = App.captureTeachVisualReference(72)
                        teachError.text = ref && ref.file
                            ? "Plantilla guardada: " + ref.file
                            : (App.teachError || "No se pudo capturar la región bajo el cursor.")
                    }
                }
                LcButton {
                    text: "Seleccionar región"
                    secondary: true
                    visible: teachMode.currentIndex === 0 && App.teachState === "recording"
                    onClicked: {
                        teachError.text = App.armTeachVisualRegionSelection()
                            ? "Selección armada: arrastrá un rectángulo sobre el objetivo."
                            : "No se pudo armar la selección."
                    }
                }
                LcButton {
                    text: "Finalizar"
                    visible: App.teachState === "recording" || App.teachState === "paused"
                    onClicked: {
                        const artifact = App.finishTeach()
                        if (artifact.length > 0) teachPopup.close()
                    }
                }
                LcButton {
                    text: "Cancelar"
                    secondary: true
                    onClicked: { App.cancelTeach(); teachPopup.close() }
                }
            }
        }
    }

    Popup {
        id: importPopup
        modal: true
        parent: Overlay.overlay
        width: 460
        height: 300
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        padding: 18
        property var skills: []
        onOpened: { skills = App.listBrowserSkills(); importError.text = "" }
        background: Rectangle { color: Theme.popupBg; radius: 12; border.color: Theme.popupBorderColor }
        contentItem: ColumnLayout {
            Text { text: "Importar skill Playwright"; color: Theme.textPrimary; font { pixelSize: 15; bold: true } }
            Text {
                Layout.fillWidth: true
                text: "Crea un proceso de navegador background sin modificar el skill existente."
                color: Theme.textMuted; font.pixelSize: 11; wrapMode: Text.Wrap
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: importPopup.skills
                delegate: RowLayout {
                    width: ListView.view.width
                    Text { Layout.fillWidth: true; text: modelData; color: Theme.textSecondary }
                    LcButton {
                        text: "Importar"
                        onClicked: {
                            const id = App.importBrowserSkillAsTask(modelData)
                            if (id.length > 0) importPopup.close()
                            else importError.text = "No se pudo importar el skill."
                        }
                    }
                }
            }
            Text { id: importError; color: Theme.btnDangerBg; font.pixelSize: 11 }
            LcButton { text: "Cerrar"; secondary: true; onClicked: importPopup.close() }
        }
    }

    // ── Editor de Proceso ──
    Popup {
        id: editor
        modal: true
        parent: Overlay.overlay
        closePolicy: Popup.CloseOnEscape
        width: Math.min(760, Math.max(520, root.width - 80))
        height: Math.min(760, Math.max(520, root.height - 80))
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        padding: 0

        function saveAndClose() {
            var workflow = {}
            if (workflowJsonField.text.trim().length > 0) {
                try {
                    workflow = JSON.parse(workflowJsonField.text)
                    if (!workflow.entry || !workflow.steps)
                        throw new Error("debe contener entry y steps")
                    const validation = App.validateWorkflow(workflow)
                    if (validation.length > 0) throw new Error(validation)
                } catch (e) {
                    workflowJsonError.text = "Workflow JSON inválido: " + e
                    return
                }
            }
            App.taskStore.save(root.editId, {
                name: nameField.text,
                description: descField.text,
                prePrompt: prePromptField.text,
                postPrompt: postPromptField.text,
                silentUnlessError: silentUnlessError.checked,
                loopEnabled: loopEnabled.checked,
                loopGoal: loopGoalField.text,
                loopMaxIterations: loopMaxIterations.value,
                loopMaxSeconds: loopMaxSeconds.value,
                verifyProfileId: verifyOtherModel.checked ? verifyProfileCombo.selectedProfileId : "",
                autoDifficultyRouting: verifyOtherModel.checked && autoDifficultyRouting.checked,
                executionMode: ["auto", "desktop", "browserBackground"][executionMode.currentIndex],
                approvalPolicy: ["always", "sensitive", "autonomous"][approvalPolicy.currentIndex],
                safetyProfile: ["normal", "investigation", "guarded", "production"][safetyProfile.currentIndex],
                trainingType: ["literal", "adaptive"][trainingType.currentIndex],
                timeoutSec: timeoutSec.value,
                maxActions: maxActions.value,
                maxRetries: maxRetries.value,
                datasetInline: datasetInlineField.text,
                datasetFormat: ["", "csv", "json"][datasetFormatCombo.currentIndex],
                datasetOnError: ["continue", "abort"][datasetOnErrorCombo.currentIndex],
                triggerType: ["manual", "fileWatch", "hotkey"][triggerTypeCombo.currentIndex],
                triggerPath: triggerPathField.text,
                triggerDebounceMs: triggerDebounce.value,
                triggerHotkey: triggerHotkeyField.text,
                profileId: profileCombo.selectedProfileId,
                steps: root.collectSteps(),
                permScope: root.collectPermScope(),
                permFolders: root.collectPermFolders(),
                workflow: workflow
            })
            close()
        }

        function refreshVisualWorkflow() {
            workflowVisualModel.clear()
            if (workflowJsonField.text.trim().length === 0) return
            try {
                const def = JSON.parse(workflowJsonField.text)
                const rows = App.workflowVisualRows(def)
                for (let i = 0; i < rows.length; ++i) {
                    const row = rows[i]
                    workflowVisualModel.append({ stepId: row.stepId, stepType: row.stepType,
                        stepNext: row.stepNext || "", stepPrompt: row.stepPrompt || "",
                        routeKey: row.routeKey || "", originalStepJson: JSON.stringify(row.originalStep || {}) })
                }
                workflowJsonError.text = App.validateWorkflow(def)
            } catch (e) { workflowJsonError.text = "Workflow JSON inválido: " + e }
        }

        function applyVisualWorkflow() {
            let def = { schemaVersion: 1, entry: "", steps: {} }
            try { if (workflowJsonField.text.trim().length > 0) def = JSON.parse(workflowJsonField.text) }
            catch (e) {}
            const rows = []
            for (let i = 0; i < workflowVisualModel.count; ++i) {
                const row = workflowVisualModel.get(i)
                let original = {}
                try { original = JSON.parse(row.originalStepJson || "{}") } catch (e) {}
                rows.push({ stepId: row.stepId, stepType: row.stepType,
                    stepNext: row.stepNext, stepPrompt: row.stepPrompt,
                    routeKey: row.routeKey, originalStep: original })
            }
            def = App.mergeWorkflowVisual(def, rows)
            workflowJsonField.text = workflowVisualModel.count > 0 ? JSON.stringify(def, null, 2) : ""
            workflowJsonError.text = workflowVisualModel.count > 0 ? App.validateWorkflow(def) : ""
        }

        function addVisualStep() {
            let n = workflowVisualModel.count + 1
            let id = "step" + n
            let used = true
            while (used) {
                used = false
                for (let i = 0; i < workflowVisualModel.count; ++i)
                    if (workflowVisualModel.get(i).stepId === id) { used = true; id = "step" + (++n); break }
            }
            if (workflowVisualModel.count > 0)
                workflowVisualModel.setProperty(workflowVisualModel.count - 1, "stepNext", id)
            workflowVisualModel.append({ stepId: id, stepType: "agent", stepNext: "", stepPrompt: "",
                routeKey: "next", originalStepJson: "{}" })
            applyVisualWorkflow()
        }

        background: Rectangle {
            color: Theme.popupBg
            radius: 12
            border.color: Theme.popupBorderColor
            border.width: 1
        }
        Overlay.modal: Rectangle { color: Theme.overlayColor }

        contentItem: ColumnLayout {
            spacing: 0
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                color: Theme.popupHeaderBg
                radius: 12
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 12; color: Theme.popupHeaderBg }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.popupHeaderBorder }
                Text {
                    anchors { left: parent.left; leftMargin: 22; verticalCenter: parent.verticalCenter }
                    text: root.editId.length > 0 ? "Editar proceso" : "Nuevo proceso"
                    font { pixelSize: 14; bold: true }
                    color: Theme.textPrimary
                }
            }

            ScrollView {
                id: editorScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ColumnLayout {
                    width: Math.max(0, editorScroll.width - 36)
                    spacing: 10
                    x: 18
                    Item { Layout.preferredHeight: 6; Layout.fillWidth: true }

                    Text { text: "Nombre"; color: Theme.textSecondary; font.pixelSize: 12 }
                    LcTextField { id: nameField; Layout.fillWidth: true; placeholderText: "Ej: Extraer cotización del dólar" }

                    Text { text: "Objetivo (qué se busca y por qué)"; color: Theme.textSecondary; font.pixelSize: 12 }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 80
                        TextArea {
                            id: descField
                            wrapMode: TextArea.Wrap
                            color: Theme.textPrimary
                            placeholderText: "Describí el objetivo en lenguaje natural. El agente lo usa para adaptarse."
                            background: Rectangle { radius: 6; color: Theme.inputBg; border.color: Theme.inputBorderColor }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "Workflow"; color: Theme.textSecondary; font.pixelSize: 12; Layout.fillWidth: true }
                        LcButton { text: "↻ Visualizar"; secondary: true; onClicked: editor.refreshVisualWorkflow() }
                        LcButton { text: "+ Nodo"; secondary: true; onClicked: editor.addVisualStep() }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: workflowVisualModel.count > 0 ? Math.min(300, 18 + workflowVisualModel.count * 112) : 0
                        visible: workflowVisualModel.count > 0
                        color: Theme.inputBg
                        border.color: Theme.inputBorderColor
                        radius: 6
                        ListView {
                            anchors { fill: parent; margins: 8 }
                            spacing: 6
                            model: workflowVisualModel
                            clip: true
                            delegate: Rectangle {
                                width: ListView.view.width
                                height: 104
                                radius: 5
                                color: Theme.popupBg
                                border.color: index === 0 ? Theme.accent : Theme.borderColor
                                ColumnLayout {
                                    anchors { fill: parent; margins: 7 }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        LcTextField { Layout.preferredWidth: 120; text: model.stepId; onEditingFinished: { workflowVisualModel.setProperty(index, "stepId", text); editor.applyVisualWorkflow() } }
                                        LcComboBox { Layout.preferredWidth: 120; model: ["agent","tool","verify","condition","approval","parallel","finish"]; currentIndex: Math.max(0, model.indexOf(workflowVisualModel.get(index).stepType)); onActivated: { workflowVisualModel.setProperty(index, "stepType", currentText); editor.applyVisualWorkflow() } }
                                        Text { text: "→"; color: Theme.textMuted }
                                        LcTextField { Layout.fillWidth: true; text: model.stepNext; placeholderText: "siguiente"; onEditingFinished: { workflowVisualModel.setProperty(index, "stepNext", text); editor.applyVisualWorkflow() } }
                                        LcButton { text: "✕"; secondary: true; onClicked: { workflowVisualModel.remove(index); editor.applyVisualWorkflow() } }
                                    }
                                    LcTextField { Layout.fillWidth: true; text: model.stepPrompt; placeholderText: "Prompt o instrucción del nodo"; onEditingFinished: { workflowVisualModel.setProperty(index, "stepPrompt", text); editor.applyVisualWorkflow() } }
                                }
                            }
                        }
                    }
                    Text { text: "JSON sincronizado (avanzado)"; color: Theme.textMuted; font.pixelSize: 11 }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 150
                        TextArea {
                            id: workflowJsonField
                            wrapMode: TextArea.NoWrap
                            color: Theme.textPrimary
                            font.family: "Consolas"
                            placeholderText: '{"schemaVersion":1,"entry":"explore","steps":{"explore":{"type":"agent","prompt":"Explorá","next":"done"},"done":{"type":"finish"}}}'
                            background: Rectangle { radius: 6; color: Theme.inputBg; border.color: Theme.inputBorderColor }
                        }
                    }
                    Text { id: workflowJsonError; color: Theme.btnDangerBg; font.pixelSize: 11; visible: text.length > 0 }

                    Text { text: "Preprompt opcional"; color: Theme.textSecondary; font.pixelSize: 12 }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 72
                        TextArea {
                            id: prePromptField
                            wrapMode: TextArea.Wrap
                            color: Theme.textPrimary
                            placeholderText: "Contexto o reglas previas antes de ejecutar el proceso."
                            background: Rectangle { radius: 6; color: Theme.inputBg; border.color: Theme.inputBorderColor }
                        }
                    }

                    Text { text: "Postprompt opcional"; color: Theme.textSecondary; font.pixelSize: 12 }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 72
                        TextArea {
                            id: postPromptField
                            wrapMode: TextArea.Wrap
                            color: Theme.textPrimary
                            placeholderText: "Chequeo posterior, validación o resumen que el agente debe hacer al terminar."
                            background: Rectangle { radius: 6; color: Theme.inputBg; border.color: Theme.inputBorderColor }
                        }
                    }

                    CheckBox {
                        id: silentUnlessError
                        text: "Ejecutar en silencio salvo error"
                        contentItem: Text {
                            text: silentUnlessError.text
                            color: Theme.textSecondary
                            font.pixelSize: 12
                            leftPadding: silentUnlessError.indicator.width + 6
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    CheckBox {
                        id: loopEnabled
                        text: "Correr en bucle hasta cumplir un objetivo"
                        contentItem: Text {
                            text: loopEnabled.text
                            color: Theme.textSecondary
                            font.pixelSize: 12
                            leftPadding: loopEnabled.indicator.width + 6
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        visible: loopEnabled.checked
                        Text { text: "Objetivo de éxito del bucle"; color: Theme.textSecondary; font.pixelSize: 12 }
                        ScrollView {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 60
                            TextArea {
                                id: loopGoalField
                                wrapMode: TextArea.Wrap
                                color: Theme.textPrimary
                                placeholderText: "Condición que el agente verifica entre iteraciones (ej: 'todos los tests en verde')."
                                background: Rectangle { radius: 6; color: Theme.inputBg; border.color: Theme.inputBorderColor }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            Text { text: "Máx. iteraciones"; color: Theme.textSecondary; font.pixelSize: 11 }
                            SpinBox { id: loopMaxIterations; from: 1; to: 1000; value: 5; editable: true }
                            Text { text: "Máx. segundos (0 = sin límite)"; color: Theme.textSecondary; font.pixelSize: 11 }
                            SpinBox { id: loopMaxSeconds; from: 0; to: 86400; stepSize: 3600; value: 0; editable: true }
                        }
                    }

                    Text { text: "Perfil del agente (opcional)"; color: Theme.textSecondary; font.pixelSize: 12 }
                    LcComboBox {
                        id: profileCombo
                        Layout.fillWidth: true
                        textRole: "name"
                        valueRole: "profileId"
                        property string selectedProfileId: currentValue || ""
                        model: App.profileManager
                        function selectProfile(pid) {
                            const idx = indexOfValue(pid)
                            currentIndex = idx >= 0 ? idx : 0
                        }
                    }

                    CheckBox {
                        id: verifyOtherModel
                        text: "Verificar con otro modelo (routing por fase)"
                        contentItem: Text {
                            text: verifyOtherModel.text
                            color: Theme.textSecondary
                            font.pixelSize: 12
                            leftPadding: verifyOtherModel.indicator.width + 6
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    LcComboBox {
                        id: verifyProfileCombo
                        Layout.fillWidth: true
                        visible: verifyOtherModel.checked
                        textRole: "name"
                        valueRole: "profileId"
                        property string selectedProfileId: currentValue || ""
                        model: App.profileManager
                        function selectProfile(pid) {
                            const idx = indexOfValue(pid)
                            currentIndex = idx >= 0 ? idx : 0
                        }
                    }

                    CheckBox {
                        id: autoDifficultyRouting
                        visible: verifyOtherModel.checked
                        text: "Escalar al revisor sólo si la dificultad es alta"
                        contentItem: Text {
                            text: autoDifficultyRouting.text
                            color: Theme.textSecondary
                            font.pixelSize: 12
                            leftPadding: autoDifficultyRouting.indicator.width + 6
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: "Tipo de proceso"; color: Theme.textSecondary; font.pixelSize: 12 }
                            LcComboBox {
                                id: executionMode
                                Layout.fillWidth: true
                                model: ["Auto", "Escritorio foreground", "Navegador background"]
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: "Aprobaciones"; color: Theme.textSecondary; font.pixelSize: 12 }
                            LcComboBox {
                                id: approvalPolicy
                                Layout.fillWidth: true
                                model: ["Confirmar siempre", "Sólo acciones sensibles", "Autónoma"]
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: "Perfil de seguridad"; color: Theme.textSecondary; font.pixelSize: 12 }
                            LcComboBox {
                                id: safetyProfile
                                Layout.fillWidth: true
                                model: ["Normal", "Investigación", "Guardado", "Producción"]
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: "Tipo de entrenamiento"; color: Theme.textSecondary; font.pixelSize: 12 }
                            LcComboBox {
                                id: trainingType
                                Layout.fillWidth: true
                                model: ["Copia literal tal cual", "Indicaciones generales"]
                            }
                        }
                    }

                    // Explica los dos modos de reproducción del Teach.
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        font.pixelSize: 11
                        text: trainingType.currentIndex === 0
                            ? "Copia literal: reproduce los pasos grabados EXACTO (teclado y trazos), sin pensar. Rápido y liviano; ideal para dibujos/secuencias fijas. Requiere que la app quede como al grabar."
                            : "Indicaciones generales: el agente entiende el título, la descripción y el Teach como una GUÍA, y decide cada paso adaptándose a la interfaz actual. Más robusto si algo cambió, pero más pesado (la IA piensa cada paso)."
                    }

                    // Aclara qué hace cada tipo de proceso (en especial "Auto").
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        font.pixelSize: 11
                        text: executionMode.currentIndex === 0
                            ? "Auto: el sistema elige la superficie al ejecutar. Si la automatización tiene pasos de escritorio corre como Escritorio foreground; si no, como Navegador background (headless, sin robar el foco)."
                            : (executionMode.currentIndex === 1
                                ? "Escritorio foreground: controla tu pantalla real (mouse y teclado). Ocupa la PC mientras corre."
                                : "Navegador background: corre en un navegador headless por detrás, sin tomar tu pantalla. Para tareas 100% web.")
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Text { text: "Timeout (s)"; color: Theme.textSecondary; font.pixelSize: 11 }
                        SpinBox { id: timeoutSec; from: 30; to: 3600; value: 300; editable: true }
                        Text { text: "Máx. acciones"; color: Theme.textSecondary; font.pixelSize: 11 }
                        SpinBox { id: maxActions; from: 1; to: 500; value: 50; editable: true }
                        Text { text: "Reintentos"; color: Theme.textSecondary; font.pixelSize: 11 }
                        SpinBox { id: maxRetries; from: 0; to: 10; value: 2; editable: true }
                    }

                    // ── Datos por lote (RPA data-driven): mismo flujo, una corrida
                    //    por fila, con {{variable}} sustituido por los valores. ──
                    Text { text: "Datos por lote (opcional)"; color: Theme.textSecondary; font.pixelSize: 12 }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        font.pixelSize: 11
                        text: "Pegá CSV (1ra fila = encabezados) o JSON (array de objetos). El proceso corre una vez por fila; usá {{columna}} en objetivo/preprompt para inyectar los valores."
                    }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 90
                        TextArea {
                            id: datasetInlineField
                            placeholderText: "nombre,ciudad\nAna,La Plata\nBeto,Rosario"
                            wrapMode: TextEdit.NoWrap
                            color: Theme.textPrimary
                            font.family: "Consolas, monospace"
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Text { text: "Formato"; color: Theme.textSecondary; font.pixelSize: 11 }
                        ComboBox { id: datasetFormatCombo; model: ["Auto", "CSV", "JSON"] }
                        Text { text: "Si una fila falla"; color: Theme.textSecondary; font.pixelSize: 11 }
                        ComboBox { id: datasetOnErrorCombo; model: ["Seguir con la próxima", "Cortar el lote"] }
                    }

                    // ── Disparador (arranque desatendido) ──
                    Text { text: "Disparador"; color: Theme.textSecondary; font.pixelSize: 12 }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        ComboBox { id: triggerTypeCombo; model: ["Manual", "Al cambiar un archivo/carpeta", "Con un atajo de teclado"] }
                        SpinBox {
                            id: triggerDebounce
                            visible: triggerTypeCombo.currentIndex === 1
                            from: 0; to: 60000; stepSize: 500; value: 1500; editable: true
                        }
                        Text {
                            visible: triggerTypeCombo.currentIndex === 1
                            text: "ms debounce"; color: Theme.textMuted; font.pixelSize: 11
                        }
                    }
                    TextField {
                        id: triggerPathField
                        Layout.fillWidth: true
                        visible: triggerTypeCombo.currentIndex === 1
                        placeholderText: "Ruta absoluta del archivo o carpeta a vigilar"
                        color: Theme.textPrimary
                    }
                    TextField {
                        id: triggerHotkeyField
                        Layout.fillWidth: true
                        visible: triggerTypeCombo.currentIndex === 2
                        placeholderText: "Atajo global, ej. CTRL+ALT+R (modificador + tecla)"
                        color: Theme.textPrimary
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "Pasos de referencia"; color: Theme.textSecondary; font.pixelSize: 12; Layout.fillWidth: true }
                        LcButton {
                            text: "+ Paso"
                            secondary: true
                            onClicked: stepsModel.append({ kind: "instruction", intent: "", ref: "" })
                        }
                    }

                    Repeater {
                        model: stepsModel
                        delegate: Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 88
                            radius: 6
                            color: Theme.inputBg
                            border.color: Theme.borderColor
                            ColumnLayout {
                                anchors { fill: parent; margins: 8 }
                                spacing: 6
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    LcComboBox {
                                        Layout.preferredWidth: 140
                                        model: ["instruction", "browser", "shell", "mail", "desktop"]
                                        currentIndex: Math.max(0, model.indexOf(stepsModel.get(index).kind))
                                        onActivated: stepsModel.setProperty(index, "kind", currentText)
                                    }
                                    LcTextField {
                                        Layout.fillWidth: true
                                        text: model.intent
                                        placeholderText: "Qué hacer en este paso (intención)"
                                        onTextChanged: stepsModel.setProperty(index, "intent", text)
                                    }
                                    LcButton {
                                        text: "⏺ Browser"
                                        secondary: true
                                        visible: model.kind === "browser"
                                        onClicked: {
                                            const slug = App.recordTaskBrowserStep(nameField.text + "-" + (index + 1),
                                                                                   model.ref.length > 0 ? model.ref : "https://")
                                            if (slug.length > 0) stepsModel.setProperty(index, "ref", slug)
                                        }
                                    }
                                    LcButton {
                                        text: "✕"
                                        secondary: true
                                        onClicked: stepsModel.remove(index)
                                    }
                                }
                                LcTextField {
                                    Layout.fillWidth: true
                                    text: model.ref
                                    placeholderText: "Referencia (URL, comando, nombre de skill grabado...)"
                                    onTextChanged: stepsModel.setProperty(index, "ref", text)
                                }
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

                    // ── Permisos de acceso ──
                    Text { text: "Permisos de acceso a archivos"; color: Theme.textSecondary; font.pixelSize: 12 }
                    LcComboBox {
                        id: permScopeCombo
                        Layout.fillWidth: true
                        model: ["Solo el proyecto (seguro)", "Una o varias carpetas específicas", "Toda la PC"]
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.textMuted
                        font.pixelSize: 11
                        text: permScopeCombo.currentIndex === 2
                              ? "El agente podrá leer y escribir en cualquier ruta del disco. Usalo solo si confiás en el proceso."
                              : (permScopeCombo.currentIndex === 1
                                 ? "El agente podrá leer/escribir dentro de las carpetas elegidas (además del proyecto)."
                                 : "El agente queda confinado a la carpeta de trabajo del perfil/agente.")
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: permScopeCombo.currentIndex === 1
                        spacing: 6
                        Repeater {
                            model: permFoldersModel
                            delegate: RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Text {
                                    Layout.fillWidth: true
                                    text: model.path
                                    color: Theme.textPrimary
                                    font.pixelSize: 12
                                    elide: Text.ElideMiddle
                                }
                                LcButton { text: "✕"; secondary: true; onClicked: permFoldersModel.remove(index) }
                            }
                        }
                        LcButton {
                            text: "+ Agregar carpeta…"
                            secondary: true
                            onClicked: permFolderDlg.open()
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

                    LcButton {
                        text: "Previsualizar prompt del agente"
                        secondary: true
                        onClicked: {
                            previewArea.text = composePreview()
                            previewBox.visible = true
                        }
                    }
                    Rectangle {
                        id: previewBox
                        visible: false
                        Layout.fillWidth: true
                        Layout.preferredHeight: 140
                        radius: 6
                        color: Theme.inputBg
                        border.color: Theme.borderColor
                        ScrollView {
                            anchors { fill: parent; margins: 8 }
                            TextArea {
                                id: previewArea
                                readOnly: true
                                wrapMode: TextArea.Wrap
                                color: Theme.textSecondary
                                font { family: "Consolas"; pixelSize: 12 }
                                background: null
                            }
                        }
                    }
                    Item { Layout.preferredHeight: 8; Layout.fillWidth: true }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                color: Theme.popupHeaderBg
                radius: 12
                Rectangle { anchors.top: parent.top; width: parent.width; height: 12; color: Theme.popupHeaderBg }
                Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.popupHeaderBorder }
                Row {
                    anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                    spacing: 10
                    LcButton { text: "Cancelar"; secondary: true; onClicked: editor.close() }
                    LcButton { text: "Guardar"; onClicked: editor.saveAndClose() }
                }
            }
        }
    }

    // ── Editor de Automatización ──
    Popup {
        id: autoEditor
        modal: true
        parent: Overlay.overlay
        closePolicy: Popup.CloseOnEscape
        width: Math.min(680, Math.max(480, root.width - 100))
        height: Math.min(640, Math.max(440, root.height - 100))
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        padding: 0

        function saveAndClose() {
            if (!autoProcessCombo.currentValue) { autoError.text = "Elegí un proceso."; return }
            App.automationStore.save(root.autoEditId, {
                name: autoNameField.text.length > 0 ? autoNameField.text
                        : root.processName(autoProcessCombo.currentValue),
                processId: autoProcessCombo.currentValue,
                scheduleEnabled: autoSchedEnabled.checked,
                scheduleCron: freqCombo.currentIndex === 4 ? cronField.text : "",
                scheduleSpec: root.buildScheduleSpec(),
                silentUnlessError: autoSilent.checked
            })
            close()
        }

        background: Rectangle {
            color: Theme.popupBg
            radius: 12
            border.color: Theme.popupBorderColor
            border.width: 1
        }
        Overlay.modal: Rectangle { color: Theme.overlayColor }

        contentItem: ColumnLayout {
            spacing: 0
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                color: Theme.popupHeaderBg
                radius: 12
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 12; color: Theme.popupHeaderBg }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.popupHeaderBorder }
                Text {
                    anchors { left: parent.left; leftMargin: 22; verticalCenter: parent.verticalCenter }
                    text: root.autoEditId.length > 0 ? "Editar programación" : "Nueva programación"
                    font { pixelSize: 14; bold: true }
                    color: Theme.textPrimary
                }
            }

            ScrollView {
                id: autoScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ColumnLayout {
                    width: Math.max(0, autoScroll.width - 36)
                    spacing: 10
                    x: 18
                    Item { Layout.preferredHeight: 6; Layout.fillWidth: true }

                    Text { text: "Proceso a ejecutar"; color: Theme.textSecondary; font.pixelSize: 12 }
                    LcComboBox {
                        id: autoProcessCombo
                        Layout.fillWidth: true
                        model: App.taskStore
                        textRole: "name"
                        valueRole: "id"
                        function selectProcess(pid) {
                            const idx = indexOfValue(pid)
                            currentIndex = idx >= 0 ? idx : 0
                        }
                    }

                    Text { text: "Nombre de la automatización (opcional)"; color: Theme.textSecondary; font.pixelSize: 12 }
                    LcTextField {
                        id: autoNameField
                        Layout.fillWidth: true
                        placeholderText: "Por defecto toma el nombre del proceso"
                    }

                    CheckBox {
                        id: autoSchedEnabled
                        checked: true
                        text: "Programación activa"
                        contentItem: Text {
                            text: autoSchedEnabled.text; color: Theme.textSecondary; font.pixelSize: 12
                            leftPadding: autoSchedEnabled.indicator.width + 6; verticalAlignment: Text.AlignVCenter
                        }
                    }

                    CheckBox {
                        id: autoSilent
                        text: "Ejecutar en silencio salvo error"
                        contentItem: Text {
                            text: autoSilent.text; color: Theme.textSecondary; font.pixelSize: 12
                            leftPadding: autoSilent.indicator.width + 6; verticalAlignment: Text.AlignVCenter
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Text { text: "Frecuencia"; color: Theme.textSecondary; font.pixelSize: 12; Layout.preferredWidth: 90 }
                        LcComboBox {
                            id: freqCombo
                            Layout.fillWidth: true
                            model: ["Diario", "Semanal", "Mensual", "Cada N meses", "Avanzado (cron)"]
                        }
                    }

                    // Hora (todas las frecuencias menos cron)
                    RowLayout {
                        Layout.fillWidth: true
                        visible: freqCombo.currentIndex !== 4
                        spacing: 10
                        Text { text: "Hora"; color: Theme.textSecondary; font.pixelSize: 12; Layout.preferredWidth: 90 }
                        SpinBox { id: schedHour; from: 0; to: 23; value: 9; editable: true }
                        Text { text: ":"; color: Theme.textSecondary }
                        SpinBox { id: schedMinute; from: 0; to: 59; value: 0; editable: true }
                    }

                    // Semanal: días
                    RowLayout {
                        Layout.fillWidth: true
                        visible: freqCombo.currentIndex === 1
                        spacing: 6
                        Text { text: "Días"; color: Theme.textSecondary; font.pixelSize: 12; Layout.preferredWidth: 90 }
                        Repeater {
                            id: weekdayChecks
                            model: ["Dom", "Lun", "Mar", "Mié", "Jue", "Vie", "Sáb"]
                            delegate: CheckBox {
                                property bool checkedProxy: checked
                                text: modelData
                                contentItem: Text {
                                    text: modelData; color: Theme.textSecondary; font.pixelSize: 11
                                    leftPadding: parent.indicator.width + 3; verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                    }

                    // Mensual
                    RowLayout {
                        Layout.fillWidth: true
                        visible: freqCombo.currentIndex === 2
                        spacing: 10
                        Text { text: "Cuándo"; color: Theme.textSecondary; font.pixelSize: 12; Layout.preferredWidth: 90 }
                        LcComboBox {
                            id: monthlyKindCombo
                            Layout.preferredWidth: 150
                            model: ["Por fecha (día N)", "Por posición"]
                        }
                        SpinBox {
                            id: monthDaySpin
                            visible: monthlyKindCombo.currentIndex === 0 || freqCombo.currentIndex === 3
                            from: 1; to: 31; value: 1; editable: true
                        }
                        LcComboBox {
                            id: nthCombo
                            visible: monthlyKindCombo.currentIndex === 1 && freqCombo.currentIndex === 2
                            Layout.preferredWidth: 110
                            model: ["primer", "segundo", "tercer", "cuarto", "último"]
                        }
                        LcComboBox {
                            id: nthWeekdayCombo
                            visible: monthlyKindCombo.currentIndex === 1 && freqCombo.currentIndex === 2
                            Layout.preferredWidth: 90
                            model: ["Dom", "Lun", "Mar", "Mié", "Jue", "Vie", "Sáb"]
                        }
                    }

                    // Cada N meses
                    RowLayout {
                        Layout.fillWidth: true
                        visible: freqCombo.currentIndex === 3
                        spacing: 10
                        Text { text: "Cada"; color: Theme.textSecondary; font.pixelSize: 12; Layout.preferredWidth: 90 }
                        SpinBox { id: everyNSpin; from: 1; to: 24; value: 2; editable: true }
                        Text { text: "mes(es), desde el mes"; color: Theme.textSecondary; font.pixelSize: 12 }
                        SpinBox { id: startMonthSpin; from: 1; to: 12; value: 1; editable: true }
                    }

                    // Avanzado (cron)
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: freqCombo.currentIndex === 4
                        spacing: 4
                        LcTextField {
                            id: cronField
                            Layout.fillWidth: true
                            placeholderText: "0 9 * * *  (min hora dia mes diaSem)"
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: Theme.textMuted
                            font.pixelSize: 11
                            text: "Formato: min hora diaMes mes diaSem. Ej: 0 9 * * * = 9:00 todos los días · "
                                + "*/15 9-17 * * 1-5 = cada 15 min, 9-17h, lun-vie. Domingo = 0 o 7."
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.textMuted
                        font.pixelSize: 11
                        text: "El companion del scheduler sigue activo con la ventana cerrada; al vencer una programación despierta la app e inicia el perfil del proceso."
                    }
                    Text {
                        id: autoError
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: ""
                        color: Theme.btnDangerBg
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
                    Item { Layout.preferredHeight: 8; Layout.fillWidth: true }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                color: Theme.popupHeaderBg
                radius: 12
                Rectangle { anchors.top: parent.top; width: parent.width; height: 12; color: Theme.popupHeaderBg }
                Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.popupHeaderBorder }
                Row {
                    anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                    spacing: 10
                    LcButton { text: "Cancelar"; secondary: true; onClicked: autoEditor.close() }
                    LcButton { text: "Guardar"; onClicked: autoEditor.saveAndClose() }
                }
            }
        }
    }

    // Compone una vista previa local equivalente a TaskStore::composePrompt.
    function composePreview() {
        let out = "Ejecutá el siguiente proceso guardado de forma autónoma.\n"
        if (prePromptField.text) out += "\nPreprompt operativo:\n" + prePromptField.text + "\n"
        if (nameField.text) out += "Proceso: " + nameField.text + "\n"
        if (descField.text) out += "Objetivo: " + descField.text + "\n"
        if (stepsModel.count > 0) {
            out += "\nPasos de referencia (grabados en una corrida previa):\n"
            for (var i = 0; i < stepsModel.count; ++i) {
                const s = stepsModel.get(i)
                out += (i + 1) + ". [" + s.kind + "] " + s.intent + (s.ref ? " (ref: " + s.ref + ")" : "") + "\n"
            }
        }
        out += "\nIMPORTANTE: los pasos son una guía, no un guion literal. Entendé QUÉ se busca y POR QUÉ; si algo cambió de lugar, adaptate y logrueá el objetivo igual."
        if (postPromptField.text) out += "\n\nPostprompt de verificación posterior:\n" + postPromptField.text
        return out
    }

    Connections {
        target: App
        function onTaskRunFinished(id, name, status, summary, silentUnlessError) {
            if (silentUnlessError && status !== "error")
                return
            resultDialog.taskId = id
            resultDialog.taskName = name || id
            resultDialog.status = status
            resultDialog.summary = summary
            resultDialog.open()
        }
    }

    LcDialog {
        id: resultDialog
        title: status === "error" ? "Proceso con error" : "Proceso finalizado"
        property string taskId: ""
        property string taskName: ""
        property string status: ""
        property string summary: ""
        implicitWidth: 520
        footer: Rectangle {
            color: Theme.popupHeaderBg
            height: 56
            radius: 12
            Rectangle { anchors.top: parent.top; width: parent.width; height: 12; color: Theme.popupHeaderBg }
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.popupHeaderBorder }
            Row {
                anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                spacing: 10
                LcButton {
                    text: "Ver trabajo"
                    secondary: true
                    enabled: resultDialog.taskId.length > 0
                    onClicked: workDialog.openFor(resultDialog.taskId, resultDialog.taskName)
                }
                LcButton {
                    text: "Reintentar"
                    secondary: true
                    enabled: !App.taskRunning
                    onClicked: { resultDialog.close(); App.runTask(resultDialog.taskId) }
                }
                LcButton { text: "Cerrar"; onClicked: resultDialog.close() }
            }
        }
        contentItem: ColumnLayout {
            spacing: 8
            Text {
                Layout.fillWidth: true
                text: resultDialog.taskName
                color: Theme.textPrimary
                font { pixelSize: 14; bold: true }
                wrapMode: Text.Wrap
            }
            Text {
                Layout.fillWidth: true
                text: "Estado: " + (resultDialog.status === "ok" ? "correcto" : resultDialog.status)
                color: resultDialog.status === "error" ? Theme.errorText : Theme.accent
                font.pixelSize: 12
            }
            Text {
                Layout.fillWidth: true
                text: resultDialog.summary || "Sin resumen disponible."
                color: Theme.textSecondary
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }
        }
    }

    Popup {
        id: workDialog
        modal: true
        parent: Overlay.overlay
        closePolicy: Popup.CloseOnEscape
        width: Math.min(980, Math.max(640, root.width - 120))
        height: Math.min(760, Math.max(520, root.height - 100))
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        padding: 0

        property string taskId: ""
        property string taskName: ""
        property string workText: ""

        function openFor(id, name) {
            taskId = id
            taskName = name || id
            workText = App.taskRunWorkLog(id)
            open()
        }

        background: Rectangle {
            color: Theme.popupBg
            radius: 12
            border.color: Theme.popupBorderColor
            border.width: 1
        }
        Overlay.modal: Rectangle { color: Theme.overlayColor }

        contentItem: ColumnLayout {
            spacing: 0
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                color: Theme.popupHeaderBg
                radius: 12
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 12; color: Theme.popupHeaderBg }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.popupHeaderBorder }
                Text {
                    anchors { left: parent.left; leftMargin: 22; verticalCenter: parent.verticalCenter }
                    text: "Trabajo del proceso"
                    font { pixelSize: 14; bold: true }
                    color: Theme.textPrimary
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 18
                spacing: 10
                Text {
                    Layout.fillWidth: true
                    text: workDialog.taskName
                    color: Theme.textPrimary
                    font { pixelSize: 13; bold: true }
                    wrapMode: Text.Wrap
                }
                Text {
                    Layout.fillWidth: true
                    text: "Traza registrada: prompts enviados, eventos del agente, llamadas/resultados de tools, errores y respuesta final cuando el backend los emite."
                    color: Theme.textMuted
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    TextArea {
                        text: workDialog.workText
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextArea.Wrap
                        color: Theme.textPrimary
                        font.family: "Consolas"
                        font.pixelSize: 11
                        background: Rectangle {
                            radius: 6
                            color: Theme.inputBg
                            border.color: Theme.inputBorderColor
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                color: Theme.popupHeaderBg
                radius: 12
                Rectangle { anchors.top: parent.top; width: parent.width; height: 12; color: Theme.popupHeaderBg }
                Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.popupHeaderBorder }
                Row {
                    anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                    spacing: 10
                    LcButton {
                        text: "Copiar"
                        secondary: true
                        enabled: workDialog.workText.length > 0
                        onClicked: App.copyToClipboard(workDialog.workText)
                    }
                    LcButton { text: "Cerrar"; onClicked: workDialog.close() }
                }
            }
        }
    }

    // ── Historial de corridas (Proceso o Programación) ──
    Popup {
        id: historyDialog
        modal: true
        parent: Overlay.overlay
        closePolicy: Popup.CloseOnEscape
        width: Math.min(980, Math.max(640, root.width - 120))
        height: Math.min(760, Math.max(520, root.height - 100))
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        padding: 0

        property string ownerId: ""
        property string ownerName: ""
        property var runs: []
        property int selectedRun: -1
        property int baselineRun: -1
        property var metricComparison: ({})

        function fmt(iso) {
            if (!iso) return "—"
            const d = new Date(iso)
            return isNaN(d.getTime()) ? iso : Qt.formatDateTime(d, "yyyy-MM-dd HH:mm:ss")
        }
        function statusColor(s) {
            return s === "error" ? Theme.errorText : (s === "ok" ? Theme.accent : Theme.textSecondary)
        }
        function openFor(id, name) {
            ownerId = id
            ownerName = name || id
            runs = App.runHistory(id)
            selectedRun = runs.length > 0 ? 0 : -1
            baselineRun = runs.length > 1 ? 1 : -1
            metricComparison = baselineRun >= 0
                ? App.compareTaskRunMetrics(ownerId, baselineRun, selectedRun) : ({})
            open()
        }

        background: Rectangle {
            color: Theme.popupBg; radius: 12
            border.color: Theme.popupBorderColor; border.width: 1
        }
        Overlay.modal: Rectangle { color: Theme.overlayColor }

        contentItem: ColumnLayout {
            spacing: 0
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                color: Theme.popupHeaderBg
                radius: 12
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 12; color: Theme.popupHeaderBg }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.popupHeaderBorder }
                Text {
                    anchors { left: parent.left; leftMargin: 22; verticalCenter: parent.verticalCenter }
                    text: "Historial · " + historyDialog.ownerName
                    font { pixelSize: 14; bold: true }
                    color: Theme.textPrimary
                    elide: Text.ElideRight
                    width: parent.width - 44
                }
            }

            Text {
                visible: historyDialog.runs.length === 0
                Layout.fillWidth: true
                Layout.margins: 20
                text: "Todavía no hay corridas registradas. Ejecutá el proceso/programación y su inicio, estado y reporte completo quedarán acá."
                color: Theme.textMuted
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }

            RowLayout {
                visible: historyDialog.runs.length > 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 14
                spacing: 12

                // Lista de corridas.
                Rectangle {
                    Layout.preferredWidth: 280
                    Layout.fillHeight: true
                    radius: 8
                    color: Theme.inputBg
                    border.color: Theme.inputBorderColor
                    ListView {
                        anchors { fill: parent; margins: 6 }
                        clip: true
                        model: historyDialog.runs
                        delegate: Rectangle {
                            width: ListView.view.width
                            height: 56
                            radius: 6
                            color: index === historyDialog.selectedRun ? Theme.surfaceBg : "transparent"
                            ColumnLayout {
                                anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                                spacing: 2
                                Text {
                                    text: historyDialog.fmt(modelData.startedAt)
                                    color: Theme.textPrimary
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: (modelData.status === "ok" ? "correcto"
                                          : (modelData.status || "—"))
                                          + "  ·  " + (modelData.source === "programacion" ? "programada" : "manual")
                                    color: historyDialog.statusColor(modelData.status)
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    historyDialog.selectedRun = index
                                    historyDialog.metricComparison = historyDialog.baselineRun >= 0
                                        ? App.compareTaskRunMetrics(historyDialog.ownerId,
                                            historyDialog.baselineRun, index) : ({})
                                }
                            }
                        }
                    }
                }

                // Detalle de la corrida seleccionada.
                ColumnLayout {
                    id: detailCol
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 8
                    property var run: historyDialog.selectedRun >= 0
                                      ? historyDialog.runs[historyDialog.selectedRun] : null

                    Text {
                        Layout.fillWidth: true
                        visible: detailCol.run !== null
                        text: detailCol.run
                              ? ("Inicio: " + historyDialog.fmt(detailCol.run.startedAt)
                                 + "   ·   Fin: " + historyDialog.fmt(detailCol.run.finishedAt)
                                 + "   ·   Estado: " + (detailCol.run.status === "ok" ? "correcto" : (detailCol.run.status || "—")))
                              : ""
                        color: Theme.textSecondary
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: metricsRow.implicitHeight + 18
                        visible: detailCol.run && detailCol.run.metrics
                        radius: 6
                        color: Theme.inputBg
                        border.color: Theme.inputBorderColor
                        RowLayout {
                            id: metricsRow
                            anchors { fill: parent; margins: 9 }
                            spacing: 12
                            Text {
                                Layout.fillWidth: true
                                color: Theme.textSecondary
                                font.pixelSize: 11
                                text: detailCol.run && detailCol.run.metrics
                                    ? ("Tokens prompt: " + (detailCol.run.metrics.promptTokens || 0)
                                       + " · generados: " + (detailCol.run.metrics.generatedTokens || 0)
                                       + " · tools: " + (detailCol.run.metrics.toolCalls || 0)
                                       + " · tiempo: " + Number(detailCol.run.metrics.wallMs || 0).toFixed(0) + " ms") : ""
                            }
                            LcButton {
                                text: historyDialog.baselineRun === historyDialog.selectedRun
                                      ? "Baseline" : "Usar como baseline"
                                secondary: true
                                onClicked: {
                                    historyDialog.baselineRun = historyDialog.selectedRun
                                    historyDialog.metricComparison = ({})
                                }
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: historyDialog.metricComparison
                                 && !historyDialog.metricComparison.error
                                 && historyDialog.baselineRun !== historyDialog.selectedRun
                        color: Theme.accent
                        font.pixelSize: 11
                        text: visible
                            ? ("A/B vs baseline · prompt "
                               + Number(historyDialog.metricComparison.promptTokensChangePct || 0).toFixed(1) + "%"
                               + " · tiempo " + Number(historyDialog.metricComparison.wallMsChangePct || 0).toFixed(1) + "%"
                               + " · bytes tools " + Number(historyDialog.metricComparison.toolBytesChangePct || 0).toFixed(1) + "%") : ""
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: detailCol.run !== null && !!detailCol.run.summary
                        text: detailCol.run ? ("Resumen: " + detailCol.run.summary) : ""
                        color: Theme.textMuted
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
                    // Run-report por paso: qué herramientas corrió y con qué resultado.
                    Text {
                        Layout.fillWidth: true
                        visible: detailCol.run && detailCol.run.report && detailCol.run.report.length > 0
                        text: "Pasos ejecutados"
                        color: Theme.textSecondary
                        font.pixelSize: 12
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        visible: detailCol.run && detailCol.run.report && detailCol.run.report.length > 0
                        Layout.preferredHeight: Math.min(140, 8 + reportCol.implicitHeight)
                        radius: 6
                        color: Theme.inputBg
                        border.color: Theme.inputBorderColor
                        ScrollView {
                            anchors { fill: parent; margins: 4 }
                            clip: true
                            ColumnLayout {
                                id: reportCol
                                width: parent.width
                                spacing: 2
                                Repeater {
                                    model: detailCol.run ? detailCol.run.report : []
                                    delegate: RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8
                                        Text {
                                            text: (modelData.ok ? "✓" : "✗")
                                            color: modelData.ok ? Theme.accent : Theme.errorText
                                            font.pixelSize: 12
                                        }
                                        Text {
                                            text: modelData.n + ". " + modelData.tool
                                            color: Theme.textPrimary
                                            font.pixelSize: 11
                                            font.family: "Consolas"
                                        }
                                        Text {
                                            text: modelData.summary || ""
                                            color: Theme.textMuted
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                    }
                                }
                            }
                        }
                    }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        TextArea {
                            text: detailCol.run ? (detailCol.run.log || "Sin traza registrada.") : ""
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextArea.Wrap
                            color: Theme.textPrimary
                            font.family: "Consolas"
                            font.pixelSize: 11
                            background: Rectangle {
                                radius: 6; color: Theme.inputBg; border.color: Theme.inputBorderColor
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                color: Theme.popupHeaderBg
                radius: 12
                Rectangle { anchors.top: parent.top; width: parent.width; height: 12; color: Theme.popupHeaderBg }
                Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.popupHeaderBorder }
                Row {
                    anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                    spacing: 10
                    LcButton {
                        text: "Exportar evidencia"
                        secondary: true
                        enabled: historyDialog.runs.length > 0
                        onClicked: App.exportRunEvidence(historyDialog.ownerId)
                    }
                    LcButton {
                        text: "Copiar reporte"
                        secondary: true
                        enabled: historyDialog.selectedRun >= 0
                        onClicked: {
                            const r = historyDialog.runs[historyDialog.selectedRun]
                            if (r) App.copyToClipboard(r.log || "")
                        }
                    }
                    LcButton { text: "Cerrar"; onClicked: historyDialog.close() }
                }
            }
        }
    }

    Popup {
        id: templatePopup
        modal: true
        parent: Overlay.overlay
        width: Math.min(620, root.width - 80)
        height: Math.min(480, root.height - 80)
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        padding: 18
        property string artifactId: ""
        property string processName: ""
        property var rows: []
        property string statusText: ""
        property string replaceFile: ""
        property string variantBase: ""
        function refresh() { rows = App.automationTemplates(artifactId) }
        function openFor(id, name) {
            artifactId = id; processName = name; statusText = ""; refresh(); open()
        }
        background: Rectangle { color: Theme.popupBg; radius: 12; border.color: Theme.popupBorderColor }
        contentItem: ColumnLayout {
            spacing: 10
            Text { text: "Plantillas visuales · " + templatePopup.processName; color: Theme.textPrimary; font.bold: true }
            Text {
                Layout.fillWidth: true
                text: "Teach las usa después de UI Automation y OCR. Probar no hace clic; sólo busca la imagen en el alcance actual."
                color: Theme.textMuted; wrapMode: Text.Wrap; font.pixelSize: 11
            }
            ListView {
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                model: templatePopup.rows
                delegate: Rectangle {
                    width: ListView.view.width; height: 66; radius: 6; color: Theme.cardBg
                    RowLayout {
                        anchors.fill: parent; anchors.margins: 8; spacing: 8
                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: modelData.file || "plantilla"; color: Theme.textPrimary; elide: Text.ElideMiddle; Layout.fillWidth: true }
                            Text { text: (modelData.width || 0) + "×" + (modelData.height || 0) + " · umbral " + (modelData.threshold || 0.88); color: Theme.textMuted; font.pixelSize: 10 }
                        }
                        LcButton {
                            text: "Probar"; secondary: true
                            onClicked: {
                                const r = App.testAutomationTemplate(templatePopup.artifactId, modelData.file)
                                templatePopup.statusText = r.found && !r.ambiguous
                                    ? "Encontrada · confianza " + Number(r.confidence).toFixed(3)
                                      + (r.recommendRecapture ? " · conviene recapturar o agregar variante" : "")
                                    : (r.error || "No encontrada o ambigua")
                            }
                        }
                        LcButton {
                            text: "Eliminar"; secondary: true
                            onClicked: {
                                if (App.removeAutomationTemplate(templatePopup.artifactId, modelData.file))
                                    templatePopup.refresh()
                            }
                        }
                        LcButton {
                            text: "Reemplazar"; secondary: true
                            onClicked: {
                                templatePopup.replaceFile = modelData.file
                                replaceTemplateDialog.open()
                            }
                        }
                        LcButton {
                            text: "+ Variante"; secondary: true
                            visible: !(modelData.variantOf || "")
                            onClicked: {
                                templatePopup.variantBase = modelData.file
                                variantTemplateDialog.open()
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Text { Layout.fillWidth: true; text: templatePopup.statusText; color: Theme.accent; elide: Text.ElideRight }
                LcButton { text: "Cerrar"; onClicked: templatePopup.close() }
            }
        }
    }

    Popup {
        id: networkPopup
        modal: true
        parent: Overlay.overlay
        width: Math.min(720, root.width - 80)
        height: Math.min(520, root.height - 80)
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        padding: 18
        property string artifactId: ""
        property string processName: ""
        property var rows: []
        property string statusText: ""
        function refresh() { rows = App.automationNetworkDiscoveries(artifactId) }
        function openFor(id, name) {
            artifactId = id; processName = name; statusText = ""; refresh(); open()
        }
        background: Rectangle { color: Theme.popupBg; radius: 12; border.color: Theme.popupBorderColor }
        contentItem: ColumnLayout {
            spacing: 10
            Text { text: "Contratos de red · " + networkPopup.processName; color: Theme.textPrimary; font.bold: true }
            Text {
                Layout.fillWidth: true
                text: "Evidencia pasiva correlacionada con acciones del flujo. No contiene query strings, headers, cookies ni cuerpos y no autoriza a reproducir requests."
                color: Theme.textMuted; wrapMode: Text.Wrap; font.pixelSize: 11
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: networkPopup.rows
                delegate: Rectangle {
                    width: ListView.view.width
                    height: discoveryColumn.implicitHeight + 18
                    radius: 6
                    color: Theme.cardBg
                    ColumnLayout {
                        id: discoveryColumn
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 9 }
                        Text {
                            Layout.fillWidth: true
                            text: (modelData.action || "Acción no rotulada") + " · " + (modelData.at || "")
                            color: Theme.textPrimary; font.bold: true; wrapMode: Text.Wrap
                        }
                        Repeater {
                            model: modelData.endpoints || []
                            Text {
                                Layout.fillWidth: true
                                text: (modelData.method || "?") + " "
                                      + (modelData.origin || "") + (modelData.pathTemplate || "")
                                      + " · estados " + JSON.stringify(modelData.statuses || [])
                                      + " · ×" + (modelData.count || 1)
                                color: Theme.textSecondary
                                font { family: "Consolas"; pixelSize: 10 }
                                wrapMode: Text.Wrap
                            }
                        }
                        Text {
                            text: "Estado: " + (modelData.reviewStatus || "pending")
                            color: Theme.accent; font.pixelSize: 10
                        }
                        RowLayout {
                            LcButton {
                                text: "Aprobar"
                                secondary: true
                                enabled: (modelData.reviewStatus || "pending") !== "approved"
                                onClicked: {
                                    if (App.reviewAutomationNetworkDiscovery(
                                            networkPopup.artifactId, modelData.signature, "approved"))
                                        networkPopup.refresh()
                                }
                            }
                            LcButton {
                                text: "Rechazar"
                                secondary: true
                                enabled: (modelData.reviewStatus || "pending") !== "rejected"
                                onClicked: {
                                    if (App.reviewAutomationNetworkDiscovery(
                                            networkPopup.artifactId, modelData.signature, "rejected"))
                                        networkPopup.refresh()
                                }
                            }
                            Item { Layout.fillWidth: true }
                        }
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                visible: networkPopup.rows.length === 0
                text: "Todavía no hay contratos observados. Ejecutá el proceso: el agente analizará la red después de la acción principal."
                color: Theme.textMuted; wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                Text { Layout.fillWidth: true; text: networkPopup.statusText; color: Theme.accent }
                LcButton {
                    text: "Limpiar"
                    secondary: true
                    enabled: networkPopup.rows.length > 0
                    onClicked: {
                        if (App.clearAutomationNetworkDiscoveries(networkPopup.artifactId)) {
                            networkPopup.statusText = "Descubrimientos eliminados."
                            networkPopup.refresh()
                        }
                    }
                }
                LcButton { text: "Cerrar"; onClicked: networkPopup.close() }
            }
        }
    }

    FileDialog {
        id: replaceTemplateDialog
        title: "Elegir nueva plantilla visual"
        nameFilters: ["Imágenes (*.png *.jpg *.jpeg *.bmp *.webp)"]
        onAccepted: {
            const ok = App.replaceAutomationTemplate(templatePopup.artifactId,
                templatePopup.replaceFile, selectedFile.toString())
            templatePopup.statusText = ok ? "Plantilla reemplazada." : "No se pudo reemplazar la plantilla."
            templatePopup.refresh()
        }
    }

    FileDialog {
        id: variantTemplateDialog
        title: "Agregar variante visual"
        nameFilters: ["Imágenes (*.png *.jpg *.jpeg *.bmp *.webp)"]
        onAccepted: {
            const ok = App.addAutomationTemplateVariant(templatePopup.artifactId,
                templatePopup.variantBase, selectedFile.toString())
            templatePopup.statusText = ok ? "Variante agregada." : "No se pudo agregar la variante."
            templatePopup.refresh()
        }
    }

    LcDialog {
        id: delConfirm
        title: "Eliminar proceso"
        property string taskId: ""
        property string taskName: ""
        implicitWidth: 420
        onAccepted: App.taskStore.remove(taskId)
        contentItem: ColumnLayout {
            Text {
                Layout.fillWidth: true   // un Text suelto como contentItem no toma ancho → cuerpo invisible
                text: "¿Eliminar el proceso \"" + delConfirm.taskName + "\"? Las automatizaciones que lo usen quedarán sin proceso. No se puede deshacer."
                color: Theme.textPrimary
                wrapMode: Text.WordWrap
            }
        }
    }

    LcDialog {
        id: delAutoConfirm
        title: "Eliminar programación"
        property string autoId: ""
        property string autoName: ""
        implicitWidth: 420
        onAccepted: App.automationStore.remove(autoId)
        contentItem: ColumnLayout {
            Text {
                Layout.fillWidth: true   // un Text suelto como contentItem no toma ancho → cuerpo invisible
                text: "¿Eliminar la programación \"" + delAutoConfirm.autoName + "\"? El proceso enlazado no se borra. No se puede deshacer."
                color: Theme.textPrimary
                wrapMode: Text.WordWrap
            }
        }
    }
}
