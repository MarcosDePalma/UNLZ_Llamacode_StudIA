import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import LlamaCode 1.0

Item {
    id: root

    property string selectedLaunchId: ""
    property string resolvedAdapter: ""
    property string resolvedAdapterLabel: ""
    property int currentView: 0   // 0 = Vista Agente, 1 = Vista terminal
    property double lastAgentActivityMs: Date.now()
    property int idleSeconds: 0
    property var agentAttachments: []
    property string lastProfileSuggestionKind: ""
    property var portableSkillRows: []
    property var selectedPortableSkill: ({})
    property bool restoringSessionModes: false
    property string restoredSessionModesId: ""
    property string nativeSaveRunId: ""
    property string nativeSaveRelativePath: ""

    function sessionModeKey(sessionId, field) {
        return "agent/sessionModes/" + sessionId + "/" + field
    }

    function persistCurrentSessionModes() {
        const sessionId = App.opencodeSessionId
        if (restoringSessionModes || !sessionId || sessionId.length === 0) return
        App.writeSetting(sessionModeKey(sessionId, "approvalMode"), App.agentApprovalMode)
        App.writeSetting(sessionModeKey(sessionId, "agentProfileId"), App.activeAgentProfileId)
    }

    function restoreCurrentSessionModes() {
        const sessionId = App.opencodeSessionId
        if (!sessionId || sessionId.length === 0 || sessionId === restoredSessionModesId) return
        restoringSessionModes = true

        // Aplicar primero el nivel: un perfil puede definir su aprobación por
        // defecto. La política guardada se restaura después y tiene la última palabra.
        const savedProfile = String(App.readSetting(
            sessionModeKey(sessionId, "agentProfileId"), ""))
        if (savedProfile.length > 0)
            App.activeAgentProfileId = savedProfile

        const savedApproval = String(App.readSetting(
            sessionModeKey(sessionId, "approvalMode"), ""))
        if (savedApproval.length > 0)
            App.agentApprovalMode = savedApproval

        restoredSessionModesId = sessionId
        restoringSessionModes = false
        // Una sesión nueva o legacy hereda el modo visible actual y desde este
        // momento ya cuenta con una preferencia propia.
        persistCurrentSessionModes()
    }

    // App.agentMessages es QVariantList y cada NOTIFY crea una instancia nueva.
    // Mantener un ListModel estable evita que ListView se vacíe durante un frame
    // y fuerce contentY al inicio al enviar o actualizar un mensaje.
    ListModel {
        id: agentMessageUiModel
        dynamicRoles: true
    }

    function sameAgentMessage(a, b) {
        if (!a || !b) return false
        const fields = ["role", "content", "typing", "status", "createdAt",
                        "completedAt", "tokens", "elapsedMs", "tps", "reverted",
                        "diff", "absPath", "path", "ok", "name", "command",
                        "output", "title"]
        for (let i = 0; i < fields.length; ++i) {
            const key = fields[i]
            if ((a[key] ?? null) !== (b[key] ?? null))
                return false
        }
        return true
    }

    function syncAgentMessageModel() {
        const source = App.agentMessages
        const shared = Math.min(agentMessageUiModel.count, source.length)
        for (let i = 0; i < shared; ++i) {
            if (!sameAgentMessage(agentMessageUiModel.get(i).entry, source[i]))
                agentMessageUiModel.setProperty(i, "entry", source[i])
        }
        while (agentMessageUiModel.count > source.length)
            agentMessageUiModel.remove(agentMessageUiModel.count - 1)
        for (let i = shared; i < source.length; ++i)
            agentMessageUiModel.append({entry: source[i]})
    }

    // ── Ancho del panel de sesiones (redimensionable + persistente) ──────
    property int sessionsPanelWidth: 220
    property int sessionsPanelMin: 160
    property int sessionsPanelMax: Math.max(sessionsPanelMin, Math.min(520, width - 480))
    function clampSessionsWidth(w) {
        return Math.max(sessionsPanelMin, Math.min(sessionsPanelMax, Math.round(w)))
    }
    property int _savedSessionsWidth: parseInt(App.readSetting("agentSessionsPanelWidth", "0")) || 0
    property bool _spRestored: false
    function tryRestoreSessionsPanel() {
        if (_spRestored) return
        if (_savedSessionsWidth > 0 && sessionsPanelMax > sessionsPanelMin) {
            sessionsPanelWidth = clampSessionsWidth(_savedSessionsWidth)
            _spRestored = true
        }
    }
    onSessionsPanelMaxChanged: {
        if (!_spRestored) tryRestoreSessionsPanel()
        else sessionsPanelWidth = clampSessionsWidth(sessionsPanelWidth)
    }

    // @-mentions: estado del autocompletar de archivos.
    property var mentionSuggestions: []
    property int mentionStart: -1      // índice del '@' en el texto
    property int mentionActive: 0      // sugerencia resaltada

    function fileName(p) { return p.split(/[\\/]/).pop() }

    // Recalcula el popup de @-mentions según el token bajo el cursor.
    function updateMention() {
        const txt = agentInput.text
        const pos = agentInput.cursorPosition
        let i = pos - 1
        while (i >= 0 && !/\s/.test(txt.charAt(i))) i--
        const tokenStart = i + 1
        const token = txt.substring(tokenStart, pos)
        if (token.length >= 1 && token.charAt(0) === "@") {
            root.mentionStart = tokenStart
            root.mentionSuggestions = App.agentProjectFiles(token.substring(1))
            root.mentionActive = 0
            if (root.mentionSuggestions.length > 0) mentionPopup.open()
            else mentionPopup.close()
        } else {
            root.mentionStart = -1
            mentionPopup.close()
        }
    }

    // Acepta una sugerencia: saca el @token del texto y adjunta el archivo.
    function acceptMention(path) {
        if (root.mentionStart < 0) { mentionPopup.close(); return }
        const txt = agentInput.text
        const before = txt.substring(0, root.mentionStart)
        const after = txt.substring(agentInput.cursorPosition)
        agentInput.text = before + after
        agentInput.cursorPosition = before.length
        if (root.agentAttachments.indexOf(path) < 0)
            root.agentAttachments = root.agentAttachments.concat([path])
        root.mentionStart = -1
        mentionPopup.close()
    }
    function sendAgentNow(text, attachments) {
        if (attachments.length > 0)
            App.sendToAgentWithAttachments(text, attachments)
        else
            App.sendToAgent(text)
        agentInput.text = ""
        root.agentAttachments = []
    }

    // Envío normal (idle): antes ofrece una copia optimizada cuando la consigna
    // difiere materialmente del perfil activo. Nunca cambia el original solo.
    function agentSend() {
        const t = agentInput.text.trim()
        if (t.length === 0 && root.agentAttachments.length === 0) return
        const recommendation = App.profileManager.recommendAgentProfile(t, App.activeAgentProfileId)
        if (recommendation && recommendation.kind
                && recommendation.kind !== root.lastProfileSuggestionKind) {
            root.lastProfileSuggestionKind = recommendation.kind
            profileSuggestionDialog.recommendation = recommendation
            profileSuggestionDialog.pendingText = t
            profileSuggestionDialog.pendingAttachments = root.agentAttachments.slice()
            profileSuggestionDialog.open()
            return
        }
        sendAgentNow(t, root.agentAttachments.slice())
    }

    readonly property bool waitingApproval: (App.agentPendingTool.id ?? "").length > 0
    readonly property bool hasTypingMessage: {
        for (let i = 0; i < App.agentMessages.length; i++) {
            if ((App.agentMessages[i].typing ?? false) === true) return true
        }
        return false
    }
    readonly property string activityLabel: {
        if (App.agentStarting) return "Iniciando"
        if (!App.agentRunning) return "Detenido"
        if (waitingApproval) return "Esperando aprobación"
        if (hasTypingMessage) {
            if (idleSeconds < 12) return "Procesando"
            return "Sin actividad (" + idleSeconds + "s)"
        }
        return "Listo"
    }
    readonly property color activityColor: {
        if (App.agentStarting) return Theme.warnText
        if (!App.agentRunning) return Theme.textMuted
        if (waitingApproval) return Theme.warnText
        if (hasTypingMessage && idleSeconds >= 12) return Theme.errorText
        if (hasTypingMessage) return Theme.successText
        return Theme.textSecondary
    }

    // Confirmación para activar el modo "Super Agente" (acceso total al disco).
    // Perfil cloud sin API key resoluble → pedirla y reintentar el arranque.
    property string _pendingCloudProfile: ""
    Connections {
        target: App
        function onCloudSecretRequired(launchProfileId, keyRef) {
            root._pendingCloudProfile = launchProfileId
            cloudKeyPrompt.keyRef = keyRef
            cloudKeyInput.text = ""
            cloudKeyPrompt.open()
        }
    }
    Dialog {
        id: cloudKeyPrompt
        property string keyRef: ""
        modal: true
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: 460
        height: 230
        leftPadding: 20
        rightPadding: 20
        topPadding: 16
        bottomPadding: 16
        closePolicy: Popup.CloseOnEscape
        background: Rectangle { color: Theme.popupBg; radius: 12; border.color: Theme.popupBorderColor; border.width: 1 }
        Overlay.modal: Rectangle { color: Theme.overlayColor }
        header: Rectangle {
            color: Theme.popupHeaderBg; height: 50; radius: 12
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.popupHeaderBorder }
            Text {
                anchors { left: parent.left; leftMargin: 20; verticalCenter: parent.verticalCenter }
                text: "🔑  API key requerida"
                font.pixelSize: 14
                font.bold: true
                color: Theme.textPrimary
            }
        }
        contentItem: ColumnLayout {
            spacing: 10
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textSecondary; font.pixelSize: 12
                text: "Este perfil cloud necesita la key «" + cloudKeyPrompt.keyRef + "». "
                      + "Se guarda fuera del repo (no se commitea). Tip: también podés exportarla como variable de entorno con ese nombre."
            }
            LcTextField {
                id: cloudKeyInput; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: "sk-…"
            }
        }
        footer: Rectangle {
            color: Theme.popupHeaderBg; height: 50; radius: 12
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.popupHeaderBorder }
            Row {
                anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                spacing: 10
                LcButton { text: "Cancelar"; secondary: true; onClicked: cloudKeyPrompt.close() }
                LcButton {
                    text: "Guardar y arrancar"
                    enabled: cloudKeyInput.text.length > 0 && cloudKeyPrompt.keyRef.length > 0
                    onClicked: {
                        App.setSecret(cloudKeyPrompt.keyRef, cloudKeyInput.text)
                        cloudKeyPrompt.close()
                        App.startAgent(root._pendingCloudProfile)
                    }
                }
            }
        }
    }

    Dialog {
        id: profileSuggestionDialog
        property var recommendation: ({})
        property string pendingText: ""
        property var pendingAttachments: []
        modal: true
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: 500
        height: 210
        leftPadding: 20
        rightPadding: 20
        topPadding: 16
        bottomPadding: 16
        closePolicy: Popup.CloseOnEscape
        background: Rectangle {
            color: Theme.popupBg
            radius: 12
            border.color: Theme.popupBorderColor
            border.width: 1
        }
        Overlay.modal: Rectangle { color: Theme.overlayColor }
        header: Rectangle {
            color: Theme.popupHeaderBg; height: 50; radius: 12
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.popupHeaderBorder
            }
            Text {
                anchors { left: parent.left; leftMargin: 20; verticalCenter: parent.verticalCenter }
                text: "Perfil recomendado: " + (profileSuggestionDialog.recommendation.label || "optimizado")
                color: Theme.textPrimary; font { pixelSize: 14; bold: true }
            }
        }
        contentItem: ColumnLayout {
            spacing: 10
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                color: Theme.textSecondary; font.pixelSize: 12
                text: profileSuggestionDialog.recommendation.reason || ""
            }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                color: Theme.textMuted; font.pixelSize: 11
                text: "Se creará una copia editable del perfil actual: temperatura "
                      + profileSuggestionDialog.recommendation.temperature
                      + " · " + profileSuggestionDialog.recommendation.toolCount + " tools · razonamiento "
                      + (profileSuggestionDialog.recommendation.thinking ? "activo" : "desactivado")
                      + ". El perfil original no cambia."
            }
        }
        footer: Rectangle {
            color: Theme.popupHeaderBg; height: 54; radius: 12
            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: Theme.popupHeaderBorder
            }
            Row {
                anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                spacing: 10
                LcButton {
                    text: "Ahora no"
                    secondary: true
                    onClicked: {
                        profileSuggestionDialog.close()
                        root.sendAgentNow(profileSuggestionDialog.pendingText,
                                          profileSuggestionDialog.pendingAttachments)
                    }
                }
                LcButton {
                    text: "Crear copia y usar"
                    onClicked: {
                        const id = App.profileManager.createRecommendedAgentProfile(
                            App.activeAgentProfileId, profileSuggestionDialog.recommendation.kind)
                        if (id) App.activeAgentProfileId = id
                        profileSuggestionDialog.close()
                        root.sendAgentNow(profileSuggestionDialog.pendingText,
                                          profileSuggestionDialog.pendingAttachments)
                    }
                }
            }
        }
    }

    // Bandeja local de corridas nativas: los estados uncertain se resuelven
    // explícitamente y los entregables sólo salen del sandbox mediante Save As.
    Dialog {
        id: nativeRunsDialog
        modal: true
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: Math.min(900, parent.width - 36)
        height: Math.min(680, parent.height - 36)
        leftPadding: 18; rightPadding: 18; topPadding: 14; bottomPadding: 14
        closePolicy: Popup.CloseOnEscape
        onOpened: App.refreshNativeAgentRuns()
        background: Rectangle {
            color: Theme.popupBg; radius: 12
            border.color: Theme.popupBorderColor; border.width: 1
        }
        Overlay.modal: Rectangle { color: Theme.overlayColor }
        header: Rectangle {
            color: Theme.popupHeaderBg; height: 52; radius: 12
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.popupHeaderBorder }
            Text {
                anchors { left: parent.left; leftMargin: 20; verticalCenter: parent.verticalCenter }
                text: "📦 Inbox de corridas nativas"
                color: Theme.textPrimary; font { pixelSize: 14; bold: true }
            }
        }
        contentItem: ColumnLayout {
            spacing: 9
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: App.nativeUncertainRunCount > 0
                        ? "Hay " + App.nativeUncertainRunCount + " corrida(s) incierta(s): requieren decisión humana."
                        : "Corridas y entregables persistidos localmente."
                    color: App.nativeUncertainRunCount > 0 ? Theme.warnText : Theme.textMuted
                    font.pixelSize: 11; wrapMode: Text.WordWrap
                }
                LcButton {
                    text: "Actualizar"; secondary: true
                    onClicked: App.refreshNativeAgentRuns()
                }
            }
            ListView {
                id: nativeRunsList
                Layout.fillWidth: true; Layout.fillHeight: true
                clip: true; spacing: 6
                model: App.nativeAgentRuns
                delegate: Rectangle {
                    required property var modelData
                    width: nativeRunsList.width; height: 96; radius: 7
                    color: Theme.inputBg; border.color: Theme.borderColor
                    RowLayout {
                        anchors { fill: parent; margins: 9 }
                        spacing: 9
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 2
                            Text {
                                text: (modelData.status || "") + " · " + (modelData.harnessNamespace || "native")
                                color: modelData.status === "completed" ? Theme.successText
                                     : modelData.status === "uncertain" ? Theme.warnText
                                       : modelData.status === "failed" ? Theme.errorText : Theme.textPrimary
                                font { pixelSize: 12; bold: true }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.objective || modelData.detail || "Sin objetivo"
                                color: Theme.textSecondary; font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.workspace || ""
                                color: Theme.textMuted; font.pixelSize: 9; elide: Text.ElideMiddle
                            }
                        }
                        ColumnLayout {
                            Layout.alignment: Qt.AlignVCenter
                            spacing: 4
                            LcButton {
                                text: "Detalle"; secondary: true
                                onClicked: {
                                    nativeRunDetailDialog.runId = modelData.runId || ""
                                    nativeRunDetailDialog.open()
                                }
                            }
                            LcButton {
                                text: "Abrir carpeta"; secondary: true
                                onClicked: App.openNativeAgentRunDirectory(modelData.runId || "")
                            }
                            LcButton {
                                text: "Cerrar incierta"; danger: true
                                visible: modelData.status === "uncertain"
                                onClicked: App.resolveNativeAgentRun(
                                    modelData.runId || "", "cancelled",
                                    "cancelada explícitamente desde Inbox")
                            }
                        }
                    }
                }
                Text {
                    anchors.centerIn: parent; visible: nativeRunsList.count === 0
                    text: "Todavía no hay corridas nativas persistidas."
                    color: Theme.textMuted; font.pixelSize: 11
                }
            }
        }
        footer: null
    }

    Dialog {
        id: nativeRunDetailDialog
        property string runId: ""
        property var run: ({})
        property var manifest: ({})
        property var events: []
        property string errorText: ""
        property bool allowOverwrite: false
        modal: true; parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2); y: Math.round((parent.height - height) / 2)
        width: Math.min(820, parent.width - 36); height: Math.min(600, parent.height - 36)
        leftPadding: 16; rightPadding: 16; topPadding: 14; bottomPadding: 14
        closePolicy: Popup.CloseOnEscape
        onOpened: {
            run = App.nativeAgentRun(runId)
            manifest = App.nativeAgentDeliverableManifest(runId)
            events = App.nativeAgentRunEvents(runId)
            errorText = ""
            allowOverwrite = false
        }
        background: Rectangle { color: Theme.popupBg; radius: 12; border.color: Theme.popupBorderColor }
        Overlay.modal: Rectangle { color: Theme.overlayColor }
        header: Rectangle {
            color: Theme.popupHeaderBg; height: 48; radius: 12
            Text {
                anchors { left: parent.left; leftMargin: 18; verticalCenter: parent.verticalCenter }
                text: "Detalle · " + (nativeRunDetailDialog.runId || "corrida")
                color: Theme.textPrimary; font { pixelSize: 13; bold: true }
            }
        }
        contentItem: ColumnLayout {
            spacing: 8
            Text {
                Layout.fillWidth: true
                text: (nativeRunDetailDialog.run.status || "") + " · "
                    + (nativeRunDetailDialog.run.detail || nativeRunDetailDialog.run.objective || "")
                color: Theme.textSecondary; font.pixelSize: 11; wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                text: nativeRunDetailDialog.manifest && nativeRunDetailDialog.manifest.entries
                    ? "Entregables: " + nativeRunDetailDialog.manifest.entries.length
                    : "No hay entregables capturados para esta corrida."
                color: Theme.textMuted; font.pixelSize: 11
            }
            TextArea {
                Layout.fillWidth: true; Layout.preferredHeight: 92
                readOnly: true
                text: nativeRunDetailDialog.events.length > 0
                    ? nativeRunDetailDialog.events.map(function(event) {
                        return "#" + (event.seq || "?") + " · "
                            + (event.kind || "evento") + " · " + (event.ts || "")
                    }).join("\n") : "Sin eventos registrados."
                color: Theme.textMuted; font.pixelSize: 10
                wrapMode: TextArea.WrapAnywhere
                background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.inputBorderColor }
            }
            Text {
                Layout.fillWidth: true; visible: nativeRunDetailDialog.errorText.length > 0
                text: nativeRunDetailDialog.errorText
                color: Theme.errorText; font.pixelSize: 11; wrapMode: Text.WordWrap
            }
            ListView {
                id: nativeDeliverablesList
                Layout.fillWidth: true; Layout.fillHeight: true
                clip: true; spacing: 5
                model: nativeRunDetailDialog.manifest && nativeRunDetailDialog.manifest.entries
                    ? nativeRunDetailDialog.manifest.entries : []
                delegate: Rectangle {
                    required property var modelData
                    width: nativeDeliverablesList.width; height: 54; radius: 6
                    color: Theme.inputBg; border.color: Theme.borderColor
                    RowLayout {
                        anchors { fill: parent; margins: 8 }
                        Text {
                            Layout.fillWidth: true
                            text: (modelData.status || "") + " · " + (modelData.path || "")
                            color: modelData.stored === false ? Theme.warnText : Theme.textPrimary
                            font.pixelSize: 10; elide: Text.ElideMiddle
                        }
                        LcButton {
                            text: "Guardar como"; secondary: true
                            enabled: modelData.stored !== false
                            onClicked: {
                                root.nativeSaveRunId = nativeRunDetailDialog.runId
                                root.nativeSaveRelativePath = modelData.path || ""
                                nativeSaveDialog.open()
                            }
                        }
                    }
                }
            }
        }
        footer: Rectangle {
            color: Theme.popupHeaderBg; height: 50; radius: 12
            RowLayout {
                anchors {
                    left: parent.left; leftMargin: 14
                    right: parent.right; rightMargin: 14
                    verticalCenter: parent.verticalCenter
                }
                CheckBox {
                    id: nativeOverwriteCheck
                    text: "Permitir reemplazar destino"
                    checked: nativeRunDetailDialog.allowOverwrite
                    onClicked: nativeRunDetailDialog.allowOverwrite = checked
                    contentItem: Text {
                        text: nativeOverwriteCheck.text
                        color: Theme.textMuted; font.pixelSize: 10
                        leftPadding: nativeOverwriteCheck.indicator.width + 5
                    }
                }
                Item { Layout.fillWidth: true }
                LcButton { text: "Cerrar"; secondary: true; onClicked: nativeRunDetailDialog.close() }
            }
        }
    }

    FileDialog {
        id: nativeSaveDialog
        title: "Guardar entregable"
        fileMode: FileDialog.SaveFile
        onAccepted: {
            const path = selectedFile && selectedFile.toLocalFile
                ? selectedFile.toLocalFile() : String(selectedFile)
            if (!App.saveNativeAgentDeliverable(root.nativeSaveRunId,
                                                root.nativeSaveRelativePath, path,
                                                nativeRunDetailDialog.allowOverwrite)) {
                nativeRunDetailDialog.errorText =
                    "No se pudo guardar. El destino no se pisa automáticamente; elegí otra ruta."
            } else {
                nativeRunDetailDialog.errorText = "Entregable guardado en " + path
            }
        }
    }

    // Corridas largas de Claude Code/Codex: el prompt y los logs quedan en un
    // manifiesto durable, aunque el usuario cierre este diálogo o la app.
    Dialog {
        id: managedRunsDialog
        modal: true
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: Math.min(820, parent.width - 36)
        height: Math.min(650, parent.height - 36)
        leftPadding: 18; rightPadding: 18; topPadding: 14; bottomPadding: 14
        closePolicy: Popup.CloseOnEscape
        property string selectedRuntime: "claude"
        property bool applyEdits: false
        property string errorText: ""
        background: Rectangle {
            color: Theme.popupBg; radius: 12
            border.color: Theme.popupBorderColor; border.width: 1
        }
        Overlay.modal: Rectangle { color: Theme.overlayColor }
        header: Rectangle {
            color: Theme.popupHeaderBg; height: 52; radius: 12
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.popupHeaderBorder }
            Text {
                anchors { left: parent.left; leftMargin: 20; verticalCenter: parent.verticalCenter }
                text: "🚀 Corridas administradas"
                color: Theme.textPrimary; font { pixelSize: 14; bold: true }
            }
        }
        contentItem: ColumnLayout {
            spacing: 9
            RowLayout {
                Layout.fillWidth: true
                Text { text: "Runtime"; color: Theme.textSecondary; font.pixelSize: 12 }
                LcComboBox {
                    id: managedRuntimeCombo
                    Layout.preferredWidth: 150
                    model: ["claude", "codex"]
                    currentIndex: managedRunsDialog.selectedRuntime === "codex" ? 1 : 0
                    onActivated: managedRunsDialog.selectedRuntime = currentValue
                }
                Text {
                    Layout.fillWidth: true
                    text: "Workspace: " + (App.currentAgentProjectDir() || "directorio actual")
                    color: Theme.textMuted; font.pixelSize: 11; elide: Text.ElideMiddle
                }
            }
            TextArea {
                id: managedPrompt
                Layout.fillWidth: true; Layout.preferredHeight: 130
                placeholderText: "Describí la revisión o implementación larga…"
                color: Theme.textPrimary; wrapMode: TextArea.Wrap
                background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.inputBorderColor }
            }
            RowLayout {
                Layout.fillWidth: true
                CheckBox {
                    id: managedEditsCheck
                    text: "Permitir ediciones"
                    checked: managedRunsDialog.applyEdits
                    onClicked: managedRunsDialog.applyEdits = checked
                    contentItem: Text { text: managedEditsCheck.text; color: Theme.textPrimary; font.pixelSize: 11
                        leftPadding: managedEditsCheck.indicator.width + 6 }
                }
                Text {
                    Layout.fillWidth: true
                    text: managedRunsDialog.applyEdits
                        ? "Se conserva la aprobación del CLI; no se activa bypass automáticamente."
                        : "Modo plan: lectura y reporte, sin escribir el workspace."
                    color: managedRunsDialog.applyEdits ? Theme.warnText : Theme.textMuted
                    font.pixelSize: 11; wrapMode: Text.WordWrap
                }
                LcButton {
                    text: "Iniciar"
                    enabled: managedPrompt.text.trim().length > 0
                    onClicked: {
                        const id = App.startManagedAgentRun({
                            runtime: managedRunsDialog.selectedRuntime,
                            prompt: managedPrompt.text,
                            workspace: App.currentAgentProjectDir(),
                            applyEdits: managedRunsDialog.applyEdits,
                            approvalMode: App.agentApprovalMode,
                            ownerId: "manual-managed-agent"
                        })
                        if (id.length > 0) {
                            managedPrompt.text = ""
                            managedRunsDialog.errorText = ""
                        } else {
                            managedRunsDialog.errorText = App.managedAgentRunStore.lastError
                        }
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                visible: managedRunsDialog.errorText.length > 0
                text: managedRunsDialog.errorText
                color: Theme.errorText; font.pixelSize: 11; wrapMode: Text.WordWrap
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }
            Text {
                text: "Historial local · " + App.managedAgentRunStore.activeCount + " activa(s)"
                color: Theme.textSecondary; font { pixelSize: 12; bold: true }
            }
            ListView {
                id: managedRunsList
                Layout.fillWidth: true; Layout.fillHeight: true
                clip: true; spacing: 6
                model: App.managedAgentRunStore.runs
                delegate: Rectangle {
                    required property var modelData
                    width: managedRunsList.width; height: 88; radius: 7
                    color: Theme.inputBg; border.color: Theme.borderColor
                    RowLayout {
                        anchors { fill: parent; margins: 9 }
                        spacing: 9
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 2
                            Text {
                                text: (modelData.runtime || "cli") + " · " + (modelData.status || "")
                                      + (modelData.resultStatus ? " · " + modelData.resultStatus : "")
                                color: modelData.resultStatus === "verified" ? Theme.successText
                                     : ["failed", "stale", "timed_out"].indexOf(modelData.status) >= 0
                                       ? Theme.errorText : Theme.textPrimary
                                font { pixelSize: 12; bold: true }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.summary || modelData.workspace || ""
                                color: Theme.textMuted; font.pixelSize: 10; elide: Text.ElideRight
                            }
                            Text {
                                text: modelData.startedAt || ""
                                color: Theme.textMuted; font.pixelSize: 9
                            }
                        }
                        LcButton {
                            text: "Logs"; secondary: true
                            onClicked: {
                                managedLogDialog.runId = modelData.runId || ""
                                managedLogDialog.open()
                            }
                        }
                        LcButton {
                            text: "Abrir"; secondary: true
                            onClicked: App.openManagedAgentRunDirectory(modelData.runId || "")
                        }
                        LcButton {
                            text: "Parar"; danger: true
                            visible: ["starting", "running", "stopping", "verifying"].indexOf(modelData.status) >= 0
                            onClicked: App.stopManagedAgentRun(modelData.runId || "")
                        }
                        LcButton {
                            text: "Reintentar"; secondary: true
                            visible: ["failed", "cancelled", "timed_out", "stale"].indexOf(modelData.status) >= 0
                            onClicked: App.managedAgentRunStore.retryRun(modelData.runId || "")
                        }
                        LcButton {
                            text: "×"; secondary: true
                            visible: ["starting", "running", "stopping", "verifying"].indexOf(modelData.status) < 0
                            onClicked: App.removeManagedAgentRun(modelData.runId || "")
                        }
                    }
                }
                Text {
                    anchors.centerIn: parent; visible: managedRunsList.count === 0
                    text: "Todavía no hay corridas administradas."
                    color: Theme.textMuted; font.pixelSize: 11
                }
            }
        }
        footer: null
    }

    Dialog {
        id: managedLogDialog
        property string runId: ""
        modal: true; parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2); y: Math.round((parent.height - height) / 2)
        width: Math.min(900, parent.width - 36); height: Math.min(620, parent.height - 36)
        leftPadding: 16; rightPadding: 16; topPadding: 14; bottomPadding: 14
        background: Rectangle { color: Theme.popupBg; radius: 12; border.color: Theme.popupBorderColor }
        Overlay.modal: Rectangle { color: Theme.overlayColor }
        contentItem: Flickable {
            clip: true; contentWidth: width; contentHeight: managedLogText.implicitHeight
            Text {
                id: managedLogText
                width: parent.width
                text: App.managedAgentRunLog(managedLogDialog.runId)
                color: Theme.textPrimary; font.family: Theme.codeFont; font.pixelSize: 11
                wrapMode: Text.WrapAnywhere
            }
        }
        footer: Rectangle {
            color: Theme.popupHeaderBg; height: 50; radius: 12
            Row {
                anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                LcButton { text: "Cerrar"; secondary: true; onClicked: managedLogDialog.close() }
            }
        }
    }

    Dialog {
        id: superAgentDialog
        modal: true
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: 460
        height: 260
        closePolicy: Popup.CloseOnEscape

        // Cancelar/cerrar: revertir el combo al modo real actual.
        function revertCombo() {
            approvalModeCombo.currentIndex =
                App.agentApprovalMode === "manual" ? 2
                : App.agentApprovalMode === "auto" ? 0
                : App.agentApprovalMode === "super" ? 3 : 1
        }
        onRejected: revertCombo()

        background: Rectangle {
            color: Theme.popupBg; radius: 12
            border.color: Theme.popupBorderColor; border.width: 1
        }
        Overlay.modal: Rectangle { color: Theme.overlayColor }

        header: Rectangle {
            color: Theme.popupHeaderBg; height: 50; radius: 12
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 12; color: Theme.popupHeaderBg }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1;  color: Theme.popupHeaderBorder }
            Text {
                anchors { left: parent.left; leftMargin: 20; verticalCenter: parent.verticalCenter }
                text: "⚠  Modo Super Agente — riesgoso"
                font { pixelSize: 14; bold: true }
                color: Theme.textPrimary
            }
        }

        footer: Rectangle {
            color: Theme.popupHeaderBg; height: 50; radius: 12
            Rectangle { anchors.top: parent.top; width: parent.width; height: 12; color: Theme.popupHeaderBg }
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1;  color: Theme.popupHeaderBorder }
            Row {
                anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                spacing: 10
                LcButton {
                    text: "Cancelar"; secondary: true
                    onClicked: { superAgentDialog.revertCombo(); superAgentDialog.close() }
                }
                LcButton {
                    text: "Activar"
                    danger: true
                    onClicked: { App.agentApprovalMode = "super"; superAgentDialog.close() }
                }
            }
        }

        contentItem: Item {
            Text {
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 20 }
                text: "El Super Agente tendrá acceso de LECTURA y ESCRITURA a TODAS las " +
                      "carpetas de la computadora y ejecutará comandos de shell SIN pedir " +
                      "permiso.\n\nPuede modificar o borrar archivos fuera del proyecto. " +
                      "Usalo solo si confiás en lo que le pedís.\n\n¿Activar Super Agente?"
                color: Theme.textPrimary
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }
        }
    }

    Dialog {
        id: thinkingRestartDialog
        modal: true
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: Math.min(560, root.width - 48)
        height: 230
        leftPadding: 20
        rightPadding: 20
        topPadding: 16
        bottomPadding: 16
        closePolicy: Popup.CloseOnEscape

        property bool targetEnabled: false
        readonly property bool responseActive: App.agentStreamingIndex >= 0

        background: Rectangle {
            color: Theme.popupBg; radius: 12
            border.color: Theme.popupBorderColor; border.width: 1
        }
        Overlay.modal: Rectangle { color: Theme.overlayColor }

        header: Rectangle {
            color: Theme.popupHeaderBg; height: 50; radius: 12
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 12; color: Theme.popupHeaderBg }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.popupHeaderBorder }
            Text {
                anchors { left: parent.left; leftMargin: 20; verticalCenter: parent.verticalCenter }
                text: "Cambiar pensamiento"
                font { pixelSize: 14; bold: true }
                color: Theme.textPrimary
            }
        }

        contentItem: ColumnLayout {
            spacing: 14
            Text {
                Layout.fillWidth: true
                text: "¿Desea reiniciar el modelo para cambiar la profundidad del pensamiento?"
                color: Theme.textPrimary
                wrapMode: Text.WordWrap
                font.pixelSize: 14
            }
            Text {
                Layout.fillWidth: true
                text: thinkingRestartDialog.targetEnabled
                      ? "Pensar quedará activado. El cambio duro se aplica al relanzar llama-server."
                      : "Pensar quedará desactivado. El cambio duro se aplica al relanzar llama-server."
                color: Theme.textSecondary
                wrapMode: Text.WordWrap
                font.pixelSize: 12
            }
        }

        footer: Rectangle {
            color: Theme.popupHeaderBg; height: 58; radius: 12
            Rectangle { anchors.top: parent.top; width: parent.width; height: 12; color: Theme.popupHeaderBg }
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.popupHeaderBorder }
            Row {
                anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                spacing: 10
                LcButton {
                    text: "No reiniciar"; secondary: true
                    onClicked: {
                        App.applyThinkingChange(thinkingRestartDialog.targetEnabled, "agent", "none")
                        thinkingRestartDialog.close()
                    }
                }
                LcButton {
                    text: "Reiniciar luego de esta respuesta"; secondary: true
                    enabled: thinkingRestartDialog.responseActive && App.serverRunning
                    onClicked: {
                        App.applyThinkingChange(thinkingRestartDialog.targetEnabled, "agent", "after-response")
                        thinkingRestartDialog.close()
                    }
                }
                LcButton {
                    text: "Reiniciar ahora sin esperar"
                    enabled: App.serverRunning
                    onClicked: {
                        App.applyThinkingChange(thinkingRestartDialog.targetEnabled, "agent", "now")
                        thinkingRestartDialog.close()
                    }
                }
            }
        }
    }

    // La página vive en un StackLayout (no se recrea): re-resolver el harness
    // al mostrarla, conservando siempre la última selección explícita de Agente.
    onVisibleChanged: if (visible && selectedLaunchId.length > 0) resolveHarness(selectedLaunchId)

    function projectDirForSection(sectionName) {
        for (let i = 0; i < App.agentSessions.length; i++) {
            if ((App.agentSessions[i].projectName ?? "") === sectionName)
                return App.agentSessions[i].projectDir ?? ""
        }
        return ""
    }

    function resolveHarness(launchId) {
        // Política: todo perfil usa el agente nativo LlamaAgent. Perfiles viejos sin
        // harness ("none") o con Opencode se normalizan a llamaagent (espeja
        // AppController::normalizeHarnessAdapter para que la UI no quede grisada).
        if (!launchId || launchId.length === 0) {
            resolvedAdapter = "llamaagent"; resolvedAdapterLabel = "LlamaAgent"; return
        }
        const lp = App.profileManager.getLaunchProfile(launchId)
        const harnessId = lp.harnessProfileId ?? ""
        let a = ""
        if (harnessId.length > 0)
            a = App.profileManager.getHarness(harnessId).adapter ?? ""
        a = (a || "").trim()
        if (a === "" || a === "none" || a === "opencode")
            a = "llamaagent"
        resolvedAdapter = a
        resolvedAdapterLabel = a === "llamaagent" ? "LlamaAgent" : a
    }

    function estimateTokens(text) {
        const n = String(text ?? "").trim().length
        return n <= 0 ? 0 : Math.ceil(n / 4)
    }

    function formatDuration(ms) {
        const v = Math.max(0, Math.floor(ms || 0))
        if (v < 1000) return v + "ms"
        return (v / 1000).toFixed(2) + "s"
    }

    function formatMeta(modelData) {
        try {
            if (!modelData) return ""
            if (String(modelData.role ?? "") === "diff") return ""
            let ts = Number(modelData.completedAt ?? modelData.createdAt ?? 0)
            if (!isFinite(ts) || ts <= 0) return ""
            if (ts < 1000000000000) ts *= 1000 // si vino en segundos
            const d = new Date(ts)
            if (isNaN(d.getTime())) return ""
            const pad = n => (n < 10 ? "0" : "") + n
            const date = d.getFullYear() + "-" + pad(d.getMonth() + 1) + "-" + pad(d.getDate())
            const hms = pad(d.getHours()) + ":" + pad(d.getMinutes()) + ":" + pad(d.getSeconds())
            const content = String(modelData.content ?? "")
            const rawTokens = Number(modelData.tokens ?? estimateTokens(content))
            const tokens = isFinite(rawTokens) && rawTokens >= 0 ? Math.floor(rawTokens) : estimateTokens(content)
            // El mensaje del usuario no es generación de un LLM: ms/tps no aplican.
            if (String(modelData.role ?? "") === "user")
                return date + " - " + hms + " - tok " + tokens
            let elapsedMs = Number(modelData.elapsedMs ?? 0)
            if (!isFinite(elapsedMs) || elapsedMs < 0) {
                const started = Number(modelData.createdAt ?? 0)
                const ended = Number(modelData.completedAt ?? 0)
                elapsedMs = (isFinite(started) && isFinite(ended) && ended >= started) ? (ended - started) : 0
            }
            const rawTps = Number(modelData.tps ?? (elapsedMs > 0 ? (tokens * 1000.0 / elapsedMs) : 0))
            const tps = isFinite(rawTps) && rawTps >= 0 ? rawTps : 0
            return date + " - " + hms + " - tok " + tokens + " - " + formatDuration(elapsedMs) + " - " + tps.toFixed(2) + " tps"
        } catch (e) {
            return ""
        }
    }

    Component.onCompleted: {
        syncAgentMessageModel()
        tryRestoreSessionsPanel()
        // Restaurar la última selección explícita de Agente. El launch activo
        // sólo es fallback para instalaciones que todavía no guardaron esa clave.
        let target = App.preferredAgentLaunchId()
        if (!target || target.length === 0)
            target = App.activeLaunchId
        if (!target || target.length === 0)
            target = App.readSetting("lastLaunchId", "")
        if (!target || target.length === 0) {
            const menu = App.profileManager.launchProfilesForMenu()
            if (menu.length > 0) target = menu[0].id ?? ""
        }
        if (target && target.length > 0) {
            selectedLaunchId = target
            profileCombo.currentIndex = profileCombo.indexOfValue(target)
            resolveHarness(target)
        }
        restoreCurrentSessionModes()
    }

    function markActivity() {
        lastAgentActivityMs = Date.now()
        idleSeconds = 0
    }

    Connections {
        target: App
        function syncLocalLaunchSelection() {
            const target = App.preferredAgentLaunchId()
            if (!target || target.length === 0 || target === root.selectedLaunchId)
                return
            root.selectedLaunchId = target
            profileCombo.currentIndex = profileCombo.indexOfValue(target)
            root.resolveHarness(target)
        }
        function onActiveLaunchIdChanged() { syncLocalLaunchSelection() }
        function onServerRunningChanged() { syncLocalLaunchSelection() }
        function onAgentMessagesChanged() {
            root.syncAgentMessageModel()
            root.markActivity()
        }
        function onAgentSessionsChanged() { root.restoreCurrentSessionModes() }
        function onAgentApprovalModeChanged() { root.persistCurrentSessionModes() }
        function onActiveAgentProfileChanged() { root.persistCurrentSessionModes() }
        function onAgentPendingToolChanged() { root.markActivity() }
        function onAgentLogChanged() { root.markActivity() }
        function onAgentRunningChanged() { root.markActivity() }
        // El streaming (texto Y args de tool) emite agentStreamingChanged en cada
        // delta pero NO messagesChanged: sin esto el badge marcaba "Sin actividad"
        // mientras el modelo generaba una tool grande (parecía colgado).
        function onAgentStreamingChanged() { root.markActivity() }
    }

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: {
            if (!App.agentRunning) {
                idleSeconds = 0
                return
            }
            idleSeconds = Math.floor((Date.now() - lastAgentActivityMs) / 1000)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Header ──────────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            height: 48
            color: Theme.baseBg

            RowLayout {
                anchors { fill: parent; leftMargin: 16; rightMargin: 12 }
                spacing: 10

                Text {
                    text: (App.langV, App.l("agent.title"))
                    color: Theme.textPrimary
                    font { pixelSize: 15; bold: true }
                }

                LcComboBox {
                    id: profileCombo
                    Layout.preferredWidth: 200
                    // Menú ordenado: favoritos (★) arriba; displayName = alias - name.
                    property var launchMenu: App.profileManager.launchProfilesForMenu()
                    Connections {
                        target: App.profileManager
                        function onLaunchesChanged() {
                            const sel = profileCombo.currentValue
                            profileCombo.launchMenu = App.profileManager.launchProfilesForMenu()
                            const i = profileCombo.indexOfValue(sel)
                            if (i >= 0) profileCombo.currentIndex = i
                        }
                    }
                    model: launchMenu
                    textRole: "displayName"
                    valueRole: "id"
                    enabled: !App.agentRunning
                    background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                    contentItem: Text {
                        text: profileCombo.displayText.length > 0 ? profileCombo.displayText : "—"
                        color: Theme.textPrimary; font.pixelSize: 12; leftPadding: 8
                        verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                    }
                    onCurrentValueChanged: {
                        selectedLaunchId = currentValue ?? ""
                        resolveHarness(selectedLaunchId)
                    }
                    onActivated: {
                        if (currentValue) {
                            App.writeSetting("lastAgentLaunchId", currentValue)
                            App.writeSetting("lastLaunchId", currentValue)
                        }
                    }
                }

                Rectangle {
                    visible: resolvedAdapter !== "none" && resolvedAdapterLabel.length > 0
                    height: 22; radius: 4; color: Theme.highlight
                    implicitWidth: adapterLabel.implicitWidth + 16
                    Text {
                        id: adapterLabel
                        anchors.centerIn: parent
                        text: resolvedAdapterLabel
                        color: Theme.accent
                        font { pixelSize: 11; bold: true }
                    }
                }

                Rectangle {
                    width: 8; height: 8; radius: 4
                    color: App.agentRunning ? Theme.successText
                         : App.agentStarting ? Theme.warnText : Theme.errorText
                }
                Text {
                    text: {
                        const _lang = App.langV
                        if (App.agentStarting) {
                            return resolvedAdapter !== "none" && resolvedAdapterLabel.length > 0
                                ? resolvedAdapterLabel + " · Iniciando agente..."
                                : "Iniciando agente..."
                        }
                        if (!App.agentRunning) return App.l("agent.stopped")
                        const title = App.opencodeSessionTitle ?? ""
                        const base  = App.activeAgentAdapter + " · " + App.l("agent.running")
                        return title.length > 0 ? (base + "  —  " + title) : base
                    }
                    color: App.agentRunning ? Theme.successText
                         : App.agentStarting ? Theme.warnText : Theme.textMuted
                    font.pixelSize: 12
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
                Rectangle {
                    visible: App.agentRunning || App.agentStarting
                    implicitWidth: activityText.implicitWidth + 16
                    height: 24
                    radius: 6
                    color: Theme.inputBg
                    border.color: root.activityColor
                    border.width: 1
                    Text {
                        id: activityText
                        anchors.centerIn: parent
                        text: root.activityLabel
                        color: root.activityColor
                        font.pixelSize: 11
                    }
                }

                // View toggle tabs
                Row {
                    spacing: 0
                    Repeater {
                        model: ["Vista Agente", "Vista terminal"]
                        Rectangle {
                            width: tabLabel.implicitWidth + 20
                            height: 28
                            radius: 4
                            color: root.currentView === index ? Theme.highlight : "transparent"
                            border.color: root.currentView === index ? Theme.accent : Theme.borderColor
                            border.width: 1
                            Text {
                                id: tabLabel
                                anchors.centerIn: parent
                                text: modelData
                                color: root.currentView === index ? Theme.accent : Theme.textMuted
                                font { pixelSize: 11; bold: root.currentView === index }
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.currentView = index
                            }
                        }
                    }
                }

                // Indicador de contexto (tokens usados / n_ctx).
                Rectangle {
                    visible: App.agentContextLimit > 0 && App.agentRunning
                    implicitWidth: App.agentContextPruneEvents > 0 ? 174 : 130
                    implicitHeight: 26
                    radius: 6
                    color: Theme.inputBg
                    border.color: Theme.borderColor
                    readonly property real frac: App.agentContextLimit > 0
                        ? Math.min(1, App.agentContextUsed / App.agentContextLimit) : 0
                    Rectangle {
                        anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 1 }
                        width: (parent.width - 2) * parent.frac
                        radius: 6
                        color: parent.frac > 0.9 ? Theme.errorText
                             : parent.frac > 0.7 ? Theme.warnText : Theme.accent
                        opacity: 0.35
                    }
                    Text {
                        anchors.centerIn: parent
                        text: "ctx " + App.agentContextUsed + "/" + App.agentContextLimit
                              + (App.agentContextPruneEvents > 0
                                 ? " · −" + App.agentContextPruned : "")
                        color: Theme.textSecondary; font { pixelSize: 11; family: Theme.codeFont }
                    }
                    MouseArea {
                        id: contextHover
                        anchors.fill: parent
                        hoverEnabled: true
                    }
                    ToolTip.visible: contextHover.containsMouse
                    ToolTip.text: "Memoria de trabajo: " + App.agentContextUsed + " tokens"
                                  + "\nTranscript completo: " + App.agentContextTranscript
                                  + "\nAhorro acumulado: " + App.agentContextPruned
                                  + " tokens en " + App.agentContextPruneEvents + " eventos"
                }

                // Política de aprobación de herramientas.
                LcComboBox {
                    id: approvalModeCombo
                    visible: resolvedAdapter === "opencode" || resolvedAdapter === "llamaagent"
                    implicitWidth: 150
                    model: [
                        { key: "auto",   label: "Aprobar todo" },
                        { key: "ask",    label: "Pedir escritura" },
                        { key: "manual", label: "Pedir todo" },
                        { key: "super",  label: "Super Agente ⚠" },
                        { key: "plan",   label: "Plan (solo lectura)" }
                    ]
                    textRole: "label"
                    valueRole: "key"
                    // indexOfValue() no es fiable con model de objetos JS (devuelve -1
                    // y el combo cae a índice 0 mostrando "Aprobar todo" aunque el modo
                    // real sea "ask"). Mapeo explícito para que UI y backend coincidan.
                    currentIndex: App.agentApprovalMode === "manual" ? 2
                                  : App.agentApprovalMode === "auto" ? 0
                                  : App.agentApprovalMode === "super" ? 3
                                  : App.agentApprovalMode === "plan" ? 4 : 1
                    onActivated: {
                        if (currentValue === "super" && App.agentApprovalMode !== "super") {
                            superAgentDialog.open()   // confirmar antes de aplicar
                        } else {
                            App.agentApprovalMode = currentValue
                        }
                    }
                    background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                    contentItem: Text {
                        text: approvalModeCombo.displayText
                        color: Theme.textPrimary; font.pixelSize: 12
                        leftPadding: 10
                        rightPadding: approvalModeCombo.indicator ? approvalModeCombo.indicator.width + 4 : 10
                        verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                    }
                }
                Text {
                    visible: resolvedAdapter === "llamaagent"
                    readonly property var sandbox: {
                        const modeDependency = App.agentApprovalMode
                        return App.agentSandboxStatus()
                    }
                    text: App.agentApprovalMode === "super" ? "⚠ Sin sandbox" : "🛡 Workspace"
                    color: App.agentApprovalMode === "super" ? Theme.warnText : Theme.textMuted
                    font.pixelSize: 10
                    ToolTip.visible: sandboxMA.containsMouse
                    ToolTip.text: (sandbox.detail ?? "") + "\nSO enforced: "
                                  + ((sandbox.osEnforced ?? false) ? "sí" : "no")
                    MouseArea { id: sandboxMA; anchors.fill: parent; hoverEnabled: true }
                }
                // Perfil de agente activo (capacidades + directivas). Override vivo
                // de la sesión; no pisa el del perfil de lanzamiento.
                LcComboBox {
                    id: agentProfileCombo
                    visible: resolvedAdapter === "llamaagent"
                    implicitWidth: 150
                    model: App.profileManager.agentProfiles
                    textRole: "name"; valueRole: "profileId"
                    currentIndex: Math.max(0, indexOfValue(App.activeAgentProfileId))
                    onActivated: App.activeAgentProfileId = currentValue
                    background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                    contentItem: Text {
                        text: "🤖 " + agentProfileCombo.displayText
                        color: Theme.textPrimary; font.pixelSize: 12
                        leftPadding: 10
                        rightPadding: agentProfileCombo.indicator ? agentProfileCombo.indicator.width + 4 : 10
                        verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                    }
                }
                CheckBox {
                    id: agentThinkingCheck
                    visible: resolvedAdapter === "llamaagent"
                    text: "Pensar"
                    checkable: false
                    checked: App.thinkingEnabled
                    onClicked: {
                        thinkingRestartDialog.targetEnabled = !App.thinkingEnabled
                        thinkingRestartDialog.open()
                    }
                    contentItem: Text {
                        text: agentThinkingCheck.text
                        color: Theme.textPrimary; font.pixelSize: 12
                        leftPadding: agentThinkingCheck.indicator.width + 6
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                LcButton {
                    text: "🧩 Skills"
                    visible: resolvedAdapter === "llamaagent"
                    onClicked: {
                        root.portableSkillRows = App.portableSkills()
                        root.selectedPortableSkill = ({})
                        skillsDialog.open()
                    }
                }
                LcButton {
                    text: "👥 Sala"
                    secondary: true
                    visible: resolvedAdapter === "llamaagent"
                    onClicked: {
                        if ((App.agentRoomStore.currentRoomId ?? "").length === 0)
                            App.createAgentRoom("Sala de " + (App.opencodeSessionTitle || "trabajo"), "")
                        roomDialog.open()
                    }
                }
                LcButton {
                    text: "🧠 Memoria"
                    secondary: true
                    visible: (resolvedAdapter === "llamaagent" || resolvedAdapter === "opencode")
                    onClicked: {
                        memoryDialog.text = App.readAgentMemory("")
                        memoryDialog.open()
                    }
                }
                LcButton {
                    text: "⚙ Agente"
                    secondary: true
                    visible: resolvedAdapter === "llamaagent"
                    onClicked: {
                        agentTuningSystem.text = App.agentSystemPrompt
                        agentTuningPerm.text = App.agentPermRules
                        agentTuningTemp.text = App.agentTemperature >= 0 ? String(App.agentTemperature) : ""
                        agentTuningDialog.open()
                    }
                }
                LcButton {
                    text: "⚙ Config opencode"
                    secondary: true
                    visible: resolvedAdapter === "opencode"
                    onClicked: {
                        opencodeConfigDialog.projectDir = App.currentAgentProjectDir()
                        opencodeConfigDialog.open()
                    }
                }
                LcButton {
                    text: "Ver log nativo"
                    secondary: true
                    visible: resolvedAdapter.length > 0 && resolvedAdapter !== "none"
                    onClicked: App.openAgentLogDir(resolvedAdapter)
                }
                LcButton {
                    text: "Abrir logs runtime"
                    secondary: true
                    onClicked: App.openRuntimeLogDir()
                }
                LcButton {
                    text: "🚀 Corridas"
                    secondary: true
                    onClicked: managedRunsDialog.open()
                }
                LcButton {
                    text: "📦 Inbox"
                    secondary: true
                    onClicked: nativeRunsDialog.open()
                }
                LcButton {
                    text: {
                        const _lang = App.langV
                        if (App.agentStarting) return "Iniciando agente..."
                        return App.agentRunning ? App.l("agent.stop") : App.l("agent.start")
                    }
                    danger: App.agentRunning || App.agentStarting
                    enabled: selectedLaunchId.length > 0
                    onClicked: {
                        if (App.agentRunning || App.agentStarting) {
                            App.stopAgent()
                        } else {
                            App.writeSetting("lastAgentLaunchId", selectedLaunchId)
                            App.writeSetting("lastLaunchId", selectedLaunchId)
                            App.startAgent(selectedLaunchId)
                        }
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

        Rectangle {
            Layout.fillWidth: true; height: 32
            visible: selectedLaunchId.length > 0
                     && (resolvedAdapter === "none" || resolvedAdapterLabel.length === 0)
                     && !App.agentRunning && !App.agentStarting
            color: Theme.surfaceBg
            Text {
                anchors { verticalCenter: parent.verticalCenter; left: parent.left; leftMargin: 16 }
                text: (App.langV, App.l("agent.noHarness"))
                color: Theme.textMuted; font.pixelSize: 12
            }
        }

        Rectangle {
            Layout.fillWidth: true; height: 32
            visible: resolvedAdapter !== "none" && resolvedAdapterLabel.length > 0
                     && !(App.harnessCheckV, App.isHarnessInstalled(resolvedAdapter))
                     && !App.agentRunning && !App.agentStarting
            color: Theme.errorBg
            Text {
                anchors { verticalCenter: parent.verticalCenter; left: parent.left; leftMargin: 16 }
                text: (App.langV, App.l("agent.notInstalled"))
                color: Theme.errorText; font.pixelSize: 12
            }
        }

        // Aviso de tool-calling del perfil activo (cookbook + chat-template).
        ToolSupportBanner {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 8
            visible: App.agentRunning && App.activeProfileToolSupport !== "supported"
            support: App.activeProfileToolSupport
        }

        // ── Body ─────────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ── Sessions panel ───────────────────────────────────────────────
            Rectangle {
                Layout.preferredWidth: root.sessionsPanelWidth
                Layout.fillHeight: true
                visible: App.agentRunning && root.currentView === 0
                color: Theme.surfaceBg
                border.color: Theme.divider
                border.width: 0

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        height: 40
                        color: Theme.baseBg

                        RowLayout {
                            anchors { fill: parent; leftMargin: 10; rightMargin: 8 }
                            spacing: 6
                            Text {
                                text: "Sesiones"
                                color: Theme.textPrimary
                                font { pixelSize: 12; bold: true }
                                Layout.fillWidth: true
                            }
                            LcButton {
                                text: "↻"
                                secondary: true
                                implicitWidth: 28
                                implicitHeight: 24
                                onClicked: App.refreshOpencodeSessionList()
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

                    // ── Nueva sesión ──────────────────────────────────────────
                    Rectangle {
                        id: newSessionBtn
                        Layout.fillWidth: true
                        height: 40
                        color: "transparent"
                        RowLayout {
                            anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                            spacing: 6
                            Text { text: "💬"; font.pixelSize: 11 }
                            Text {
                                text: "Nueva sesión"
                                color: Theme.textMuted
                                font.pixelSize: 12
                                Layout.fillWidth: true
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            hoverEnabled: true
                            onEntered: newSessionBtn.color = Theme.highlight
                            onExited:  newSessionBtn.color = "transparent"
                            onClicked: App.newOpencodeSession()
                        }
                    }

                    // ── Nuevo proyecto ────────────────────────────────────────
                    Rectangle {
                        id: newProjectBtnTop
                        Layout.fillWidth: true
                        height: 40
                        color: "transparent"
                        RowLayout {
                            anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                            spacing: 6
                            Text { text: "📁"; font.pixelSize: 11 }
                            Text {
                                text: "Nuevo proyecto"
                                color: Theme.textMuted
                                font.pixelSize: 12
                                Layout.fillWidth: true
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            hoverEnabled: true
                            onEntered: newProjectBtnTop.color = Theme.highlight
                            onExited:  newProjectBtnTop.color = "transparent"
                            onClicked: {
                                const dir = App.pickDirectory("Elegí la carpeta del proyecto")
                                if (dir.length === 0) return
                                App.changeAgentProject(dir)
                                // Si el agente no está corriendo, arrancarlo en esa carpeta.
                                if (!App.agentRunning && selectedLaunchId.length > 0)
                                    App.startAgent(selectedLaunchId)
                            }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

                    ListView {
                        id: sessionsList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: App.agentSessions
                        ScrollBar.vertical: LcScrollBar { id: sessionsScrollBar; policy: ScrollBar.AsNeeded }

                        section.property: "projectName"
                        section.criteria: ViewSection.FullString

                        Text {
                            anchors.centerIn: parent
                            width: parent.width - 32
                            visible: sessionsList.count === 0
                            text: "Sin sesiones todavía.\nUsá + o ↻ para empezar."
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            color: Theme.textMuted
                            font.pixelSize: 11
                        }

                        section.delegate: Rectangle {
                            width: sessionsList.width
                            height: 28
                            color: Theme.baseBg

                            // Click derecho → menú de proyecto (borrar proyecto + chats).
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.RightButton
                                onClicked: {
                                    agentProjectCtxMenu.projectName = section
                                    agentProjectCtxMenu.projectDir = root.projectDirForSection(section)
                                    agentProjectCtxMenu.popup()
                                }
                            }

                            RowLayout {
                                // Reservar ancho del scrollbar cuando está visible para que
                                // no se superponga con el botón "+".
                                anchors { fill: parent; leftMargin: 8
                                          rightMargin: 6 + (sessionsScrollBar.visible ? sessionsScrollBar.width : 0) }
                                spacing: 4
                                Text { text: "📁"; font.pixelSize: 10 }
                                Text {
                                    Layout.fillWidth: true
                                    text: section
                                    color: Theme.textMuted
                                    font { pixelSize: 10; bold: true }
                                    elide: Text.ElideLeft
                                }
                                // "+" per project → new session in this project
                                Rectangle {
                                    width: 20; height: 20; radius: 4
                                    color: addSessionHover.containsMouse ? Theme.highlight : "transparent"
                                    Text {
                                        anchors.centerIn: parent
                                        text: "+"
                                        color: Theme.textMuted
                                        font { pixelSize: 13; bold: true }
                                    }
                                    MouseArea {
                                        id: addSessionHover
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: App.newOpencodeSessionInProject(root.projectDirForSection(section))
                                    }
                                }
                            }
                        }

                        delegate: Rectangle {
                            width: sessionsList.width
                            height: 52
                            color: modelData.id === (App.opencodeSessionId ?? "")
                                   ? Theme.highlight : "transparent"

                            Rectangle {
                                width: 3; height: parent.height
                                color: modelData.id === (App.opencodeSessionId ?? "")
                                       ? Theme.accent : "transparent"
                            }

                            ColumnLayout {
                                anchors {
                                    verticalCenter: parent.verticalCenter
                                    left: parent.left; leftMargin: 14
                                    right: parent.right; rightMargin: 8
                                }
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    text: {
                                        const t = modelData.title ?? ""
                                        const branch = (modelData.depth ?? 0) > 0 ? "↳ " : ""
                                        return branch + (t.length > 0 ? t : "Nueva sesión")
                                    }
                                    color: modelData.id === (App.opencodeSessionId ?? "")
                                           ? Theme.accent : Theme.textPrimary
                                    font { pixelSize: 12; bold: modelData.id === (App.opencodeSessionId ?? "") }
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: {
                                        const ms = modelData.created ?? 0
                                        const state = modelData.runtimeState ?? "idle"
                                        if (state === "running") return "● Trabajando"
                                        if (state === "queued") return "○ En cola (" + (modelData.queuedCount ?? 0) + ")"
                                        return ms > 0 ? new Date(ms).toLocaleDateString(Qt.locale(), "d MMM yyyy") : ""
                                    }
                                    color: (modelData.runtimeState ?? "idle") === "running"
                                           ? Theme.successText : Theme.textMuted
                                    font.pixelSize: 10
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onClicked: (mouse) => {
                                    if (mouse.button === Qt.RightButton) {
                                        sessionCtxMenu.sessionId = modelData.id
                                        sessionCtxMenu.sessionTitle = (modelData.title ?? "")
                                        sessionCtxMenu.popup()
                                    } else {
                                        App.switchOpencodeSession(modelData.id)
                                    }
                                }
                            }
                        }

                        Menu {
                            id: sessionCtxMenu
                            property string sessionId: ""
                            property string sessionTitle: ""
                            MenuItem {
                                text: "Renombrar sesión"
                                onTriggered: if (sessionCtxMenu.sessionId.length > 0) {
                                    agentRenameDialog.targetId = sessionCtxMenu.sessionId
                                    agentRenameField.text = sessionCtxMenu.sessionTitle
                                    agentRenameDialog.open()
                                }
                            }
                            MenuItem {
                                text: "Bifurcar (fork)"
                                onTriggered: if (sessionCtxMenu.sessionId.length > 0)
                                    App.forkOpencodeSession(sessionCtxMenu.sessionId)
                            }
                            MenuSeparator {}
                            MenuItem {
                                text: "Borrar sesión"
                                onTriggered: if (sessionCtxMenu.sessionId.length > 0) {
                                    agentDeleteDialog.targetId = sessionCtxMenu.sessionId
                                    agentDeleteDialog.sessionTitle = sessionCtxMenu.sessionTitle
                                    agentDeleteDialog.open()
                                }
                            }
                        }
                    }

                }
            }

            // ── Handle de redimensión del panel de sesiones ──────────────────
            Item {
                id: sessionsResizeHandle
                Layout.preferredWidth: 10
                Layout.fillHeight: true
                visible: App.agentRunning && root.currentView === 0

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: sessionsResizeMouse.pressed || sessionsResizeHover.hovered ? 3 : 1
                    height: parent.height
                    color: sessionsResizeMouse.pressed || sessionsResizeHover.hovered
                           ? Theme.accent : Theme.divider
                }

                HoverHandler {
                    id: sessionsResizeHover
                    cursorShape: Qt.SplitHCursor
                }

                MouseArea {
                    id: sessionsResizeMouse
                    anchors.fill: parent
                    cursorShape: Qt.SplitHCursor
                    property real pressRootX: 0
                    property int pressWidth: root.sessionsPanelWidth
                    onPressed: function(mouse) {
                        pressRootX = mapToItem(root, mouse.x, mouse.y).x
                        pressWidth = root.sessionsPanelWidth
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed) return
                        const currentRootX = mapToItem(root, mouse.x, mouse.y).x
                        root.sessionsPanelWidth = root.clampSessionsWidth(pressWidth + currentRootX - pressRootX)
                    }
                    onReleased: {
                        root._spRestored = true
                        root._savedSessionsWidth = root.sessionsPanelWidth
                        App.writeSetting("agentSessionsPanelWidth", root.sessionsPanelWidth)
                    }
                }
            }

            // ── Main content area ────────────────────────────────────────────
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

            // ── Vista Agente (chat) ───────────────────────────────────────────
            Item {
                anchors.fill: parent
                visible: root.currentView === 0

                // Estado bloqueado: servidor caído o modelo aún cargando.
                Rectangle {
                    anchors.centerIn: parent
                    z: 5
                    visible: App.thinkingRestarting || App.agentStarting || (App.agentRunning && (!App.backendAvailable || (App.serverRunning && !App.serverReady)))
                    radius: 10
                    color: Theme.surfaceBg
                    border.color: Theme.borderColor
                    implicitWidth: agentLoadingRow.implicitWidth + 32
                    implicitHeight: agentLoadingRow.implicitHeight + 24
                    Row {
                        id: agentLoadingRow
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
                            text: App.thinkingRestarting
                                ? "Recargando modelo para aplicar el nivel de pensamiento..."
                                : App.agentStarting
                                ? (App.hybridStatus.length > 0 ? App.hybridStatus : "Iniciando agente...")
                                : !App.backendAvailable
                                ? "Servidor no disponible. Iniciá el modelo en Lanzar."
                                : "Cargando modelo..."
                            color: Theme.textSecondary; font.pixelSize: 14
                        }
                    }
                }

                // Idle / not running
                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 16
                    visible: !App.agentRunning && !App.agentStarting && App.agentMessages.length === 0
                    Text { Layout.alignment: Qt.AlignHCenter; text: "🤖"; font.pixelSize: 48 }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: resolvedAdapter !== "none" && resolvedAdapterLabel.length > 0
                            ? resolvedAdapterLabel + " · " + (App.langV, App.l("agent.stopped"))
                            : (App.langV, App.l("agent.stopped"))
                        color: Theme.textMuted; font.pixelSize: 14
                    }
                }

                // Message list
                ListView {
                    id: msgList
                    anchors.fill: parent
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    flickDeceleration: 3000
                    maximumFlickVelocity: 6000
                    spacing: 4
                    topMargin: 12
                    bottomMargin: 12
                    // Mantener delegados de arriba medidos: evita que contentHeight
                    // se re-estime al subir (causa del salto/traba hacia arriba).
                    cacheBuffer: 4000
                    visible: agentMessageUiModel.count > 0
                    model: agentMessageUiModel
                    ScrollBar.vertical: LcScrollBar { policy: ScrollBar.AsNeeded }

                    delegate: Item {
                        id: delegateRoot
                        width: msgList.width
                        height: (isDiff ? diffCard.height : (isTool ? toolCard.height : bubbleRect.height)) + 8

                        readonly property bool isUser: entry.role === "user"
                        readonly property bool isDiff: entry.role === "diff"
                        readonly property bool isTool: entry.role === "toolcall"
                        // Durante streaming, esta burbuja usa el texto en vivo
                        // (App.agentStreamingText) en vez de entry.content, así
                        // sólo este delegate se refresca por token (sin reset de lista).
                        readonly property bool isStreaming: index === App.agentStreamingIndex
                        readonly property string content: isStreaming
                            ? App.agentStreamingText
                            : (entry.content ?? "")
                        readonly property int maxInlineChars: 60000
                        readonly property bool isLongContent: content.length > maxInlineChars
                        property bool expandedLongContent: false
                        readonly property string visibleContent: {
                            if (!isLongContent || expandedLongContent)
                                return content
                            if (isStreaming || isTyping)
                                return content.slice(Math.max(0, content.length - maxInlineChars))
                            return content.slice(0, maxInlineChars)
                        }
                        readonly property bool isTyping: entry.typing ?? false
                        readonly property string metaLine: root.formatMeta(entry)
                        property bool editing: false

                        Rectangle {
                            id: bubbleRect
                            visible: !delegateRoot.isDiff && !delegateRoot.isTool
                            anchors {
                                top: parent.top
                                right: delegateRoot.isUser ? parent.right : undefined
                                rightMargin: delegateRoot.isUser ? 16 : undefined
                                left: delegateRoot.isUser ? undefined : parent.left
                                leftMargin: delegateRoot.isUser ? undefined : 16
                            }
                            width: Math.min(delegateRoot.width - 80, delegateRoot.width * 0.78)
                            height: Math.max(msgCol.implicitHeight + 22, 44)
                            radius: 10
                            color: delegateRoot.isUser ? Theme.chatUserBubble : Theme.chatAsstBubble
                            border.width: delegateRoot.isUser ? 0 : 1
                            border.color: Theme.borderColor

                            Column {
                                id: msgCol
                                anchors { top: parent.top; left: parent.left; right: parent.right; margins: 11 }
                                spacing: 6

                                TextEdit {
                                    id: msgText
                                    visible: !delegateRoot.editing
                                    width: parent.width
                                    text: {
                                        if (delegateRoot.isTyping && delegateRoot.visibleContent.length === 0)
                                            return (entry.status ?? "Pensando...") + "  "
                                        if (delegateRoot.isTyping)
                                            return delegateRoot.visibleContent + "▌"
                                        return delegateRoot.visibleContent
                                    }
                                    color: {
                                        if (delegateRoot.isTyping && delegateRoot.content.length === 0)
                                            return Theme.textMuted
                                        return delegateRoot.isUser ? Theme.chatUserText : Theme.chatAsstText
                                    }
                                    font.family: "Segoe UI"
                                    font.pixelSize: 13
                                    font.italic: delegateRoot.isTyping && delegateRoot.content.length === 0
                                    wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                                    readOnly: true
                                    selectByMouse: true
                                }

                                // Edición inline del mensaje (rebobina + reescribe).
                                Column {
                                    visible: delegateRoot.editing
                                    width: parent.width
                                    spacing: 6
                                    TextArea {
                                        id: agentEditArea
                                        width: parent.width
                                        wrapMode: TextArea.WrapAtWordBoundaryOrAnywhere
                                        color: Theme.chatAsstText
                                        font.family: "Segoe UI"
                                        font.pixelSize: 13
                                        background: Rectangle {
                                            color: Theme.surfaceBg; radius: 6
                                            border.color: Theme.borderColor; border.width: 1
                                        }
                                    }
                                    Row {
                                        layoutDirection: Qt.RightToLeft
                                        width: parent.width
                                        spacing: 12
                                        Text {
                                            text: "Guardar"
                                            color: agentSaveMA.containsMouse ? Theme.textPrimary : Theme.accent
                                            font.pixelSize: 11; font.bold: true
                                            MouseArea {
                                                id: agentSaveMA
                                                anchors.fill: parent; hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: {
                                                    App.editAgentMessage(index, agentEditArea.text)
                                                    delegateRoot.editing = false
                                                }
                                            }
                                        }
                                        Text {
                                            text: "Cancelar"
                                            color: agentCancelMA.containsMouse ? Theme.textPrimary : Theme.textMuted
                                            font.pixelSize: 11
                                            MouseArea {
                                                id: agentCancelMA
                                                anchors.fill: parent; hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: delegateRoot.editing = false
                                            }
                                        }
                                    }
                                }
                                Text {
                                    visible: (modelData.parentSessionId ?? "").length > 0
                                    text: "rama · mensaje " + ((modelData.forkMessageIndex ?? -1) >= 0
                                                              ? modelData.forkMessageIndex : "final")
                                    color: Theme.accent; font.pixelSize: 9
                                }
                                Text {
                                    visible: delegateRoot.metaLine.length > 0
                                    width: parent.width
                                    text: delegateRoot.metaLine
                                    color: Theme.textMuted
                                    font.pixelSize: 10
                                    horizontalAlignment: Text.AlignRight
                                    elide: Text.ElideRight
                                }
                                // Copiar mensaje al portapapeles.
                                Row {
                                    width: parent.width
                                    layoutDirection: Qt.RightToLeft
                                    spacing: 12
                                    visible: delegateRoot.content.length > 0
                                    Text {
                                        visible: delegateRoot.isLongContent
                                        text: delegateRoot.expandedLongContent ? "Contraer" : "Ver completo"
                                        color: longMA.containsMouse ? Theme.textPrimary : Theme.textMuted
                                        font.pixelSize: 10
                                        MouseArea {
                                            id: longMA
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: delegateRoot.expandedLongContent = !delegateRoot.expandedLongContent
                                        }
                                    }
                                    Text {
                                        visible: !delegateRoot.isTyping
                                        text: bubbleRect.justCopied ? "✓ Copiado" : "⧉ Copiar"
                                        color: copyMA.containsMouse || bubbleRect.justCopied
                                               ? Theme.textPrimary : Theme.textMuted
                                        font.pixelSize: 10
                                        MouseArea {
                                            id: copyMA
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                App.copyToClipboard(delegateRoot.content)
                                                bubbleRect.justCopied = true
                                                copyResetTimer.restart()
                                            }
                                        }
                                    }
                                    // Rebobinar: descarta este turno y los siguientes
                                    // (revierte edits posteriores). Solo en mensajes del usuario.
                                    Text {
                                        visible: delegateRoot.isUser && !root.hasTypingMessage && !root.waitingApproval && !delegateRoot.editing
                                        text: "↩ Rebobinar"
                                        color: rewindMA.containsMouse ? Theme.textPrimary : Theme.textMuted
                                        font.pixelSize: 10
                                        MouseArea {
                                            id: rewindMA
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: App.rollbackAgentToMessage(index)
                                        }
                                    }
                                    Text {
                                        visible: delegateRoot.isUser && resolvedAdapter === "llamaagent"
                                                 && !root.hasTypingMessage && !root.waitingApproval
                                                 && !delegateRoot.editing
                                        text: "⑂ Bifurcar"
                                        color: forkMessageMA.containsMouse ? Theme.textPrimary : Theme.textMuted
                                        font.pixelSize: 10
                                        MouseArea {
                                            id: forkMessageMA
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: App.forkAgentAtMessage(index)
                                        }
                                    }
                                    // Editar: reescribe el texto (de IA o usuario) y descarta lo posterior.
                                    Text {
                                        visible: !root.hasTypingMessage && !root.waitingApproval && !delegateRoot.editing
                                        text: "✎ Editar"
                                        color: editMA.containsMouse ? Theme.textPrimary : Theme.textMuted
                                        font.pixelSize: 10
                                        MouseArea {
                                            id: editMA
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                agentEditArea.text = delegateRoot.content
                                                delegateRoot.editing = true
                                            }
                                        }
                                    }
                                }
                            }

                            property bool justCopied: false
                            Timer { id: copyResetTimer; interval: 1500; onTriggered: bubbleRect.justCopied = false }
                        }

                        // ── Entrada de diff (edición de archivo) ──────────────
                        Rectangle {
                            id: diffCard
                            visible: delegateRoot.isDiff
                            anchors { top: parent.top; left: parent.left; right: parent.right
                                      leftMargin: 16; rightMargin: 16 }
                            height: visible ? diffCol.implicitHeight + 20 : 0
                            radius: 8
                            color: Theme.inputBg
                            border.color: Theme.borderColor
                            readonly property bool reverted: entry.reverted ?? false
                            // Diff colapsado por defecto: solo nombre + toggle.
                            property bool expanded: false
                            readonly property int diffLines: {
                                const d = String(entry.diff ?? "")
                                if (d.length === 0) return 0
                                return d.split("\n").length
                            }
                            readonly property string filePath: entry.absPath ?? entry.path ?? ""

                            // Click derecho → menú: abrir carpeta contenedora.
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.RightButton
                                onClicked: {
                                    fileCtxMenu.filePath = diffCard.filePath
                                    fileCtxMenu.popup()
                                }
                            }

                            ColumnLayout {
                                id: diffCol
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                                spacing: 6

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    // Toggle expandir/colapsar (chevron). Clic en todo el header también abre.
                                    Text {
                                        text: diffCard.expanded ? "▾" : "▸"
                                        color: Theme.textMuted
                                        font.pixelSize: 12
                                        MouseArea {
                                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                            onClicked: diffCard.expanded = !diffCard.expanded
                                        }
                                    }
                                    Text { text: "📝"; font.pixelSize: 13 }
                                    Text {
                                        Layout.fillWidth: true
                                        text: (entry.path ?? "")
                                              + (diffCard.diffLines > 0 ? "  · " + diffCard.diffLines + " líneas" : "")
                                              + (diffCard.reverted ? "  · revertido" : "")
                                        color: diffCard.reverted ? Theme.textMuted : Theme.textPrimary
                                        font { family: Theme.codeFont; pixelSize: 12; bold: true }
                                        elide: Text.ElideMiddle
                                        MouseArea {
                                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                            onClicked: diffCard.expanded = !diffCard.expanded
                                        }
                                    }
                                    LcButton {
                                        text: diffCard.expanded ? "Ocultar" : "Ver"
                                        secondary: true
                                        implicitHeight: 24
                                        visible: diffCard.diffLines > 0
                                        onClicked: diffCard.expanded = !diffCard.expanded
                                    }
                                    LcButton {
                                        text: "Copiar"; secondary: true
                                        implicitHeight: 24
                                        visible: diffCard.diffLines > 0
                                        onClicked: App.copyToClipboard(entry.diff ?? "")
                                    }
                                    LcButton {
                                        text: "Revertir"; secondary: true
                                        visible: !diffCard.reverted
                                        implicitHeight: 24
                                        onClicked: App.revertAgentEdit(entry.absPath ?? entry.path ?? "")
                                    }
                                }

                                // Cuerpo del diff: oculto por defecto, scrollable al abrir.
                                ScrollView {
                                    Layout.fillWidth: true
                                    visible: diffCard.expanded
                                    clip: true
                                    Layout.preferredHeight: visible
                                        ? Math.min(diffText.implicitHeight + 8, 280)
                                        : 0
                                    ScrollBar.horizontal.policy: ScrollBar.AsNeeded
                                    ScrollBar.vertical.policy: ScrollBar.AsNeeded

                                    TextEdit {
                                        id: diffText
                                        // Lazy: solo materializar el texto (puede ser de miles de
                                        // líneas) cuando la tarjeta está expandida. Si no, vacío →
                                        // sin layout costoso en cada rebuild del ListView.
                                        text: diffCard.expanded ? (entry.diff ?? "") : ""
                                        color: Theme.textSecondary
                                        font { family: Theme.codeFont; pixelSize: 11 }
                                        wrapMode: TextEdit.NoWrap
                                        readOnly: true; selectByMouse: true
                                        opacity: diffCard.reverted ? 0.5 : 1.0
                                    }
                                }
                            }
                        }

                        // ── Tarjeta de ejecución de tool (run_shell, list_dir, …) ──
                        Rectangle {
                            id: toolCard
                            visible: delegateRoot.isTool
                            anchors { top: parent.top; left: parent.left; right: parent.right
                                      leftMargin: 16; rightMargin: 16 }
                            height: visible ? toolCol.implicitHeight + 20 : 0
                            radius: 8
                            color: Theme.inputBg
                            border.color: Theme.borderColor

                            // run_shell async en ejecución → expandido y "corriendo".
                            readonly property bool running: entry.typing ?? false
                            property bool userExpanded: false
                            readonly property bool expanded: running || userExpanded
                            readonly property bool ok: entry.ok ?? true
                            readonly property string toolName: entry.name ?? ""
                            readonly property string command: entry.command ?? ""
                            readonly property string output: entry.output ?? ""
                            readonly property bool hasBody: command.length > 0 || output.length > 0

                            ColumnLayout {
                                id: toolCol
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                                spacing: 6

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Text {
                                        text: toolCard.expanded ? "▾" : "▸"
                                        color: Theme.textMuted; font.pixelSize: 12
                                        MouseArea {
                                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                            onClicked: toolCard.userExpanded = !toolCard.expanded
                                        }
                                    }
                                    Text { text: "🔧"; font.pixelSize: 13 }
                                    Text {
                                        Layout.fillWidth: true
                                        text: toolCard.toolName
                                              + (toolCard.command.length > 0 ? "  " + toolCard.command : "")
                                        color: Theme.textPrimary
                                        font { family: Theme.codeFont; pixelSize: 12; bold: true }
                                        elide: Text.ElideRight
                                        MouseArea {
                                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                            onClicked: toolCard.userExpanded = !toolCard.expanded
                                        }
                                    }
                                    // Estado: corriendo / ok / error
                                    Rectangle {
                                        radius: 4
                                        color: toolCard.running ? "#5a4a1a"
                                               : (toolCard.ok ? Theme.highlight : "#7a2230")
                                        implicitHeight: 18; implicitWidth: statusTxt.implicitWidth + 12
                                        Text {
                                            id: statusTxt
                                            anchors.centerIn: parent
                                            text: toolCard.running ? "corriendo…"
                                                  : (toolCard.ok ? "ok" : "error")
                                            color: toolCard.running ? "#ffe9a8"
                                                   : (toolCard.ok ? Theme.textPrimary : "#ffd5d5")
                                            font.pixelSize: 10
                                        }
                                    }
                                    LcButton {
                                        text: "Copiar"; secondary: true
                                        implicitHeight: 24
                                        visible: toolCard.hasBody
                                        onClicked: App.copyToClipboard(
                                            (toolCard.command.length > 0 ? "$ " + toolCard.command + "\n\n" : "")
                                            + toolCard.output)
                                    }
                                }

                                // Cuerpo: comando + salida, oculto por defecto, scrollable.
                                ScrollView {
                                    Layout.fillWidth: true
                                    visible: toolCard.expanded
                                    clip: true
                                    Layout.preferredHeight: visible
                                        ? Math.min(toolBody.implicitHeight + 8, 260)
                                        : 0
                                    ScrollBar.horizontal.policy: ScrollBar.AsNeeded
                                    ScrollBar.vertical.policy: ScrollBar.AsNeeded

                                    TextEdit {
                                        id: toolBody
                                        // Lazy: materializar solo al expandir (la salida puede ser enorme).
                                        text: toolCard.expanded
                                              ? ((toolCard.command.length > 0 ? "$ " + toolCard.command + "\n\n" : "")
                                                 + toolCard.output)
                                              : ""
                                        color: Theme.textSecondary
                                        font { family: Theme.codeFont; pixelSize: 11 }
                                        wrapMode: TextEdit.NoWrap
                                        readOnly: true; selectByMouse: true
                                    }
                                }
                            }
                        }

                    }

                    // Auto-scroll "pegado al fondo": seguir solo si el usuario ya
                    // está abajo. Si scrollea hacia arriba, no lo arrastramos.
                    // Usamos contentY directo (no positionViewAtEnd) porque con
                    // delegates de altura variable que cambian por token, la
                    // estimación de positionViewAtEnd oscila (salta arriba/abajo).
                    property bool followBottom: true
                    // QVariantList se reemplaza completo al cambiar un mensaje. Qt
                    // vacía el ListView durante un frame y fuerza contentY al inicio.
                    // Recordar que estábamos abajo evita interpretar ese movimiento
                    // interno como una decisión del usuario.
                    property bool restoreBottomAfterModelReset: false
                    property string scrollMutationReason: ""
                    property real previousContentY: 0

                    function scrollState(extra) {
                        return JSON.stringify({
                            y: Number(contentY.toFixed(2)),
                            previousY: Number(previousContentY.toFixed(2)),
                            originY: Number(minContentY().toFixed(2)),
                            maxY: Number(maxContentY().toFixed(2)),
                            contentHeight: Number(contentHeight.toFixed(2)),
                            viewportHeight: Number(height.toFixed(2)),
                            count: count,
                            followBottom: followBottom,
                            moving: moving,
                            dragging: dragging,
                            typing: root.hasTypingMessage,
                            mutation: scrollMutationReason.length > 0
                                      ? scrollMutationReason : "qt/internal",
                            extra: extra ?? ""
                        })
                    }

                    function traceScroll(event, extra) {
                        App.logAgentUiScroll(event, scrollState(extra))
                    }

                    function setContentY(reason, value) {
                        if (!isFinite(value)) {
                            traceScroll("reject-nonfinite-target", reason + "=" + value)
                            return
                        }
                        if (Math.abs(contentY - value) < 0.01)
                            return
                        scrollMutationReason = reason
                        contentY = value
                        scrollMutationReason = ""
                    }

                    function minContentY() {
                        return isFinite(originY) ? originY : 0
                    }

                    function maxContentY() {
                        return minContentY() + Math.max(0, contentHeight - height)
                    }

                    function scrollToBottom() {
                        // Nunca mover el viewport hacia arriba desde el auto-follow.
                        // ListView puede informar transitoriamente un contentHeight
                        // demasiado chico mientras recicla/mide delegates altos; si
                        // copiamos ese maxY, un refresh normal del QVariantList puede
                        // mandar la conversación hasta el inicio.
                        var maxY = maxContentY()
                        if (contentY < maxY)
                            setContentY("scrollToBottom", maxY)
                    }

                    function normalizeViewport() {
                        var minY = minContentY()
                        var maxY = maxContentY()
                        if (!isFinite(contentY) || contentY < minY) {
                            setContentY("normalize:min", minY)
                            return
                        }
                        if (contentY > maxY)
                            setContentY("normalize:max", maxY)
                    }

                    // followBottom se actualiza en vivo con cualquier cambio de
                    // posición (flick nativo o rueda animada): si el usuario sube,
                    // dejamos de auto-bajar durante el streaming.
                    onContentYChanged: {
                        traceScroll("contentYChanged", "")
                        previousContentY = contentY
                        if (count === 0) {
                            if (followBottom)
                                restoreBottomAfterModelReset = true
                            return
                        }
                        if (restoreBottomAfterModelReset) {
                            followBottom = true
                            return
                        }
                        // Clamp duro cuando no hay streaming. agentRunning no sirve
                        // como condición: el agente queda activo mientras se restaura
                        // una sesión y Qt recalcula varias veces las alturas.
                        var minY = minContentY()
                        var maxY = maxContentY()
                        if (!root.hasTypingMessage
                                && (!isFinite(contentY) || contentY < minY || contentY > maxY)) {
                            normalizeViewport()
                            return
                        }
                        followBottom = (contentY >= maxY - 2)
                    }
                    onMovementStarted: traceScroll("movementStarted", "")
                    onMovementEnded: {
                        followBottom = atYEnd
                        traceScroll("movementEnded", "atYEnd=" + atYEnd)
                    }
                    // Throttle: durante streaming el contentHeight cambia por token.
                    // Un solo callLater coalescido evita reflows en cascada.
                    onContentHeightChanged: {
                        traceScroll("contentHeightChanged", "")
                        // Los delegates largos se miden en varias pasadas. Si una
                        // pasada reduce contentHeight, el contentY anterior puede
                        // quedar fuera del rango y Qt muestra un viewport vacío.
                        if (!root.hasTypingMessage)
                            normalizeViewport()
                        if (followBottom)
                            bottomTimer.restart()
                        if (restoreBottomAfterModelReset)
                            modelResetSettleTimer.restart()
                    }
                    // Sólo re-pegar al fondo si el usuario YA estaba abajo. Si subió
                    // a leer, un mensaje/token nuevo no lo arrastra de vuelta.
                    onCountChanged: {
                        traceScroll("countChanged", "")
                        // Si el modelo se vacía durante un cambio de backend/sesión,
                        // descartar el offset del historial anterior. De lo contrario
                        // un contentY grande puede quedar fuera del nuevo contenido y
                        // dejar el viewport completamente negro.
                        if (count === 0) {
                            restoreBottomAfterModelReset =
                                    restoreBottomAfterModelReset || followBottom
                            setContentY("countChanged:empty", minContentY())
                            followBottom = true
                            return
                        }
                        if (restoreBottomAfterModelReset)
                            followBottom = true
                        bottomTimer.restart()
                        viewportSettleTimer.restart()
                        modelResetSettleTimer.restart()
                    }
                    onModelChanged: {
                        traceScroll("modelChanged", "")
                        // agentMessages es un QVariantList: cada NOTIFY puede hacer
                        // que QML vea una nueva instancia de modelo aunque sólo haya
                        // cambiado un mensaje. No reiniciar contentY aquí; el vaciado
                        // real ya se maneja en onCountChanged.
                        followBottom = true
                        bottomTimer.restart()
                        viewportSettleTimer.restart()
                    }

                    Timer {
                        id: bottomTimer
                        // Debounce ~1 frame: durante streaming el contentHeight cambia
                        // muchas veces por segundo (uno o más por token). Con interval 0
                        // cada cambio dispara forceLayout+scroll y, con delegates de
                        // altura variable que se re-miden en pasadas, el fondo rebota
                        // (sube/baja). Coalescer la ráfaga en un solo reflow por frame
                        // elimina el temblor sin latencia perceptible.
                        interval: 32
                        onTriggered: {
                            if (!msgList.followBottom) return
                            msgList.traceScroll("bottomTimer:before", "")
                            msgList.forceLayout()
                            msgList.scrollToBottom()
                            msgList.traceScroll("bottomTimer:after", "")
                        }
                    }

                    // Una segunda pasada después del layout inicial estabiliza
                    // historiales con mensajes de decenas de miles de caracteres.
                    Timer {
                        id: viewportSettleTimer
                        interval: 120
                        onTriggered: {
                            msgList.traceScroll("settleTimer:before", "")
                            msgList.forceLayout()
                            msgList.normalizeViewport()
                            if (msgList.followBottom)
                                msgList.scrollToBottom()
                            msgList.traceScroll("settleTimer:after", "")
                        }
                    }

                    // Se reinicia con cada medición de delegates. Al quedar quieto el
                    // layout, restaura por última vez el fondo y recién entonces
                    // vuelve a permitir que contentY determine followBottom.
                    Timer {
                        id: modelResetSettleTimer
                        interval: 180
                        onTriggered: {
                            if (!msgList.restoreBottomAfterModelReset)
                                return
                            msgList.traceScroll("modelResetSettle:before", "")
                            msgList.forceLayout()
                            msgList.scrollToBottom()
                            msgList.restoreBottomAfterModelReset = false
                            msgList.followBottom = true
                            msgList.traceScroll("modelResetSettle:after", "")
                        }
                    }

                    // Scroll 1:1 para touchpad y por líneas para rueda. No animar
                    // contentY: reiniciar una animación por cada evento acumula
                    // destinos obsoletos mientras ListView recalcula delegates altos.
                    WheelHandler {
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        onWheel: function(ev) {
                            var minY = msgList.minContentY()
                            var maxY = msgList.maxContentY()
                            var pixelY = ev.pixelDelta.y
                            var delta = pixelY !== 0 ? pixelY : (ev.angleDelta.y / 120) * 72
                            msgList.setContentY("wheel",
                                                Math.max(minY, Math.min(maxY, msgList.contentY - delta)))
                            ev.accepted = true
                        }
                    }
                }

                // ── Tarjeta de aprobación de herramienta (human-in-the-loop) ──
                Rectangle {
                    id: approvalCard
                    readonly property var tool: App.agentPendingTool
                    readonly property string toolId: tool.id ?? ""
                    readonly property string kind: tool.kind ?? "read"
                    visible: toolId.length > 0
                    anchors {
                        left: parent.left; right: parent.right; bottom: parent.bottom
                        margins: 12
                    }
                    height: visible ? approvalCol.implicitHeight + 24 : 0
                    radius: 10
                    color: Theme.surfaceBg
                    border.width: 2
                    border.color: kind === "shell" ? Theme.errorText
                                : kind === "write" ? Theme.warnText
                                :                    Theme.borderColor

                    ColumnLayout {
                        id: approvalCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                text: approvalCard.kind === "shell" ? "🖥️"
                                    : approvalCard.kind === "write" ? "✏️" : "📄"
                                font.pixelSize: 16
                            }
                            Text {
                                text: "Aprobar herramienta"
                                color: Theme.textPrimary; font { pixelSize: 13; bold: true }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: (approvalCard.tool.tool ?? "")
                                      + (approvalCard.tool.title ? " · " + approvalCard.tool.title : "")
                                color: Theme.textMuted; font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                        }

                        // Banner del guardrail Zero-Autonomy: acción destructiva/irreversible
                        // frenada aun en modo automático. Sólo visible cuando reason=="destructive".
                        Rectangle {
                            Layout.fillWidth: true
                            visible: (approvalCard.tool.reason ?? "") === "destructive"
                            color: Qt.rgba(Theme.errorText.r, Theme.errorText.g, Theme.errorText.b, 0.12)
                            border.color: Theme.errorText; radius: 6
                            implicitHeight: destructiveWarn.implicitHeight + 12
                            RowLayout {
                                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 8 }
                                spacing: 6
                                Text { text: "⚠"; font.pixelSize: 14; color: Theme.errorText }
                                Text {
                                    id: destructiveWarn
                                    Layout.fillWidth: true
                                    text: "Acción destructiva/irreversible: el guardrail exige tu aprobación aunque el modo sea automático."
                                    color: Theme.errorText; font { pixelSize: 11; bold: true }
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            visible: (approvalCard.tool.reason ?? "") === "external_write"
                            color: Qt.rgba(Theme.warnText.r, Theme.warnText.g, Theme.warnText.b, 0.12)
                            border.color: Theme.warnText; radius: 6
                            implicitHeight: externalWarn.implicitHeight + 12
                            Text {
                                id: externalWarn
                                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 8 }
                                text: "Escritura externa MCP: se ejecutará exactamente el payload mostrado. La huella y el recibo quedarán auditados."
                                color: Theme.warnText; font { pixelSize: 11; bold: true }
                                wrapMode: Text.WordWrap
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: (approvalCard.tool.payloadHash ?? "").length > 0
                            text: "Payload SHA-256: " + (approvalCard.tool.payloadHash ?? "")
                                  + "\nCorrelation ID: " + (approvalCard.tool.correlationId ?? "")
                            color: Theme.textMuted
                            font { family: Theme.codeFont; pixelSize: 10 }
                            wrapMode: Text.WrapAnywhere
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            visible: (approvalCard.tool.detail ?? "").length > 0
                            color: Theme.inputBg; radius: 6
                            border.color: Theme.borderColor
                            implicitHeight: detailText.implicitHeight + 16
                            TextEdit {
                                id: detailText
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
                                text: approvalCard.tool.detail ?? ""
                                color: Theme.textSecondary
                                font { family: Theme.codeFont; pixelSize: 12 }
                                wrapMode: TextEdit.WrapAnywhere
                                readOnly: true; selectByMouse: true
                            }
                        }

                        // Preview de diff (solo write_file).
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.min(diffPreview.implicitHeight + 16, 180)
                            visible: (approvalCard.tool.diff ?? "").length > 0
                            color: Theme.baseBg; radius: 6
                            border.color: Theme.borderColor
                            clip: true
                            Flickable {
                                anchors { fill: parent; margins: 8 }
                                contentHeight: diffPreview.implicitHeight
                                contentWidth: width
                                clip: true
                                ScrollBar.vertical: LcScrollBar { policy: ScrollBar.AsNeeded }
                                TextEdit {
                                    id: diffPreview
                                    width: parent.width
                                    text: approvalCard.tool.diff ?? ""
                                    color: Theme.textSecondary
                                    font { family: Theme.codeFont; pixelSize: 11 }
                                    wrapMode: TextEdit.NoWrap
                                    readOnly: true; selectByMouse: true
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Item { Layout.fillWidth: true }
                            LcButton {
                                text: "Rechazar"; danger: true
                                onClicked: App.rejectAgentTool(approvalCard.toolId)
                            }
                            LcButton {
                                text: "Aprobar"
                                onClicked: App.approveAgentTool(approvalCard.toolId, false)
                            }
                            LcButton {
                                text: "Siempre"; secondary: true
                                onClicked: App.approveAgentTool(approvalCard.toolId, true)
                            }
                        }
                    }
                }

                // Terminal mode placeholder
                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 20
                    visible: App.agentRunning && App.agentInTerminal
                    Text { Layout.alignment: Qt.AlignHCenter; text: "🖥️"; font.pixelSize: 48 }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: App.activeAgentAdapter + " · ejecutándose en terminal externa"
                        color: Theme.successText; font { pixelSize: 15; bold: true }
                    }
                }
            }

            // ── Vista terminal (raw log) ──────────────────────────────────────
            Item {
                anchors.fill: parent
                visible: root.currentView === 1

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    visible: !App.agentRunning && !App.agentStarting && App.agentLog.length === 0

                    Item { Layout.fillWidth: true; Layout.fillHeight: true }
                    ColumnLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 16
                        Text { Layout.alignment: Qt.AlignHCenter; text: "🤖"; font.pixelSize: 48 }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: (App.langV, App.l("agent.stopped"))
                            color: Theme.textMuted; font.pixelSize: 14
                        }
                    }
                    Item { Layout.fillWidth: true; Layout.fillHeight: true }
                }

                Rectangle {
                    anchors.fill: parent
                    color: Theme.logBg
                    visible: App.agentLog.length > 0

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0

                        ScrollView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            anchors.margins: 8
                            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                            TextArea {
                                readOnly: true
                                text: App.agentLog
                                color: Theme.textSecondary
                                font { family: Theme.codeFont; pixelSize: 12 }
                                wrapMode: TextArea.WrapAnywhere
                                background: null
                                selectByMouse: true
                                onTextChanged: cursorPosition = length
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true; height: 24
                            color: Theme.surfaceBg
                            visible: resolvedAdapter.length > 0 && resolvedAdapter !== "none"
                            Text {
                                anchors { verticalCenter: parent.verticalCenter; left: parent.left; leftMargin: 12 }
                                text: "native log: " + App.agentNativeLogDir(resolvedAdapter)
                                color: Theme.textMuted
                                font { family: Theme.codeFont; pixelSize: 10 }
                                elide: Text.ElideLeft; width: parent.width - 24
                            }
                        }
                    }
                }
            }

            }  // main content Item
        }  // RowLayout

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

        // ── Input bar ────────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            // Es hijo de ColumnLayout: `height` solo no es una restricción de
            // layout y puede terminar comprimido por debajo de sus controles,
            // que luego se dibujan fuera del borde inferior de la ventana.
            implicitHeight: inputCol.implicitHeight + 16
            Layout.preferredHeight: implicitHeight
            Layout.minimumHeight: implicitHeight
            color: Theme.baseBg
            visible: App.agentRunning && !App.agentInTerminal

            Column {
                id: inputCol
                anchors { left: parent.left; right: parent.right
                          verticalCenter: parent.verticalCenter; margins: 8 }
                spacing: 6

                // Cola visible: mantiene los próximos mensajes a la vista sin
                // obligar a abrir un menú ni ocultar el texto que se enviará.
                Column {
                    width: parent.width
                    spacing: 4
                    visible: App.agentQueuedCount > 0
                    Row {
                        width: parent.width
                        Text { text: "Mensajes en cola"; color: Theme.textSecondary; font { pixelSize: 11; bold: true } }
                        Item { width: Math.max(0, parent.width - queueClearAgent.implicitWidth - 150); height: 1 }
                        LcButton { id: queueClearAgent; text: "Vaciar cola"; danger: true; onClicked: App.clearAgentQueue() }
                    }
                    ScrollView {
                        width: parent.width
                        // El alto depende del contador del backend (y no del
                        // implicitHeight del contenido de ScrollView, que puede
                        // calcularse como 0 antes de instanciar el Repeater).
                        height: Math.min(220, Math.max(52, App.agentQueuedCount * 56))
                        clip: true
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        Column {
                            id: agentQueueRows
                            width: parent.width
                            spacing: 4
                            Repeater {
                                model: App.agentQueuedMessages
                                Rectangle {
                                    required property int index
                                    required property string modelData
                                    width: agentQueueRows.width
                                    height: 52
                                    radius: 6; color: Theme.inputBg; border.color: Theme.borderColor
                                    RowLayout {
                                        anchors.fill: parent; anchors.margins: 6; spacing: 6
                                        Text { text: (index + 1) + ")"; color: Theme.textMuted; font.bold: true }
                                        Text { Layout.fillWidth: true; text: modelData; color: Theme.textPrimary
                                            wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight }
                                        LcButton { text: "Previsualizar"; secondary: true
                                            onClicked: { agentQueueDialog.editIndex = -1; agentQueueDialog.open() } }
                                        LcButton { text: "Editar"; secondary: true
                                            onClicked: { agentQueueDialog.editIndex = index; agentQueueDialog.open() } }
                                        LcButton { text: "Eliminar"; danger: true; onClicked: App.removeAgentQueuedMessage(index) }
                                    }
                                }
                            }
                        }
                    }
                }

                // Chips de adjuntos pendientes.
                Flow {
                    width: parent.width
                    spacing: 6
                    visible: root.agentAttachments.length > 0
                    Repeater {
                        model: root.agentAttachments
                        Rectangle {
                            radius: 5; color: Theme.inputBg; border.color: Theme.borderColor
                            implicitHeight: 22; implicitWidth: chipRow.implicitWidth + 12
                            Row {
                                id: chipRow; anchors.centerIn: parent; spacing: 6
                                Text { text: "📎 " + root.fileName(modelData); color: Theme.textSecondary
                                       font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                                Text { text: "✕"; color: Theme.textMuted; font.pixelSize: 11
                                       anchors.verticalCenter: parent.verticalCenter
                                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                        onClicked: { let a = root.agentAttachments.slice(); a.splice(index, 1)
                                                     root.agentAttachments = a } }
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    width: parent.width
                    spacing: 8

                    // Adjuntar archivos (imágenes solo si el modelo tiene visión).
                    LcButton {
                        text: "📎"
                        secondary: true
                        enabled: App.backendAvailable && (App.serverReady || !App.serverRunning) && !agentInput.busy
                        onClicked: {
                            const picked = App.pickAgentAttachments()
                            if (picked && picked.length > 0)
                                root.agentAttachments = root.agentAttachments.concat(picked)
                        }
                    }

                    ScrollView {
                        id: agentInputFrame
                        Layout.fillWidth: true
                        readonly property real minimumInputHeight: 34
                        readonly property real maximumInputHeight: Math.max(minimumInputHeight, root.height * 0.5)
                        Layout.preferredHeight: Math.min(maximumInputHeight,
                                                         Math.max(minimumInputHeight,
                                                                  agentInput.contentHeight
                                                                  + agentInput.topPadding
                                                                  + agentInput.bottomPadding))
                        Layout.minimumHeight: minimumInputHeight
                        Layout.maximumHeight: maximumInputHeight
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        ScrollBar.vertical.policy: agentInput.contentHeight + agentInput.topPadding
                                                   + agentInput.bottomPadding > height
                                                   ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                        clip: true
                        background: Rectangle {
                            radius: 6
                            color: Theme.inputBg
                            border.color: agentInput.activeFocus ? Theme.inputBorderFocus
                                                                 : Theme.inputBorderColor
                            border.width: agentInput.activeFocus ? 2 : 1
                        }

                        TextArea {
                            id: agentInput
                            width: agentInputFrame.availableWidth
                            implicitHeight: Math.max(agentInputFrame.minimumInputHeight,
                                                     contentHeight + topPadding + bottomPadding)
                            enabled: App.backendAvailable && (App.serverReady || !App.serverRunning)
                            readonly property bool busy: root.hasTypingMessage
                            color: Theme.textPrimary
                            placeholderTextColor: Theme.textMuted
                            font.pixelSize: 13
                            leftPadding: 10
                            rightPadding: 10
                            topPadding: 7
                            bottomPadding: 7
                            wrapMode: TextArea.WrapAtWordBoundaryOrAnywhere
                            selectByMouse: true
                            background: null
                            placeholderText: (!App.backendAvailable)
                            ? "Servidor no disponible. Iniciá el modelo en Lanzar."
                            : (!App.serverReady && App.serverRunning
                               ? "Modelo cargando..."
                               : (busy
                                  ? ("Enter encola · Shift+Enter interrumpe"
                                     + (App.agentQueuedCount > 0 ? "  ·  " + App.agentQueuedCount + " en cola" : ""))
                                  : (App.langV, App.l("agent.input"))))
                            // @-mentions: recalcular el popup al cambiar texto/cursor.
                            onTextChanged: root.updateMention()
                            onCursorPositionChanged: root.updateMention()
                        // Enter = enviar (idle) o encolar (ocupado). Shift+Enter = interrumpir.
                        // Si el popup de @-mentions está abierto: ↑/↓ navegan, Enter acepta, Esc cierra.
                        Keys.onPressed: (event) => {
                            if (mentionPopup.visible) {
                                if (event.key === Qt.Key_Down) {
                                    root.mentionActive = Math.min(root.mentionActive + 1, root.mentionSuggestions.length - 1)
                                    event.accepted = true; return
                                }
                                if (event.key === Qt.Key_Up) {
                                    root.mentionActive = Math.max(root.mentionActive - 1, 0)
                                    event.accepted = true; return
                                }
                                if (event.key === Qt.Key_Escape) { mentionPopup.close(); event.accepted = true; return }
                                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                                    || event.key === Qt.Key_Tab) {
                                    root.acceptMention(root.mentionSuggestions[root.mentionActive])
                                    event.accepted = true; return
                                }
                            }
                            if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_V) {
                                const p = App.pasteClipboardImage()
                                if (p && p.length > 0) {
                                    if (root.agentAttachments.indexOf(p) < 0)
                                        root.agentAttachments = root.agentAttachments.concat([p])
                                    event.accepted = true
                                    return
                                }
                            }
                            if (event.key !== Qt.Key_Return && event.key !== Qt.Key_Enter) return
                            event.accepted = true
                            const t = text.trim()
                            if (!App.backendAvailable) return
                            if (event.modifiers & Qt.ShiftModifier) {
                                if (t.length > 0) { App.steerAgent(t); text = "" }
                                return
                            }
                            if (busy) { if (t.length > 0) { App.queueAgent(t); text = "" } return }
                            root.agentSend()
                        }

                        // Popup de sugerencias de @-mention (se abre hacia arriba).
                        Popup {
                            id: mentionPopup
                            y: -height - 4
                            x: 0
                            width: Math.max(agentInput.width, 280)
                            implicitHeight: Math.min(mentionList.contentHeight + 8, 200)
                            padding: 4
                            closePolicy: Popup.NoAutoClose
                            background: Rectangle { color: Theme.inputBg
                                                    radius: 8; border.color: Theme.borderColor }
                            ListView {
                                id: mentionList
                                anchors.fill: parent
                                clip: true
                                model: root.mentionSuggestions
                                currentIndex: root.mentionActive
                                delegate: Rectangle {
                                    width: mentionList.width
                                    height: 24
                                    radius: 5
                                    color: index === root.mentionActive ? Theme.highlight : "transparent"
                                    Text {
                                        anchors { left: parent.left; right: parent.right
                                                  verticalCenter: parent.verticalCenter; margins: 8 }
                                        text: "📄 " + modelData
                                        color: Theme.textPrimary; font { family: Theme.codeFont; pixelSize: 11 }
                                        elide: Text.ElideMiddle
                                    }
                                    MouseArea {
                                        anchors.fill: parent; hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onEntered: root.mentionActive = index
                                        onClicked: root.acceptMention(modelData)
                                    }
                                }
                            }
                        }
                        }
                    }
                    // Idle: "Enviar" (incluye adjuntos).
                    LcButton {
                        visible: !agentInput.busy
                        text: (App.langV, App.l("agent.send"))
                        enabled: App.backendAvailable
                            && (agentInput.text.trim().length > 0 || root.agentAttachments.length > 0)
                        onClicked: root.agentSend()
                    }
                    // Ocupado + hay texto: Encolar / Interrumpir (solo texto).
                    LcButton {
                        visible: agentInput.busy && agentInput.text.trim().length > 0
                        text: "Encolar" + (App.agentQueuedCount > 0 ? " (" + App.agentQueuedCount + ")" : "")
                        onClicked: { const t = agentInput.text.trim(); if (t.length > 0) { App.queueAgent(t); agentInput.text = "" } }
                    }
                    LcButton {
                        visible: agentInput.busy && agentInput.text.trim().length > 0
                        text: "Interrumpir"
                        danger: true
                        onClicked: { const t = agentInput.text.trim(); if (t.length > 0) { App.steerAgent(t); agentInput.text = "" } }
                    }
                    // PARAR: abortar el turno sin enviar nada.
                    LcButton {
                        visible: agentInput.busy
                        text: "PARAR"
                        danger: true
                        onClicked: App.cancelAgentGeneration()
                    }
                    // Escalar al maestro (manual): pasa el problema actual al CLI/HTTP maestro.
                    LcButton {
                        visible: App.agentRunning && App.agentMasterConfigured()
                        secondary: true
                        text: "🎓 Maestro"
                        onClicked: {
                            const t = agentInput.text.trim()
                            if (App.escalateToMaster(t)) agentInput.text = ""
                        }
                    }
                    // Auditoría read-only del diff: pide métricas y delete-list,
                    // pero la tool no puede modificar archivos ni reemplaza la
                    // aprobación humana.
                    LcButton {
                        visible: App.agentRunning && !agentInput.busy
                        secondary: true
                        text: "🧹 Revisar frugalidad"
                        onClicked: root.sendAgentNow(
                            "Revisá el diff actual usando review_overengineering. "
                            + "Es una auditoría READ-ONLY: no edites ni borres archivos. "
                            + "Devolvé primero las métricas y después una delete-list "
                            + "explicable; conservá validaciones, seguridad, tests, "
                            + "accesibilidad y manejo de errores.", [])
                    }
                }
            }
        }
    }

    // La cola es editable: mostrar el texto completo evita perder mensajes antes
    // de que termine el turno actual, y las acciones operan por índice en backend.
    LcDialog {
        id: agentQueueDialog
        modal: true
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: Math.min(680, parent.width - 40)
        height: Math.min(520, parent.height - 40)
        title: "Mensajes en cola (" + App.agentQueuedCount + ")"
        property int editIndex: -1
        footer: RowLayout {
            width: parent.width
            LcButton { text: "Vaciar cola"; danger: true; onClicked: App.clearAgentQueue() }
            Item { Layout.fillWidth: true }
            LcButton { text: "Cerrar"; secondary: true; onClicked: agentQueueDialog.close() }
        }
        contentItem: ListView {
            id: agentQueueList
            clip: true
            spacing: 8
            model: App.agentQueuedMessages
            delegate: Rectangle {
                required property int index
                required property string modelData
                width: agentQueueList.width
                height: queueEditor.visible ? Math.max(118, queueEditor.contentHeight + 58) : preview.implicitHeight + 42
                radius: 7
                color: Theme.inputBg
                border.color: Theme.borderColor
                property bool editing: agentQueueDialog.editIndex === index
                Text { id: number; anchors { left: parent.left; top: parent.top; margins: 9 }
                    text: (index + 1) + "."; color: Theme.textMuted; font.bold: true }
                Text { id: preview; visible: !parent.editing
                    anchors { left: number.right; right: controls.left; top: parent.top; margins: 9 }
                    text: modelData; color: Theme.textPrimary; wrapMode: Text.Wrap; maximumLineCount: 5; elide: Text.ElideRight }
                TextArea { id: queueEditor; visible: parent.editing
                    anchors { left: number.right; right: controls.left; top: parent.top; margins: 7 }
                    text: modelData; color: Theme.textPrimary; wrapMode: TextArea.Wrap
                    background: Rectangle { color: Theme.baseBg; radius: 4; border.color: Theme.inputBorderColor } }
                Column {
                    id: controls
                    anchors { right: parent.right; top: parent.top; margins: 7 }
                    spacing: 5
                    LcButton { text: parent.parent.editing ? "Guardar" : "Editar"; secondary: true
                        onClicked: { if (parent.parent.editing) { if (App.updateAgentQueuedMessage(index, queueEditor.text)) agentQueueDialog.editIndex = -1 } else agentQueueDialog.editIndex = index } }
                    LcButton { text: "Eliminar"; danger: true; onClicked: App.removeAgentQueuedMessage(index) }
                }
            }
            Text { anchors.centerIn: parent; visible: App.agentQueuedCount === 0
                text: "No hay mensajes en cola."; color: Theme.textMuted }
        }
        onOpened: if (App.agentQueuedCount === 0) close()
    }

    // ── Sala multiagente: timeline persistente + presets ────────────────────
    LcDialog {
        id: roomDialog
        modal: true
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: Math.min(820, parent.width - 40)
        height: Math.min(680, parent.height - 40)
        title: "Sala multiagente"
        footer: null
        closePolicy: Popup.CloseOnEscape
        background: Rectangle { color: Theme.popupHeaderBg ?? Theme.baseBg; radius: 10; border.color: Theme.borderColor }
        contentItem: ColumnLayout {
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                LcComboBox {
                    id: roomCombo
                    Layout.fillWidth: true
                    model: App.agentRoomStore.rooms
                    textRole: "title"
                    valueRole: "id"
                    currentIndex: Math.max(0, indexOfValue(App.agentRoomStore.currentRoomId))
                    onActivated: App.agentRoomStore.currentRoomId = currentValue
                }
                LcButton {
                    text: "+ Nueva"
                    onClicked: App.createAgentRoom("Sala de trabajo", "")
                }
            }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: App.agentRoomStore.currentParticipants
                    Rectangle {
                        width: participantText.implicitWidth + 16
                        height: 25; radius: 12
                        color: Theme.inputBg; border.color: Theme.borderColor
                        Text {
                            id: participantText
                            anchors.centerIn: parent
                            text: (modelData.kind === "human" ? "👤 " : "🤖 ")
                                  + (modelData.name || modelData.id)
                            color: Theme.textSecondary; font.pixelSize: 11
                        }
                    }
                }
            }
            ListView {
                id: roomTimeline
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 8
                model: App.agentRoomStore.currentEvents
                onCountChanged: positionViewAtEnd()
                delegate: Rectangle {
                    width: roomTimeline.width
                    height: roomEventColumn.implicitHeight + 18
                    radius: 7
                    color: (modelData.author || "").startsWith("human:")
                           ? Theme.highlight : Theme.inputBg
                    border.color: Theme.borderColor
                    Column {
                        id: roomEventColumn
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 9 }
                        spacing: 4
                        Text {
                            width: parent.width
                            text: (modelData.author || "system") + " · " + (modelData.type || "message")
                            color: Theme.accent; font { pixelSize: 10; bold: true }
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: modelData.content || ""
                            color: Theme.textPrimary; font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                LcButton {
                    text: "/review"
                    enabled: App.agentRunning && roomGoal.text.trim().length > 0
                    onClicked: {
                        if (App.runAgentRoomPreset(App.agentRoomStore.currentRoomId, "review", roomGoal.text))
                            roomGoal.text = ""
                    }
                }
                LcButton {
                    text: "/autoprompt"
                    secondary: true
                    enabled: App.agentRunning && roomGoal.text.trim().length > 0
                    ToolTip.visible: hovered
                    ToolTip.text: "Planificar, construir, revisar, verificar y reparar con gates."
                    onClicked: {
                        if (App.runAgentRoomPreset(App.agentRoomStore.currentRoomId, "autoprompt", roomGoal.text))
                            roomGoal.text = ""
                    }
                }
                LcButton {
                    text: "/council"; secondary: true
                    enabled: App.agentRunning && roomGoal.text.trim().length > 0
                    onClicked: {
                        if (App.runAgentRoomPreset(App.agentRoomStore.currentRoomId, "council", roomGoal.text))
                            roomGoal.text = ""
                    }
                }
                LcButton {
                    text: "/research"; secondary: true
                    enabled: App.agentRunning && roomGoal.text.trim().length > 0
                    onClicked: {
                        if (App.runAgentRoomPreset(App.agentRoomStore.currentRoomId, "research", roomGoal.text))
                            roomGoal.text = ""
                    }
                }
                LcTextField {
                    id: roomGoal
                    Layout.fillWidth: true
                    placeholderText: "Mensaje, objetivo o @participante…"
                    onAccepted: {
                        if (App.sendAgentRoomMessage(App.agentRoomStore.currentRoomId,
                                                     text, []))
                            text = ""
                    }
                }
                LcButton {
                    text: "Enviar"
                    enabled: App.agentRunning && roomGoal.text.trim().length > 0
                    onClicked: {
                        if (App.sendAgentRoomMessage(App.agentRoomStore.currentRoomId,
                                                     roomGoal.text, []))
                            roomGoal.text = ""
                    }
                }
            }
        }
    }

    // ── Git requerido para subagents ──────────────────────────────────────────
    Connections {
        target: App
        function onGitRequiredForSubagents() { gitInstallDialog.open() }
    }
    LcDialog {
        id: gitInstallDialog
        modal: true
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: 460
        height: 210
        leftPadding: 20
        rightPadding: 20
        topPadding: 16
        bottomPadding: 16
        title: "Git requerido para subagents"
        footer: null
        closePolicy: Popup.CloseOnEscape
        background: Rectangle { color: Theme.popupHeaderBg ?? Theme.baseBg; radius: 10; border.color: Theme.borderColor }
        contentItem: ColumnLayout {
            spacing: 12
            Text {
                Layout.fillWidth: true; Layout.maximumWidth: 420
                wrapMode: Text.WordWrap; color: Theme.textPrimary; font.pixelSize: 13
                text: App.installingGit
                    ? "Instalando Git vía winget… (puede tardar). Cuando termine, reabrí la terminal/app si hace falta y reintentá la tarea."
                    : "El agente quiso usar subagents (tool task) para trabajar en paralelo, pero necesita Git para aislar cada subtarea en una git worktree. ¿Instalar Git ahora?"
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 10
                Item { Layout.fillWidth: true }
                LcButton { text: "Ahora no"; secondary: true; onClicked: gitInstallDialog.close() }
                LcButton {
                    text: App.installingGit ? "Instalando…" : "Instalar Git"
                    enabled: !App.installingGit && !App.gitAvailable
                    onClicked: App.installGit()
                }
            }
        }
    }

    // ── Renombrar sesión ──────────────────────────────────────────────────────
    Dialog {
        id: agentRenameDialog
        property string targetId: ""
        modal: true
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: 420
        height: 200
        closePolicy: Popup.CloseOnEscape

        function commit() {
            const t = agentRenameField.text.trim()
            if (t.length === 0) return
            App.renameOpencodeSession(agentRenameDialog.targetId, t)
            agentRenameDialog.close()
        }

        background: Rectangle {
            color: Theme.popupBg; radius: 12
            border.color: Theme.popupBorderColor; border.width: 1
        }
        Overlay.modal: Rectangle { color: Theme.overlayColor }

        header: Rectangle {
            color: Theme.popupHeaderBg; height: 50; radius: 12
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 12; color: Theme.popupHeaderBg }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1;  color: Theme.popupHeaderBorder }
            Text {
                anchors { left: parent.left; leftMargin: 20; verticalCenter: parent.verticalCenter }
                text: "Renombrar sesión"
                font { pixelSize: 14; bold: true }
                color: Theme.textPrimary
            }
        }

        footer: Rectangle {
            color: Theme.popupHeaderBg; height: 50; radius: 12
            Rectangle { anchors.top: parent.top; width: parent.width; height: 12; color: Theme.popupHeaderBg }
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1;  color: Theme.popupHeaderBorder }
            Row {
                anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                spacing: 10
                LcButton {
                    text: "Cancelar"; secondary: true
                    onClicked: agentRenameDialog.close()
                }
                LcButton {
                    text: "Guardar"
                    enabled: agentRenameField.text.trim().length > 0
                    onClicked: agentRenameDialog.commit()
                }
            }
        }

        contentItem: Item {
            width: 380; height: 46
            LcTextField {
                id: agentRenameField
                anchors.fill: parent
                placeholderText: "Nuevo nombre"
                Keys.onReturnPressed: agentRenameDialog.commit()
            }
        }

        onOpened: { agentRenameField.forceActiveFocus(); agentRenameField.selectAll() }
    }

    // ── Borrar sesión (confirmación) ──────────────────────────────────────────
    Dialog {
        id: agentDeleteDialog
        property string targetId: ""
        property string sessionTitle: ""
        modal: true
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: 420
        height: 200
        closePolicy: Popup.CloseOnEscape

        background: Rectangle {
            color: Theme.popupBg; radius: 12
            border.color: Theme.popupBorderColor; border.width: 1
        }
        Overlay.modal: Rectangle { color: Theme.overlayColor }

        header: Rectangle {
            color: Theme.popupHeaderBg; height: 50; radius: 12
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 12; color: Theme.popupHeaderBg }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1;  color: Theme.popupHeaderBorder }
            Text {
                anchors { left: parent.left; leftMargin: 20; verticalCenter: parent.verticalCenter }
                text: "Borrar sesión"
                font { pixelSize: 14; bold: true }
                color: Theme.textPrimary
            }
        }

        footer: Rectangle {
            color: Theme.popupHeaderBg; height: 50; radius: 12
            Rectangle { anchors.top: parent.top; width: parent.width; height: 12; color: Theme.popupHeaderBg }
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1;  color: Theme.popupHeaderBorder }
            Row {
                anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                spacing: 10
                LcButton {
                    text: "Cancelar"; secondary: true
                    onClicked: agentDeleteDialog.close()
                }
                LcButton {
                    text: "Borrar"
                    danger: true
                    onClicked: {
                        App.deleteOpencodeSession(agentDeleteDialog.targetId)
                        agentDeleteDialog.close()
                    }
                }
            }
        }

        contentItem: Item {
            Text {
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 20 }
                text: "Se borrará la sesión \"" + agentDeleteDialog.sessionTitle
                      + "\". Esta acción no se puede deshacer."
                color: Theme.textPrimary
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }
        }
    }

    // ── Menú contextual de archivo creado (click derecho en la tarjeta de diff) ──
    Menu {
        id: fileCtxMenu
        property string filePath: ""
        MenuItem {
            text: "Abrir carpeta contenedora"
            enabled: fileCtxMenu.filePath.length > 0
            onTriggered: App.openContainingFolder(fileCtxMenu.filePath)
        }
        MenuItem {
            text: "Copiar ruta"
            enabled: fileCtxMenu.filePath.length > 0
            onTriggered: App.copyToClipboard(fileCtxMenu.filePath)
        }
    }

    // ── Menú contextual de proyecto (click derecho en el encabezado) ──────────
    Menu {
        id: agentProjectCtxMenu
        property string projectName: ""
        property string projectDir: ""
        MenuItem {
            text: "Borrar proyecto"
            onTriggered: if (agentProjectCtxMenu.projectName.length > 0) {
                agentDeleteProjectDialog.projectName = agentProjectCtxMenu.projectName
                agentDeleteProjectDialog.projectDir = agentProjectCtxMenu.projectDir
                agentDeleteProjectDialog.open()
            }
        }
    }

    // ── Borrar proyecto del agente (confirmación) ─────────────────────────────
    Dialog {
        id: agentDeleteProjectDialog
        property string projectName: ""
        property string projectDir: ""
        modal: true
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: 420
        height: 200
        closePolicy: Popup.CloseOnEscape

        background: Rectangle {
            color: Theme.popupBg; radius: 12
            border.color: Theme.popupBorderColor; border.width: 1
        }
        Overlay.modal: Rectangle { color: Theme.overlayColor }

        header: Rectangle {
            color: Theme.popupHeaderBg; height: 50; radius: 12
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 12; color: Theme.popupHeaderBg }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1;  color: Theme.popupHeaderBorder }
            Text {
                anchors { left: parent.left; leftMargin: 20; verticalCenter: parent.verticalCenter }
                text: "Borrar proyecto"
                font { pixelSize: 14; bold: true }
                color: Theme.textPrimary
            }
        }

        footer: Rectangle {
            color: Theme.popupHeaderBg; height: 50; radius: 12
            Rectangle { anchors.top: parent.top; width: parent.width; height: 12; color: Theme.popupHeaderBg }
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1;  color: Theme.popupHeaderBorder }
            Row {
                anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                spacing: 10
                LcButton {
                    text: "Cancelar"; secondary: true
                    onClicked: agentDeleteProjectDialog.close()
                }
                LcButton {
                    text: "Borrar"
                    danger: true
                    onClicked: {
                        App.deleteOpencodeProject(agentDeleteProjectDialog.projectDir)
                        agentDeleteProjectDialog.close()
                    }
                }
            }
        }

        contentItem: Item {
            Text {
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 20 }
                text: "Se borrará el proyecto \"" + agentDeleteProjectDialog.projectName
                      + "\" y todas sus sesiones. Esta acción no se puede deshacer."
                color: Theme.textPrimary
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }
        }
    }

    OpencodeConfigDialog { id: opencodeConfigDialog }

    // ── Ajustes del agente (LlamaAgentBackend): system prompt + temperatura ─────
    Dialog {
        id: agentTuningDialog
        modal: true
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: 580
        height: 440
        closePolicy: Popup.CloseOnEscape

        background: Rectangle {
            color: Theme.popupBg; radius: 12
            border.color: Theme.popupBorderColor; border.width: 1
        }
        Overlay.modal: Rectangle { color: Theme.overlayColor }

        header: Rectangle {
            color: Theme.popupHeaderBg; height: 56; radius: 12
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 12; color: Theme.popupHeaderBg }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1;  color: Theme.popupHeaderBorder }
            Column {
                anchors { left: parent.left; leftMargin: 22; verticalCenter: parent.verticalCenter }
                spacing: 2
                Text { text: "Ajustes del agente"; color: Theme.textPrimary; font.pixelSize: 14; font.bold: true }
                Text { text: "instrucciones extra + temperatura para tool-calling"; color: Theme.textMuted; font.pixelSize: 11 }
            }
        }

        footer: Rectangle {
            color: Theme.popupHeaderBg; height: 56; radius: 12
            Rectangle { anchors.top: parent.top; width: parent.width; height: 12; color: Theme.popupHeaderBg }
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1;  color: Theme.popupHeaderBorder }
            Row {
                anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                spacing: 10
                LcButton { text: "Cancelar"; secondary: true; onClicked: agentTuningDialog.close() }
                LcButton {
                    text: "Guardar"
                    onClicked: {
                        App.agentSystemPrompt = agentTuningSystem.text
                        App.agentPermRules = agentTuningPerm.text
                        const t = parseFloat(agentTuningTemp.text)
                        App.agentTemperature = (agentTuningTemp.text.trim().length > 0 && !isNaN(t)) ? t : -1
                        agentTuningDialog.close()
                    }
                }
            }
        }

        contentItem: ColumnLayout {
            spacing: 10

            Text { text: "Instrucciones del agente (system prompt extra):"; color: Theme.textSecondary; font.pixelSize: 12 }
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true
                color: Theme.inputBg; radius: 8; border.color: Theme.borderColor; clip: true
                ScrollView {
                    anchors.fill: parent; anchors.margins: 2
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    TextArea {
                        id: agentTuningSystem
                        placeholderText: "p.ej. priorizá cambios mínimos, corré tests antes de terminar, no toques archivos de config…"
                        color: Theme.textPrimary; placeholderTextColor: Theme.textMuted
                        font { family: Theme.codeFont; pixelSize: 12 }
                        wrapMode: TextArea.WrapAtWordBoundaryOrAnywhere
                        background: null; padding: 10; selectByMouse: true
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true; spacing: 10
                Text { text: "Temperatura:"; color: Theme.textSecondary; font.pixelSize: 12 }
                LcTextField {
                    id: agentTuningTemp
                    Layout.preferredWidth: 100
                    placeholderText: "auto"
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                }
                Text {
                    text: "vacío = default del server. Para tool-calling estable: 0.0–0.3"
                    color: Theme.textMuted; font.pixelSize: 11; Layout.fillWidth: true
                }
            }

            Text { text: "Permisos por patrón (una regla por línea, antes de la política global):"
                   color: Theme.textSecondary; font.pixelSize: 12 }
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 96
                color: Theme.inputBg; radius: 8; border.color: Theme.borderColor; clip: true
                ScrollView {
                    anchors.fill: parent; anchors.margins: 2
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    TextArea {
                        id: agentTuningPerm
                        placeholderText: "allow|deny|ask  [read:|write:|shell:]<glob>\n"
                            + "deny **/.env\ndeny write:**/secrets/**\nallow write:src/**\nask shell:rm *"
                        color: Theme.textPrimary; placeholderTextColor: Theme.textMuted
                        font { family: Theme.codeFont; pixelSize: 12 }
                        wrapMode: TextArea.NoWrap
                        background: null; padding: 10; selectByMouse: true
                    }
                }
            }
            Text {
                text: "deny = bloquea · allow = auto-aprueba · ask = pide aprobación. kind opcional (read/write/shell)."
                color: Theme.textMuted; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap
            }
        }
    }

    Dialog {
        id: skillsDialog
        modal: true
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: 680
        height: 500
        closePolicy: Popup.CloseOnEscape
        background: Rectangle {
            color: Theme.popupBg; radius: 12
            border.color: Theme.popupBorderColor; border.width: 1
        }
        Overlay.modal: Rectangle { color: Theme.overlayColor }
        header: Rectangle {
            color: Theme.popupHeaderBg; height: 58; radius: 12
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 12; color: Theme.popupHeaderBg }
            Column {
                anchors { left: parent.left; leftMargin: 22; verticalCenter: parent.verticalCenter }
                Text { text: "Habilidades portables"; color: Theme.textPrimary; font.pixelSize: 15; font.bold: true }
                Text {
                    text: "Globales: AppLocalData/skills · Proyecto: .llamacode/skills"
                    color: Theme.textMuted; font.pixelSize: 11
                }
            }
        }
        footer: Rectangle {
            color: Theme.popupHeaderBg; height: 54; radius: 12
            LcButton {
                anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                text: "Cerrar"; secondary: true; onClicked: skillsDialog.close()
            }
        }
        contentItem: RowLayout {
            spacing: 10
            Rectangle {
                Layout.preferredWidth: 260; Layout.fillHeight: true
                color: Theme.inputBg; radius: 8; border.color: Theme.borderColor
                ListView {
                    anchors.fill: parent; anchors.margins: 4
                    clip: true; spacing: 3
                    model: root.portableSkillRows
                    delegate: Rectangle {
                        required property var modelData
                        width: ListView.view.width; height: 58; radius: 6
                        color: root.selectedPortableSkill.name === modelData.name
                               ? Theme.highlight : "transparent"
                        Column {
                            anchors { fill: parent; margins: 8 }
                            Text { text: modelData.name; color: Theme.textPrimary; font.bold: true; font.pixelSize: 12 }
                            Text {
                                text: modelData.scope + " · " + modelData.description
                                color: Theme.textMuted; font.pixelSize: 10
                                width: parent.width; elide: Text.ElideRight
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: root.selectedPortableSkill = App.portableSkill(modelData.name)
                        }
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: root.portableSkillRows.length === 0
                        text: "No hay skills instalados"
                        color: Theme.textMuted; font.pixelSize: 12
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true
                color: Theme.inputBg; radius: 8; border.color: Theme.borderColor
                ScrollView {
                    anchors.fill: parent; anchors.margins: 8
                    TextArea {
                        readOnly: true; selectByMouse: true
                        wrapMode: TextArea.WrapAtWordBoundaryOrAnywhere
                        color: Theme.textPrimary; background: null
                        font { family: Theme.codeFont; pixelSize: 11 }
                        text: root.selectedPortableSkill.instructions
                              || "Seleccioná una habilidad para inspeccionar sus instrucciones."
                    }
                }
            }
        }
    }

    // ── Editor de memoria por proyecto (.llamacode/memory.md) ──────────────
    Dialog {
        id: memoryDialog
        property alias text: memoryArea.text
        modal: true
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: 620
        height: 480
        closePolicy: Popup.CloseOnEscape

        background: Rectangle {
            color: Theme.popupBg; radius: 12
            border.color: Theme.popupBorderColor; border.width: 1
        }
        Overlay.modal: Rectangle { color: Theme.overlayColor }

        header: Rectangle {
            color: Theme.popupHeaderBg; height: 56; radius: 12
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 12; color: Theme.popupHeaderBg }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1;  color: Theme.popupHeaderBorder }
            Column {
                anchors { left: parent.left; leftMargin: 22; verticalCenter: parent.verticalCenter }
                spacing: 2
                Text { text: "Memoria del proyecto"; color: Theme.textPrimary; font.pixelSize: 14; font.bold: true }
                Text {
                    text: ".llamacode/memory.md · se inyecta en el system prompt del agente"
                    font.pixelSize: 11; color: Theme.textMuted
                }
            }
        }

        footer: Rectangle {
            color: Theme.popupHeaderBg; height: 56; radius: 12
            Rectangle { anchors.top: parent.top; width: parent.width; height: 12; color: Theme.popupHeaderBg }
            Rectangle { anchors.top: parent.top; width: parent.width; height: 1;  color: Theme.popupHeaderBorder }
            Row {
                anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                spacing: 10
                LcButton { text: "Cancelar"; secondary: true; onClicked: memoryDialog.close() }
                LcButton {
                    text: "Guardar"
                    onClicked: { App.writeAgentMemory("", memoryArea.text); memoryDialog.close() }
                }
            }
        }

        contentItem: Rectangle {
            color: Theme.inputBg; radius: 8
            border.color: Theme.borderColor
            clip: true
            ScrollView {
                anchors.fill: parent; anchors.margins: 2
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                TextArea {
                    id: memoryArea
                    placeholderText: "Convenciones, comandos de build/test, arquitectura, do/don't del proyecto…"
                    color: Theme.textPrimary
                    placeholderTextColor: Theme.textMuted
                    font { family: Theme.codeFont; pixelSize: 12 }
                    wrapMode: TextArea.WrapAtWordBoundaryOrAnywhere
                    background: null
                    padding: 10
                    selectByMouse: true
                }
            }
        }
    }
}
