import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0

Item {
    id: root

    property bool newProjectDialogOpen: false
    property var thinkExpanded: ({})
    property var chatAttachments: []
    property string chatSearchText: ""
    property var chatSearchResults: []

    function refreshChatSearch() {
        root.chatSearchResults = root.chatSearchText.trim().length > 0
                                  ? App.searchChatHistory(root.chatSearchText) : []
    }

    Popup {
        id: samplingPopup
        parent: Overlay.overlay
        modal: false
        padding: 14
        x: Math.max(8, root.width - width - 18)
        y: 54
        width: 260
        background: Rectangle {
            color: Theme.popupBg; radius: 8
            border.color: Theme.popupBorderColor
        }
        ColumnLayout {
            width: parent.width
            spacing: 8
            Text {
                text: "Sampling de esta sesión"
                color: Theme.textPrimary
                font.bold: true
            }
            Text {
                text: "Vacío hereda el valor del servidor."
                color: Theme.textMuted
                font.pixelSize: 11
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "Temp."; color: Theme.textSecondary; Layout.preferredWidth: 70 }
                LcTextField {
                    id: chatTempField
                    Layout.fillWidth: true
                    text: App.chatTemperature >= 0 ? String(App.chatTemperature) : ""
                    placeholderText: "0.6"
                    onEditingFinished: App.chatTemperature = text.trim().length ? Number(text) : -1
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "Top-p"; color: Theme.textSecondary; Layout.preferredWidth: 70 }
                LcTextField {
                    id: chatTopPField
                    Layout.fillWidth: true
                    text: App.chatTopP >= 0 ? String(App.chatTopP) : ""
                    placeholderText: "0.95"
                    onEditingFinished: App.chatTopP = text.trim().length ? Number(text) : -1
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "Top-k"; color: Theme.textSecondary; Layout.preferredWidth: 70 }
                LcTextField {
                    id: chatTopKField
                    Layout.fillWidth: true
                    text: App.chatTopK >= 0 ? String(App.chatTopK) : ""
                    placeholderText: "20"
                    onEditingFinished: App.chatTopK = text.trim().length ? Number(text) : -1
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "Min-p"; color: Theme.textSecondary; Layout.preferredWidth: 70 }
                LcTextField {
                    Layout.fillWidth: true
                    text: App.chatMinP >= 0 ? String(App.chatMinP) : ""
                    placeholderText: "0.0"
                    onEditingFinished: App.chatMinP = text.trim().length ? Number(text) : -1
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "Repeat"; color: Theme.textSecondary; Layout.preferredWidth: 70 }
                LcTextField {
                    Layout.fillWidth: true
                    text: App.chatRepeatPenalty >= 0 ? String(App.chatRepeatPenalty) : ""
                    placeholderText: "1.0"
                    onEditingFinished: App.chatRepeatPenalty = text.trim().length ? Number(text) : -1
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                LcButton { text: "Cerrar"; secondary: true; onClicked: samplingPopup.close() }
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
        readonly property bool responseActive: App.chatGenerating

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
                        App.applyThinkingChange(thinkingRestartDialog.targetEnabled, "chat", "none")
                        thinkingRestartDialog.close()
                    }
                }
                LcButton {
                    text: "Reiniciar luego de esta respuesta"; secondary: true
                    enabled: thinkingRestartDialog.responseActive && App.serverRunning
                    onClicked: {
                        App.applyThinkingChange(thinkingRestartDialog.targetEnabled, "chat", "after-response")
                        thinkingRestartDialog.close()
                    }
                }
                LcButton {
                    text: "Reiniciar ahora sin esperar"
                    enabled: App.serverRunning
                    onClicked: {
                        App.applyThinkingChange(thinkingRestartDialog.targetEnabled, "chat", "now")
                        thinkingRestartDialog.close()
                    }
                }
            }
        }
    }

    // ── Mermaid: paths PNG renderizados y errores, por hash de source ────
    property var mermaidPaths: ({})
    property var mermaidErrors: ({})
    function mermaidUrl(p) { return "file:///" + String(p).replace(/\\/g, "/") }

    Connections {
        target: Mermaid
        function onRenderReady(hash, path) {
            const m = root.mermaidPaths; m[hash] = root.mermaidUrl(path); root.mermaidPaths = m
            const e = root.mermaidErrors; if (e[hash] !== undefined) { delete e[hash]; root.mermaidErrors = e }
        }
        function onRenderFailed(hash, reason) {
            const e = root.mermaidErrors; e[hash] = reason; root.mermaidErrors = e
        }
    }

    MermaidPreviewDialog { id: mermaidPreview }
    function openMermaidPreview(imgUrl, src, isSvg) {
        mermaidPreview.imageSource = imgUrl
        mermaidPreview.mermaidSource = src
        mermaidPreview.isSvg = isSvg === true
        mermaidPreview.open()
    }

    // ── Ancho del panel de chats (redimensionable + persistente) ─────────
    property int chatsPanelWidth: 220
    property int chatsPanelMin: 160
    property int chatsPanelMax: Math.max(chatsPanelMin, Math.min(520, width - 480))
    function clampChatsWidth(w) {
        return Math.max(chatsPanelMin, Math.min(chatsPanelMax, Math.round(w)))
    }
    property int _savedChatsWidth: parseInt(App.readSetting("chatPanelWidth", "0")) || 0
    property bool _cpRestored: false
    function tryRestoreChatsPanel() {
        if (_cpRestored) return
        if (_savedChatsWidth > 0 && chatsPanelMax > chatsPanelMin) {
            chatsPanelWidth = clampChatsWidth(_savedChatsWidth)
            _cpRestored = true
        }
    }
    onChatsPanelMaxChanged: {
        if (!_cpRestored) tryRestoreChatsPanel()
        else chatsPanelWidth = clampChatsWidth(chatsPanelWidth)
    }
    Component.onCompleted: tryRestoreChatsPanel()

    function fileName(p) { return p.split(/[\\/]/).pop() }

    // Envío normal (idle): texto + adjuntos.
    function doSend() {
        const t = inputField.text.trim()
        if (t.length === 0 && root.chatAttachments.length === 0) return
        if (root.chatAttachments.length > 0)
            App.sendChatMessageWithAttachments(t, root.chatAttachments)
        else
            App.sendChatMessage(t)
        inputField.text = ""
        root.chatAttachments = []
    }
    // Encolar: se envía cuando la respuesta actual termina (solo texto).
    function doQueue() {
        const t = inputField.text.trim()
        if (t.length === 0) return
        App.queueChat(t); inputField.text = ""
    }
    // Interrumpir: corta la respuesta actual y envía ya (solo texto).
    function doSteer() {
        const t = inputField.text.trim()
        if (t.length === 0) return
        App.steerChat(t); inputField.text = ""; root.chatAttachments = []
    }
    // Enter: enviar (idle) o encolar (generando).
    function enterPressed() {
        if (App.chatGenerating) doQueue(); else doSend()
    }

    function scrollToBottom() { msgList.followBottom = true; Qt.callLater(() => { msgList.scrollToBottom() }) }

    function projectIdForSection(sectionName) {
        for (let i = 0; i < App.chatSessions.length; i++) {
            if ((App.chatSessions[i].projectName ?? "") === sectionName)
                return App.chatSessions[i].projectId ?? ""
        }
        return ""
    }

    function extractThinkContent(text) {
        if (!text || text.length === 0) return ""
        let out = ""
        const re = /<think>([\s\S]*?)<\/think>/g
        let m
        while ((m = re.exec(text)) !== null) {
            const part = (m[1] ?? "").trim()
            if (part.length > 0) {
                if (out.length > 0) out += "\n\n"
                out += part
            }
        }
        return out
    }

    function stripThinkBlocks(text) {
        if (!text || text.length === 0) return ""
        return text.replace(/<think>[\s\S]*?<\/think>/g, "").trim()
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
            let ts = Number(modelData.completedAt ?? modelData.createdAt ?? 0)
            if (!isFinite(ts) || ts <= 0) return ""
            if (ts < 1000000000000) ts *= 1000
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

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ── Sessions panel ───────────────────────────────────────────────────
        Rectangle {
            Layout.preferredWidth: root.chatsPanelWidth
            Layout.fillHeight: true
            visible: App.serverRunning
            color: Theme.surfaceBg

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
                            text: "Chats"
                            color: Theme.textPrimary
                            font { pixelSize: 12; bold: true }
                            Layout.fillWidth: true
                        }
                        LcButton {
                            text: "⌕"
                            secondary: true
                            implicitWidth: 30
                            ToolTip.visible: hovered
                            ToolTip.text: "Buscar en el historial"
                            onClicked: chatSearchField.forceActiveFocus()
                        }
                    }
                }

                LcTextField {
                    id: chatSearchField
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    Layout.topMargin: 6
                    Layout.bottomMargin: 6
                    placeholderText: "Buscar chats…"
                    text: root.chatSearchText
                    onTextChanged: {
                        root.chatSearchText = text
                        root.refreshChatSearch()
                    }
                    Keys.onEscapePressed: {
                        text = ""
                        focus = false
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

                // ── Nuevo chat ────────────────────────────────────────────────
                Rectangle {
                    id: newChatBtn
                    Layout.fillWidth: true
                    height: 40
                    color: "transparent"
                    RowLayout {
                        anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                        spacing: 6
                        Text { text: "💬"; font.pixelSize: 11 }
                        Text {
                            text: "Nuevo chat"
                            color: Theme.textMuted
                            font.pixelSize: 12
                            Layout.fillWidth: true
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true
                        onEntered: newChatBtn.color = Theme.highlight
                        onExited:  newChatBtn.color = "transparent"
                        onClicked: App.newChatSession()
                    }
                }

                // ── Nuevo proyecto ────────────────────────────────────────────
                Rectangle {
                    id: newProjectBtn
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
                        onEntered: newProjectBtn.color = Theme.highlight
                        onExited:  newProjectBtn.color = "transparent"
                        onClicked: newProjectPopup.open()
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

                ListView {
                    id: sessionsList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: root.chatSearchText.trim().length > 0
                           ? root.chatSearchResults : App.chatSessions
                    ScrollBar.vertical: LcScrollBar { policy: ScrollBar.AsNeeded }

                    section.property: "projectName"
                    section.criteria: ViewSection.FullString

                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 32
                        visible: sessionsList.count === 0
                        text: root.chatSearchText.trim().length > 0
                              ? "No hay coincidencias."
                              : "Sin chats todavía.\nUsá + para crear uno."
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        font.pixelSize: 11
                    }

                    section.delegate: Rectangle {
                        width: sessionsList.width
                        height: 28
                        color: Theme.baseBg

                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.RightButton
                            onClicked: {
                                projectCtxMenu.projectName = section
                                projectCtxMenu.popup()
                            }
                        }

                        RowLayout {
                            anchors { fill: parent; leftMargin: 8; rightMargin: 6 }
                            spacing: 4
                            Text { text: "📁"; font.pixelSize: 10 }
                            Text {
                                Layout.fillWidth: true
                                text: section
                                color: Theme.textMuted
                                font { pixelSize: 10; bold: true }
                                elide: Text.ElideLeft
                            }
                            Rectangle {
                                width: 20; height: 20; radius: 4
                                color: addChatHover.containsMouse ? Theme.highlight : "transparent"
                                Text {
                                    anchors.centerIn: parent
                                    text: "+"
                                    color: Theme.textMuted
                                    font { pixelSize: 13; bold: true }
                                }
                                MouseArea {
                                    id: addChatHover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        const pid = root.projectIdForSection(section)
                                        App.newChatSessionInProject(pid, section)
                                    }
                                }
                            }
                        }
                    }

                    delegate: Rectangle {
                        width: sessionsList.width
                        height: 52
                        color: modelData.id === (App.chatSessionId ?? "")
                               ? Theme.highlight : "transparent"

                        Rectangle {
                            width: 3; height: parent.height
                            color: modelData.id === (App.chatSessionId ?? "")
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
                                    return t.length > 0 ? t : "Nuevo chat"
                                }
                                color: modelData.id === (App.chatSessionId ?? "")
                                       ? Theme.accent : Theme.textPrimary
                                font { pixelSize: 12; bold: modelData.id === (App.chatSessionId ?? "") }
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: {
                                    const ms = modelData.updated ?? modelData.created ?? 0
                                    return ms > 0 ? new Date(ms).toLocaleDateString(Qt.locale(), "d MMM yyyy") : ""
                                }
                                color: Theme.textMuted
                                font.pixelSize: 10
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onClicked: (mouse) => {
                                if (mouse.button === Qt.RightButton) {
                                    ctxMenu.sessionId = modelData.id
                                    ctxMenu.sessionTitle = (modelData.title ?? "")
                                    ctxMenu.projects = App.chatProjects()
                                    ctxMenu.popup()
                                } else {
                                    App.switchChatSession(modelData.id)
                                }
                            }
                        }
                    }

                    Menu {
                        id: ctxMenu
                        property string sessionId: ""
                        property string sessionTitle: ""
                        property var projects: []

                        Menu {
                            id: moveMenu
                            title: "Mover a proyecto"
                            Instantiator {
                                model: ctxMenu.projects
                                delegate: MenuItem {
                                    text: modelData.projectName
                                    onTriggered: App.moveChatToProject(ctxMenu.sessionId,
                                                                       modelData.projectId,
                                                                       modelData.projectName)
                                }
                                onObjectAdded: (index, object) => moveMenu.insertItem(index, object)
                                onObjectRemoved: (index, object) => moveMenu.removeItem(object)
                            }
                        }
                        MenuItem {
                            text: "Renombrar chat"
                            onTriggered: if (ctxMenu.sessionId.length > 0) {
                                renameDialog.mode = "chat"
                                renameDialog.targetId = ctxMenu.sessionId
                                renameDialog.oldName = ctxMenu.sessionTitle
                                renameField.text = ctxMenu.sessionTitle
                                renameDialog.open()
                            }
                        }
                        MenuItem {
                            text: "Exportar a Markdown"
                            onTriggered: if (ctxMenu.sessionId.length > 0) App.exportChatSession(ctxMenu.sessionId, "md")
                        }
                        MenuItem {
                            text: "Exportar a JSON"
                            onTriggered: if (ctxMenu.sessionId.length > 0) App.exportChatSession(ctxMenu.sessionId, "json")
                        }
                        MenuSeparator {}
                        MenuItem {
                            text: "Borrar chat"
                            onTriggered: if (ctxMenu.sessionId.length > 0) App.deleteChatSession(ctxMenu.sessionId)
                        }
                    }

                    Menu {
                        id: projectCtxMenu
                        property string projectName: ""
                        MenuItem {
                            text: "Renombrar proyecto"
                            onTriggered: if (projectCtxMenu.projectName.length > 0) {
                                renameDialog.mode = "project"
                                renameDialog.oldName = projectCtxMenu.projectName
                                renameField.text = projectCtxMenu.projectName
                                renameDialog.open()
                            }
                        }
                        MenuSeparator {}
                        MenuItem {
                            text: "Borrar proyecto"
                            onTriggered: if (projectCtxMenu.projectName.length > 0) {
                                deleteProjectDialog.projectName = projectCtxMenu.projectName
                                deleteProjectDialog.open()
                            }
                        }
                    }
                }

            }

            // ── Nuevo proyecto popup ──────────────────────────────────────────
            Dialog {
                id: newProjectPopup
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
                        text: "Nuevo proyecto"
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
                            onClicked: { newProjectPopup.close(); newProjNameField.text = "" }
                        }
                        LcButton {
                            text: "Crear"
                            enabled: newProjNameField.text.trim().length > 0
                            onClicked: {
                                const name = newProjNameField.text.trim()
                                const pid  = "custom-" + name.toLowerCase().replace(/\s+/g, "-")
                                App.newChatSessionInProject(pid, name)
                                newProjectPopup.close()
                                newProjNameField.text = ""
                            }
                        }
                    }
                }

                contentItem: Item {
                    width: 380; height: 46
                    LcTextField {
                        id: newProjNameField
                        anchors.fill: parent
                        placeholderText: "Nombre del proyecto"
                        Keys.onReturnPressed: {
                            if (text.trim().length === 0) return
                            const name = text.trim()
                            const pid  = "custom-" + name.toLowerCase().replace(/\s+/g, "-")
                            App.newChatSessionInProject(pid, name)
                            newProjectPopup.close(); text = ""
                        }
                    }
                }

                onOpened: newProjNameField.forceActiveFocus()
            }

            // ── Renombrar chat / proyecto ─────────────────────────────────────
            Dialog {
                id: renameDialog
                property string mode: "chat"   // "chat" | "project"
                property string targetId: ""
                property string oldName: ""
                modal: true
                parent: Overlay.overlay
                x: Math.round((parent.width - width) / 2)
                y: Math.round((parent.height - height) / 2)
                width: 420
                height: 200
                closePolicy: Popup.CloseOnEscape

                function commit() {
                    const t = renameField.text.trim()
                    if (t.length === 0) return
                    if (renameDialog.mode === "project")
                        App.renameChatProject(renameDialog.oldName, t)
                    else
                        App.renameChatSession(renameDialog.targetId, t)
                    renameDialog.close()
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
                        text: renameDialog.mode === "project" ? "Renombrar proyecto" : "Renombrar chat"
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
                            onClicked: renameDialog.close()
                        }
                        LcButton {
                            text: "Guardar"
                            enabled: renameField.text.trim().length > 0
                            onClicked: renameDialog.commit()
                        }
                    }
                }

                contentItem: Item {
                    width: 380; height: 46
                    LcTextField {
                        id: renameField
                        anchors.fill: parent
                        placeholderText: "Nuevo nombre"
                        Keys.onReturnPressed: renameDialog.commit()
                    }
                }

                onOpened: { renameField.forceActiveFocus(); renameField.selectAll() }
            }

            // ── Borrar proyecto (confirmación) ────────────────────────────────
            Dialog {
                id: deleteProjectDialog
                property string projectName: ""
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
                            onClicked: deleteProjectDialog.close()
                        }
                        LcButton {
                            text: "Borrar"
                            danger: true
                            onClicked: {
                                App.deleteChatProject(deleteProjectDialog.projectName)
                                deleteProjectDialog.close()
                            }
                        }
                    }
                }

                contentItem: Item {
                    Text {
                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 20 }
                        text: "Se borrará el proyecto \"" + deleteProjectDialog.projectName
                              + "\" y todos sus chats. Esta acción no se puede deshacer."
                        color: Theme.textPrimary
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        // ── Handle de redimensión del panel de chats ─────────────────────────
        Item {
            id: chatsResizeHandle
            Layout.preferredWidth: 10
            Layout.fillHeight: true
            visible: App.serverRunning

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: chatsResizeMouse.pressed || chatsResizeHover.hovered ? 3 : 1
                height: parent.height
                color: chatsResizeMouse.pressed || chatsResizeHover.hovered
                       ? Theme.accent : Theme.divider
            }

            HoverHandler {
                id: chatsResizeHover
                cursorShape: Qt.SplitHCursor
            }

            MouseArea {
                id: chatsResizeMouse
                anchors.fill: parent
                cursorShape: Qt.SplitHCursor
                property real pressRootX: 0
                property int pressWidth: root.chatsPanelWidth
                onPressed: function(mouse) {
                    pressRootX = mapToItem(root, mouse.x, mouse.y).x
                    pressWidth = root.chatsPanelWidth
                }
                onPositionChanged: function(mouse) {
                    if (!pressed) return
                    const currentRootX = mapToItem(root, mouse.x, mouse.y).x
                    root.chatsPanelWidth = root.clampChatsWidth(pressWidth + currentRootX - pressRootX)
                }
                onReleased: {
                    root._cpRestored = true
                    root._savedChatsWidth = root.chatsPanelWidth
                    App.writeSetting("chatPanelWidth", root.chatsPanelWidth)
                }
            }
        }

        // ── Main chat area ───────────────────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Header
            Rectangle {
                Layout.fillWidth: true
                height: 48
                color: Theme.baseBg

                RowLayout {
                    anchors { fill: parent; leftMargin: 16; rightMargin: 12 }
                    spacing: 10
                    Text { text: "Chat"; color: Theme.textPrimary; font.pixelSize: 15; font.bold: true }
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        color: !App.serverRunning ? Theme.errorText
                             : App.serverReady     ? Theme.successText
                             :                       Theme.warnText
                        SequentialAnimation on opacity {
                            running: App.serverRunning && !App.serverReady
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.3; duration: 600 }
                            NumberAnimation { to: 1.0; duration: 600 }
                        }
                    }
                    Text {
                        text: {
                            const _lang = App.langV
                            if (!App.serverRunning) return App.l("chat.serverStopped")
                            const title = App.chatSessionTitle ?? ""
                            return title.length > 0 ? title : App.activeLaunchId
                        }
                        color: !App.serverRunning ? Theme.errorText
                             : !App.serverReady   ? Theme.warnText
                             :                      Theme.textSecondary
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    CheckBox {
                        id: chatThinkingCheck
                        visible: App.serverRunning && App.serverReady
                        text: "Pensar"
                        checkable: false
                        checked: App.chatThinkingEnabled
                        onClicked: {
                            thinkingRestartDialog.targetEnabled = !App.chatThinkingEnabled
                            thinkingRestartDialog.open()
                        }
                        contentItem: Text {
                            text: chatThinkingCheck.text
                            color: Theme.textPrimary
                            font.pixelSize: 12
                            leftPadding: chatThinkingCheck.indicator.width + 6
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    // Salida estructurada: fuerza JSON válido (GBNF) en la respuesta.
                    CheckBox {
                        id: jsonModeCheck
                        visible: App.serverRunning && App.serverReady
                        text: "JSON"
                        checked: false
                        onToggled: App.chatSetStructuredOutput(
                            checked ? "root   ::= object\n" +
                                      "value  ::= object | array | string | number | (\"true\" | \"false\" | \"null\") ws\n" +
                                      "object ::= \"{\" ws ( string \":\" ws value (\",\" ws string \":\" ws value)* )? \"}\" ws\n" +
                                      "array  ::= \"[\" ws ( value (\",\" ws value)* )? \"]\" ws\n" +
                                      "string ::= \"\\\"\" ( [^\"\\\\] | \"\\\\\" . )* \"\\\"\" ws\n" +
                                      "number ::= \"-\"? ([0-9] | [1-9] [0-9]*) (\".\" [0-9]+)? ([eE] [-+]? [0-9]+)? ws\n" +
                                      "ws ::= [ \\t\\n]*\n"
                                    : "", "")
                        contentItem: Text {
                            text: jsonModeCheck.text
                            color: Theme.textPrimary
                            font.pixelSize: 12
                            leftPadding: jsonModeCheck.indicator.width + 6
                            verticalAlignment: Text.AlignVCenter
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: "Fuerza al modelo a responder con JSON válido (GBNF grammar)."
                    }
                    // Persona Diseño: sesga al modelo a emitir artifacts visuales
                    // (bloques mermaid/svg) que se rinden inline.
                    CheckBox {
                        id: personaDesignerCheck
                        visible: App.serverRunning && App.serverReady
                        text: "Diseño"
                        checked: App.chatPersonaDesigner
                        onToggled: App.chatPersonaDesigner = checked
                        contentItem: Text {
                            text: personaDesignerCheck.text
                            color: Theme.textPrimary
                            font.pixelSize: 12
                            leftPadding: personaDesignerCheck.indicator.width + 6
                            verticalAlignment: Text.AlignVCenter
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: "Sesga al modelo a responder con diagramas (mermaid) y gráficos (svg) que se rinden inline."
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

            // Messages
            ListView {
                id: msgList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                flickDeceleration: 3000
                maximumFlickVelocity: 6000
                spacing: 4
                topMargin: 12
                bottomMargin: 12
                // Mantener delegados de arriba medidos: evita que contentHeight se
                // re-estime al subir (salto/traba) y que maxY quede inflado.
                cacheBuffer: 4000
                model: App.chatMessages
                ScrollBar.vertical: LcScrollBar { policy: ScrollBar.AsNeeded }

                // Scroll inicial al fondo una sola vez (contexto del ListView, no del
                // delegate: evita ReferenceError cuando un delegate reciclado corre el
                // Qt.callLater diferido).
                Component.onCompleted: { followBottom = true; Qt.callLater(scrollToBottom) }

                Text {
                    anchors.centerIn: parent
                    visible: App.chatMessages.length === 0 && !(App.serverRunning && !App.serverReady)
                    text: {
                        const _lang = App.langV
                        return App.serverRunning ? App.l("chat.startMessage") : App.l("chat.startServer")
                    }
                    color: Theme.textMuted
                    font.pixelSize: 14
                }

                // Popup no bloqueante: modelo cargando (centrado, sin frenar la UI)
                Rectangle {
                    anchors.centerIn: parent
                    visible: App.serverRunning && !App.serverReady
                    radius: 10
                    color: Theme.surfaceBg
                    border.color: Theme.borderColor
                    implicitWidth: loadingRow.implicitWidth + 32
                    implicitHeight: loadingRow.implicitHeight + 24
                    Row {
                        id: loadingRow
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
                            text: "Cargando modelo..."
                            color: Theme.textSecondary
                            font.pixelSize: 14
                        }
                    }
                }

                delegate: Item {
                    id: delegateRoot
                    width: msgList.width
                    height: bubbleRect.height + 8

                    readonly property bool isUser: modelData.role === "user"
                    readonly property bool isStreaming: index === App.chatStreamingIndex
                    readonly property string content: isStreaming
                        ? App.chatStreamingText
                        : (modelData.content ?? "")
                    readonly property bool isTyping: modelData.typing ?? false
                    readonly property string msgId: (modelData.id ?? (index + "-" + modelData.role))
                    readonly property string thinkContent: root.extractThinkContent(content)
                    readonly property bool hasThink: thinkContent.trim().length > 0
                    readonly property string visibleContentRaw: root.stripThinkBlocks(content)
                    readonly property bool thinkOpen: root.thinkExpanded[msgId] === true
                    readonly property string metaLine: root.formatMeta(modelData)
                    property bool editing: false

                    Rectangle {
                        id: bubbleRect
                        anchors {
                            top: parent.top
                            right: delegateRoot.isUser ? parent.right : undefined
                            rightMargin: delegateRoot.isUser ? 16 : undefined
                            left: delegateRoot.isUser ? undefined : parent.left
                            leftMargin: delegateRoot.isUser ? undefined : 16
                        }
                        width: Math.min(delegateRoot.width - 80, delegateRoot.width * 0.78)
                        height: Math.max(contentCol.implicitHeight + 22, 44)
                        radius: 10
                        color: delegateRoot.isUser ? Theme.chatUserBubble : Theme.chatAsstBubble
                        border.width: delegateRoot.isUser ? 0 : 1
                        border.color: Theme.borderColor

                        Column {
                            id: contentCol
                            anchors { top: parent.top; left: parent.left; right: parent.right; margins: 11 }
                            spacing: 8

                            Rectangle {
                                visible: delegateRoot.hasThink
                                width: parent.width
                                radius: 6
                                color: Theme.surfaceBg
                                border.color: Theme.borderColor
                                border.width: 1
                                implicitHeight: thinkHeader.height + (delegateRoot.thinkOpen ? thinkBody.implicitHeight + 8 : 0) + 8

                                Column {
                                    anchors { fill: parent; margins: 6 }
                                    spacing: 6

                                    Rectangle {
                                        id: thinkHeader
                                        width: parent.width
                                        height: 24
                                        color: "transparent"

                                        Row {
                                            anchors.fill: parent
                                            spacing: 6
                                            Text {
                                                text: delegateRoot.thinkOpen ? "▾" : "▸"
                                                color: Theme.textMuted
                                                font.pixelSize: 12
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                            Text {
                                                text: "Think"
                                                color: Theme.textMuted
                                                font.pixelSize: 12
                                                font.bold: true
                                                anchors.verticalCenter: parent.verticalCenter
                                            }
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                const cur = root.thinkExpanded
                                                cur[delegateRoot.msgId] = !delegateRoot.thinkOpen
                                                root.thinkExpanded = cur
                                            }
                                        }
                                    }

                                    TextEdit {
                                        id: thinkBody
                                        visible: delegateRoot.thinkOpen
                                        width: parent.width
                                        text: delegateRoot.thinkContent
                                        color: Theme.textMuted
                                        font.family: Theme.codeFont
                                        font.pixelSize: 12
                                        wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                                        readOnly: true
                                        selectByMouse: true
                                    }
                                }
                            }

                            // Cuerpo del mensaje. Se parte en segmentos texto /
                            // mermaid; los diagramas se rinden a PNG vía sidecar.
                            // Mientras tipea (o sin sidecar) se muestra todo texto.
                            Column {
                                id: msgBody
                                visible: !delegateRoot.editing
                                width: parent.width
                                spacing: 8

                                readonly property string bodyText: {
                                    if (delegateRoot.isTyping && delegateRoot.content.length === 0)
                                        return "⏳ Procesando..."
                                    const base = delegateRoot.visibleContentRaw.length > 0
                                        ? delegateRoot.visibleContentRaw
                                        : delegateRoot.content
                                    if (delegateRoot.isTyping)
                                        return base + "▌"
                                    return base
                                }
                                // svg se rasteriza sin sidecar (QSvgRenderer), así
                                // que parseamos aunque mmdc no esté; los bloques
                                // mermaid sin sidecar degradan a ⚠ por su cuenta.
                                readonly property bool plainOnly: delegateRoot.isTyping || !App.mermaidEnabled
                                readonly property var segs: plainOnly
                                    ? [{ "type": "text", "text": bodyText }]
                                    : Mermaid.segments(bodyText)

                                Repeater {
                                    model: msgBody.segs
                                    delegate: Item {
                                        width: msgBody.width
                                        readonly property bool isMermaid: modelData.type === "mermaid"
                                        readonly property bool isSvg: modelData.type === "svg"
                                        readonly property bool isBlock: isMermaid || isSvg
                                        readonly property string segSrc: modelData.text
                                        height: isBlock ? mermaidWrap.height : segText.height

                                        TextEdit {
                                            id: segText
                                            visible: !isBlock
                                            width: parent.width
                                            text: isBlock ? "" : modelData.text
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

                                        // Bloque mermaid (async) o svg (sincrónico):
                                        // PNG renderizado (click = preview) o
                                        // placeholder mientras rinde / si falla.
                                        Item {
                                            id: mermaidWrap
                                            visible: isBlock
                                            width: parent.width
                                            height: isBlock ? (diagImg.status === Image.Ready && diagImg.source != ""
                                                                  ? diagImg.paintedHeight + 8
                                                                  : 40)
                                                            : 0

                                            readonly property string h: isBlock ? Mermaid.sourceHash(segSrc) : ""
                                            readonly property string imgUrl: isBlock ? (root.mermaidPaths[h] ?? "") : ""
                                            readonly property string errTxt: isBlock ? (root.mermaidErrors[h] ?? "") : ""

                                            onHChanged: tryRender()
                                            Component.onCompleted: tryRender()
                                            function tryRender() {
                                                if (!isBlock || imgUrl !== "") return
                                                if (isSvg) {
                                                    // QSvgRenderer es sincrónico: path directo.
                                                    const p = Mermaid.renderSvg(segSrc)
                                                    if (p !== "") {
                                                        const m = root.mermaidPaths; m[h] = root.mermaidUrl(p); root.mermaidPaths = m
                                                    } else {
                                                        const e = root.mermaidErrors; e[h] = "SVG inválido"; root.mermaidErrors = e
                                                    }
                                                    return
                                                }
                                                const c = Mermaid.cachedPath(segSrc)
                                                if (c !== "") {
                                                    const m = root.mermaidPaths; m[h] = root.mermaidUrl(c); root.mermaidPaths = m
                                                } else if (errTxt === "") {
                                                    Mermaid.requestRender(segSrc)
                                                }
                                            }

                                            Image {
                                                id: diagImg
                                                source: mermaidWrap.imgUrl
                                                visible: mermaidWrap.imgUrl !== ""
                                                width: parent.width
                                                fillMode: Image.PreserveAspectFit
                                                horizontalAlignment: Image.AlignLeft
                                                smooth: true
                                                MouseArea {
                                                    anchors.fill: parent
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: root.openMermaidPreview(mermaidWrap.imgUrl, mermaidWrap.segSrc, isSvg)
                                                }
                                            }
                                            Text {
                                                visible: mermaidWrap.imgUrl === ""
                                                width: parent.width
                                                text: mermaidWrap.errTxt !== ""
                                                    ? "⚠ Diagrama: " + mermaidWrap.errTxt
                                                    : "⏳ Renderizando diagrama…"
                                                color: Theme.textMuted
                                                font.pixelSize: 12
                                                font.italic: true
                                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                            }
                                        }
                                    }
                                }
                            }

                            // Edición inline del mensaje (rebobina + reescribe).
                            Column {
                                visible: delegateRoot.editing
                                width: parent.width
                                spacing: 6
                                TextArea {
                                    id: chatEditArea
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
                                        color: chatSaveMA.containsMouse ? Theme.textPrimary : Theme.accent
                                        font.pixelSize: 11; font.bold: true
                                        MouseArea {
                                            id: chatSaveMA
                                            anchors.fill: parent; hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                App.editChatMessage(index, chatEditArea.text)
                                                delegateRoot.editing = false
                                            }
                                        }
                                    }
                                    Text {
                                        text: "Cancelar"
                                        color: chatCancelMA.containsMouse ? Theme.textPrimary : Theme.textMuted
                                        font.pixelSize: 11
                                        MouseArea {
                                            id: chatCancelMA
                                            anchors.fill: parent; hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: delegateRoot.editing = false
                                        }
                                    }
                                }
                            }
                            Text {
                                visible: metaLine.length > 0
                                width: parent.width
                                text: metaLine
                                color: Theme.textMuted
                                font.pixelSize: 10
                                horizontalAlignment: Text.AlignRight
                                elide: Text.ElideRight
                            }
                            // Copiar mensaje al portapapeles.
                            Row {
                                width: parent.width
                                layoutDirection: Qt.RightToLeft
                                visible: delegateRoot.content.length > 0 && !delegateRoot.isTyping
                                Text {
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
                                            chatCopyResetTimer.restart()
                                        }
                                    }
                                }
                                // Rebobinar: descarta este turno y los siguientes.
                                // Solo en mensajes del usuario y sin generación en curso.
                                Text {
                                    visible: delegateRoot.isUser && !App.chatGenerating && !delegateRoot.editing
                                    text: "↩ Rebobinar"
                                    color: rewindMA.containsMouse ? Theme.textPrimary : Theme.textMuted
                                    font.pixelSize: 10
                                    rightPadding: 12
                                    MouseArea {
                                        id: rewindMA
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: App.rollbackChatToMessage(index)
                                    }
                                }
                                // Editar: reescribe el texto (de IA o usuario) y descarta lo posterior.
                                Text {
                                    visible: !delegateRoot.isTyping && !App.chatGenerating && !delegateRoot.editing
                                    text: "✎ Editar"
                                    color: editMA.containsMouse ? Theme.textPrimary : Theme.textMuted
                                    font.pixelSize: 10
                                    rightPadding: 12
                                    MouseArea {
                                        id: editMA
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            chatEditArea.text = delegateRoot.visibleContentRaw.length > 0
                                                ? delegateRoot.visibleContentRaw : delegateRoot.content
                                            delegateRoot.editing = true
                                        }
                                    }
                                }
                            }
                        }

                        property bool justCopied: false
                        Timer { id: chatCopyResetTimer; interval: 1500; onTriggered: bubbleRect.justCopied = false }
                    }
                }

                // Auto-scroll por contentY directo (no positionViewAtEnd) para
                // evitar oscilación con delegates de altura variable.
                property bool followBottom: true

                function minContentY() {
                    return isFinite(originY) ? originY : 0
                }

                function maxContentY() {
                    return minContentY() + Math.max(0, contentHeight - height)
                }

                function scrollToBottom() {
                    // Solo BAJAR, nunca subir: durante el streaming contentHeight
                    // oscila (re-estimación de delegados altos fuera de vista). Si
                    // siguiéramos esa oscilación hacia arriba, la vista saltaría.
                    var maxY = maxContentY()
                    if (contentY < maxY)
                        contentY = maxY
                }

                // followBottom se actualiza en vivo con el flick nativo: si el usuario
                // scrollea hacia arriba durante streaming, dejamos de auto-bajar.
                onContentYChanged: {
                    // Clamp duro contra overscroll bajo el último mensaje. Solo en
                    // idle: durante el streaming el contentHeight oscila y el clamp
                    // pelearía con el auto-follow (scroll brusco).
                    var minY = minContentY()
                    var maxY = maxContentY()
                    if (!App.chatGenerating && contentY < minY) { contentY = minY; return }
                    if (!App.chatGenerating && contentY > maxY) { contentY = maxY; return }
                    followBottom = (contentY >= maxY - 2)
                }
                onMovementEnded: followBottom = atYEnd
                onContentHeightChanged: if (followBottom) chatBottomTimer.restart()
                onCountChanged: if (followBottom) chatBottomTimer.restart()
                onModelChanged: { followBottom = true; chatBottomTimer.restart() }

                Timer {
                    id: chatBottomTimer
                    interval: 16
                    onTriggered: if (msgList.followBottom) msgList.scrollToBottom()
                }

                // Scroll 1:1 para touchpad y por líneas para rueda. El movimiento
                // directo evita que animaciones sucesivas peleen con el layout de
                // mensajes de altura variable.
                WheelHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: function(ev) {
                        var minY = msgList.minContentY()
                        var maxY = msgList.maxContentY()
                        var pixelY = ev.pixelDelta.y
                        var delta = pixelY !== 0 ? pixelY : (ev.angleDelta.y / 120) * 72
                        msgList.contentY = Math.max(minY, Math.min(maxY, msgList.contentY - delta))
                        ev.accepted = true
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

            // Input bar
            Rectangle {
                Layout.fillWidth: true
                color: Theme.baseBg
                implicitHeight: inputCol.implicitHeight + 20

                ColumnLayout {
                    id: inputCol
                    anchors { fill: parent; leftMargin: 12; rightMargin: 12; topMargin: 10; bottomMargin: 10 }
                    spacing: 6

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        visible: App.chatQueuedCount > 0
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "Mensajes en cola"; color: Theme.textSecondary; font { pixelSize: 11; bold: true } }
                            Item { Layout.fillWidth: true }
                            LcButton { text: "Vaciar cola"; danger: true; onClicked: App.clearChatQueue() }
                        }
                        ScrollView {
                            Layout.fillWidth: true
                            // Evita que ScrollView colapse antes de crear los
                            // delegates de la cola.
                            Layout.preferredHeight: Math.min(220, Math.max(52, App.chatQueuedCount * 56))
                            clip: true
                            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                            Column {
                                id: chatQueueRows
                                width: parent.width
                                spacing: 4
                                Repeater {
                                    model: App.chatQueuedMessages
                                    Rectangle {
                                        required property int index
                                        required property string modelData
                                        width: chatQueueRows.width
                                        height: 52
                                        radius: 6; color: Theme.inputBg; border.color: Theme.borderColor
                                        RowLayout {
                                            anchors.fill: parent; anchors.margins: 6; spacing: 6
                                            Text { text: (index + 1) + ")"; color: Theme.textMuted; font.bold: true }
                                            Text { Layout.fillWidth: true; text: modelData; color: Theme.textPrimary
                                                wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight }
                                            LcButton { text: "Previsualizar"; secondary: true
                                                onClicked: { chatQueueDialog.editIndex = -1; chatQueueDialog.open() } }
                                            LcButton { text: "Editar"; secondary: true
                                                onClicked: { chatQueueDialog.editIndex = index; chatQueueDialog.open() } }
                                            LcButton { text: "Eliminar"; danger: true; onClicked: App.removeChatQueuedMessage(index) }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    LcButton {
                        text: "Sampling"
                        secondary: true
                        visible: App.serverRunning && App.serverReady
                        onClicked: samplingPopup.open()
                        ToolTip.visible: hovered
                        ToolTip.text: "Temperatura, top-p y top-k de esta sesión"
                    }

                    // Chips de adjuntos
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        visible: root.chatAttachments.length > 0
                        Repeater {
                            model: root.chatAttachments
                            delegate: Rectangle {
                                radius: 6
                                color: Theme.inputBg
                                border.color: Theme.borderColor
                                height: 24
                                width: chipRow.implicitWidth + 14
                                Row {
                                    id: chipRow
                                    anchors.centerIn: parent
                                    spacing: 6
                                    Text { text: "📎 " + root.fileName(modelData); color: Theme.textSecondary; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                                    Text {
                                        text: "✕"; color: Theme.textMuted; font.pixelSize: 11
                                        anchors.verticalCenter: parent.verticalCenter
                                        MouseArea {
                                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                const a = root.chatAttachments.slice()
                                                a.splice(index, 1)
                                                root.chatAttachments = a
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        LcButton {
                            text: "📎"
                            secondary: true
                            implicitWidth: 36
                            enabled: App.serverRunning && App.serverReady && !App.chatGenerating
                            onClicked: {
                                const picked = App.pickChatAttachments()
                                if (picked && picked.length > 0)
                                    root.chatAttachments = root.chatAttachments.concat(picked)
                            }
                        }

                        TextField {
                            id: inputField
                            Layout.fillWidth: true
                            placeholderText: {
                                const _lang = App.langV
                                return App.chatGenerating
                                    ? ("Enter encola · Shift+Enter interrumpe"
                                       + (App.chatQueuedCount > 0 ? "  ·  " + App.chatQueuedCount + " en cola" : ""))
                                    : App.l("chat.placeholder")
                            }
                            // Habilitado también mientras genera, para poder encolar/dirigir.
                            enabled: App.serverRunning && App.serverReady
                            color: Theme.textPrimary
                            placeholderTextColor: Theme.textMuted
                            font.pixelSize: 13
                            leftPadding: 12; rightPadding: 12
                            verticalAlignment: TextInput.AlignVCenter
                            background: Rectangle {
                                color: Theme.inputBg; radius: 8
                                border.width: inputField.activeFocus ? 1 : 0
                                border.color: Theme.inputBorderFocus
                            }
                            // Enter = enviar/encolar. Shift+Enter = interrumpir (campo de una línea).
                            Keys.onReturnPressed: (event) => {
                                event.accepted = true
                                if (event.modifiers & Qt.ShiftModifier) root.doSteer()
                                else root.enterPressed()
                            }
                            // Ctrl+V: si hay imagen en el portapapeles, adjuntarla.
                            Keys.onPressed: (event) => {
                                if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_V) {
                                    const p = App.pasteClipboardImage()
                                    if (p && p.length > 0) {
                                        root.chatAttachments = root.chatAttachments.concat([p])
                                        event.accepted = true
                                    }
                                }
                            }
                        }

                        // Idle: enviar.
                        LcButton {
                            visible: !App.chatGenerating
                            text: (App.langV, App.l("chat.send"))
                            enabled: App.serverRunning && App.serverReady
                                && (inputField.text.trim().length > 0 || root.chatAttachments.length > 0)
                            onClicked: root.doSend()
                        }
                        // Generando + hay texto: encolar / interrumpir.
                        LcButton {
                            visible: App.chatGenerating && inputField.text.trim().length > 0
                            text: "Encolar" + (App.chatQueuedCount > 0 ? " (" + App.chatQueuedCount + ")" : "")
                            onClicked: root.doQueue()
                        }
                        LcButton {
                            visible: App.chatGenerating && inputField.text.trim().length > 0
                            text: "Interrumpir"
                            danger: true
                            onClicked: root.doSteer()
                        }
                        // PARAR: cortar sin enviar.
                        LcButton {
                            visible: App.chatGenerating
                            text: (App.langV, App.l("chat.stop"))
                            danger: true
                            onClicked: App.stopChatGeneration()
                        }
                    }
                }
            }
        }
    }

    LcDialog {
        id: chatQueueDialog
        modal: true
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: Math.min(680, parent.width - 40)
        height: Math.min(520, parent.height - 40)
        title: "Mensajes en cola (" + App.chatQueuedCount + ")"
        property int editIndex: -1
        footer: RowLayout {
            width: parent.width
            LcButton { text: "Vaciar cola"; danger: true; onClicked: App.clearChatQueue() }
            Item { Layout.fillWidth: true }
            LcButton { text: "Cerrar"; secondary: true; onClicked: chatQueueDialog.close() }
        }
        contentItem: ListView {
            id: chatQueueList
            clip: true
            spacing: 8
            model: App.chatQueuedMessages
            delegate: Rectangle {
                required property int index
                required property string modelData
                width: chatQueueList.width
                height: queueEditor.visible ? Math.max(118, queueEditor.contentHeight + 58) : preview.implicitHeight + 42
                radius: 7
                color: Theme.inputBg
                border.color: Theme.borderColor
                property bool editing: chatQueueDialog.editIndex === index
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
                        onClicked: { if (parent.parent.editing) { if (App.updateChatQueuedMessage(index, queueEditor.text)) chatQueueDialog.editIndex = -1 } else chatQueueDialog.editIndex = index } }
                    LcButton { text: "Eliminar"; danger: true; onClicked: App.removeChatQueuedMessage(index) }
                }
            }
            Text { anchors.centerIn: parent; visible: App.chatQueuedCount === 0
                text: "No hay mensajes en cola."; color: Theme.textMuted }
        }
        onOpened: if (App.chatQueuedCount === 0) close()
    }
}
