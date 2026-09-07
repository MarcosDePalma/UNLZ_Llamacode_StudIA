import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0

Dialog {
    id: dlg
    property string projectDir: ""
    property string scope: "global"   // "global" | "project"
    property var commandsModel: []

    modal: true
    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: 760
    height: 620
    closePolicy: Popup.CloseOnEscape

    background: Rectangle {
        color: Theme.popupBg; radius: 12
        border.color: Theme.popupBorderColor; border.width: 1
    }
    Overlay.modal: Rectangle { color: Theme.overlayColor }

    function reloadAll() {
        configArea.text = App.readOpencodeConfig(scope, projectDir)
        configPathText.text = App.opencodeConfigPath(scope, projectDir)
        mcpRepeater.model = App.listMcpServers(scope, projectDir)
        commandsModel = App.listOpencodeCommands(scope, projectDir)
    }

    onOpened: reloadAll()
    onScopeChanged: if (opened) reloadAll()

    header: Rectangle {
        color: Theme.popupHeaderBg; height: 56; radius: 12
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 12; color: Theme.popupHeaderBg }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1;  color: Theme.popupHeaderBorder }
        RowLayout {
            anchors { fill: parent; leftMargin: 20; rightMargin: 16 }
            Text {
                text: "Configuración opencode"
                font { pixelSize: 15; bold: true }
                color: Theme.textPrimary
                Layout.fillWidth: true
            }
            // Scope toggle
            Repeater {
                model: [ { id: "global", label: "Global" }, { id: "project", label: "Proyecto" } ]
                Rectangle {
                    Layout.preferredWidth: scopeLabel.implicitWidth + 22
                    height: 28; radius: 6
                    color: dlg.scope === modelData.id ? Theme.highlight : "transparent"
                    border.color: dlg.scope === modelData.id ? Theme.accent : Theme.borderColor
                    border.width: 1
                    opacity: (modelData.id === "project" && dlg.projectDir.length === 0) ? 0.4 : 1.0
                    Text {
                        id: scopeLabel
                        anchors.centerIn: parent
                        text: modelData.label
                        color: dlg.scope === modelData.id ? Theme.accent : Theme.textMuted
                        font { pixelSize: 12; bold: dlg.scope === modelData.id }
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: !(modelData.id === "project" && dlg.projectDir.length === 0)
                        cursorShape: Qt.PointingHandCursor
                        onClicked: dlg.scope = modelData.id
                    }
                }
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 10

        Text {
            id: configPathText
            Layout.fillWidth: true
            Layout.leftMargin: 4
            color: Theme.textMuted; font { pixelSize: 11; family: "Consolas" }
            elide: Text.ElideMiddle
        }
        Text {
            visible: dlg.scope === "project" && dlg.projectDir.length === 0
            Layout.fillWidth: true
            text: "No hay proyecto activo. Iniciá el agente en una carpeta para editar config de proyecto."
            color: Theme.warnText; font.pixelSize: 11; wrapMode: Text.WordWrap
        }

        TabBar {
            id: tabs
            Layout.fillWidth: true
            background: Rectangle { color: "transparent" }
            Repeater {
                model: ["opencode.json", "MCP", "Skills / comandos"]
                TabButton {
                    contentItem: Text {
                        text: modelData
                        color: tabs.currentIndex === index ? Theme.accent : Theme.textMuted
                        font { pixelSize: 12; bold: tabs.currentIndex === index }
                        horizontalAlignment: Text.AlignHCenter
                    }
                    background: Rectangle {
                        color: tabs.currentIndex === index ? Theme.highlight : "transparent"
                        radius: 6
                    }
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            // ── Tab 1: raw JSON ───────────────────────────────────────────
            ColumnLayout {
                spacing: 8
                ScrollView {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true
                    ScrollBar.vertical: LcScrollBar {}
                    TextArea {
                        id: configArea
                        wrapMode: TextArea.NoWrap
                        color: Theme.textPrimary
                        font.family: "Consolas"
                        background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    LcButton { text: "Recargar"; secondary: true; onClicked: dlg.reloadAll() }
                    LcButton {
                        text: "Guardar config"
                        onClicked: if (App.writeOpencodeConfig(dlg.scope, dlg.projectDir, configArea.text)) dlg.reloadAll()
                    }
                }
            }

            // ── Tab 2: MCP ────────────────────────────────────────────────
            ColumnLayout {
                spacing: 8
                ScrollView {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true
                    ScrollBar.vertical: LcScrollBar {}
                    ColumnLayout {
                        width: parent.parent.width
                        spacing: 6
                        Repeater {
                            id: mcpRepeater
                            delegate: Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: 56
                                color: Theme.inputBg; radius: 8
                                border.color: Theme.borderColor
                                RowLayout {
                                    anchors { fill: parent; leftMargin: 12; rightMargin: 10 }
                                    spacing: 10
                                    CheckBox {
                                        checked: modelData.enabled
                                        onToggled: App.toggleMcpServer(dlg.scope, dlg.projectDir, modelData.name, checked)
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true; spacing: 1
                                        Text { text: modelData.name; color: Theme.textPrimary; font { pixelSize: 13; bold: true } }
                                        Text {
                                            text: (modelData.type === "remote" ? (modelData.url ?? "") : (modelData.command ?? ""))
                                            color: Theme.textMuted; font { pixelSize: 11; family: "Consolas" }
                                            Layout.fillWidth: true; elide: Text.ElideRight
                                        }
                                    }
                                    Rectangle {
                                        height: 20; radius: 4; color: Theme.highlight
                                        implicitWidth: tText.implicitWidth + 12
                                        Text { id: tText; anchors.centerIn: parent; text: modelData.type; color: Theme.accent; font.pixelSize: 10 }
                                    }
                                    LcButton {
                                        text: "✎"; secondary: true; implicitWidth: 30
                                        onClicked: {
                                            mcpName.text = modelData.name
                                            mcpType.currentIndex = modelData.type === "remote" ? 1 : 0
                                            mcpTarget.text = modelData.type === "remote" ? (modelData.url ?? "") : (modelData.command ?? "")
                                            mcpEnabled.checked = modelData.enabled
                                            mcpEditor.visible = true
                                        }
                                    }
                                    LcButton {
                                        text: "🗑"; danger: true; implicitWidth: 30
                                        onClicked: { App.removeMcpServer(dlg.scope, dlg.projectDir, modelData.name); dlg.reloadAll() }
                                    }
                                }
                            }
                        }
                        Text {
                            visible: mcpRepeater.count === 0
                            text: "Sin servers MCP. Agregá uno abajo."
                            color: Theme.textMuted; font.pixelSize: 11
                        }
                    }
                }

                // MCP editor row
                Rectangle {
                    id: mcpEditor
                    Layout.fillWidth: true
                    implicitHeight: 96
                    visible: false
                    color: Theme.surfaceBg; radius: 8; border.color: Theme.borderColor
                    ColumnLayout {
                        anchors { fill: parent; margins: 10 }
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            LcTextField { id: mcpName; placeholderText: "nombre"; Layout.preferredWidth: 160 }
                            LcComboBox {
                                id: mcpType
                                model: ["local", "remote"]
                                Layout.preferredWidth: 110
                                background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                                contentItem: Text { text: mcpType.displayText; color: Theme.textPrimary; font.pixelSize: 12; leftPadding: 8; verticalAlignment: Text.AlignVCenter }
                            }
                            CheckBox { id: mcpEnabled; text: "enabled"; checked: true }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            LcTextField {
                                id: mcpTarget
                                Layout.fillWidth: true
                                placeholderText: mcpType.currentIndex === 1 ? "https://url-del-server" : "npx -y @scope/server arg1"
                            }
                            LcButton { text: "Cancelar"; secondary: true; onClicked: mcpEditor.visible = false }
                            LcButton {
                                text: "Guardar server"
                                enabled: mcpName.text.trim().length > 0 && mcpTarget.text.trim().length > 0
                                onClicked: {
                                    const def = {
                                        "type": mcpType.currentIndex === 1 ? "remote" : "local",
                                        "enabled": mcpEnabled.checked
                                    }
                                    if (mcpType.currentIndex === 1) def.url = mcpTarget.text.trim()
                                    else def.command = mcpTarget.text.trim()
                                    if (App.setMcpServer(dlg.scope, dlg.projectDir, mcpName.text.trim(), def)) {
                                        mcpEditor.visible = false
                                        dlg.reloadAll()
                                    }
                                }
                            }
                        }
                    }
                }
                LcButton {
                    text: "+ Nuevo server MCP"
                    secondary: true
                    onClicked: {
                        mcpName.text = ""; mcpTarget.text = ""; mcpType.currentIndex = 0; mcpEnabled.checked = true
                        mcpEditor.visible = true
                    }
                }
            }

            // ── Tab 3: Skills / comandos ──────────────────────────────────
            RowLayout {
                spacing: 10
                // command list
                Rectangle {
                    Layout.preferredWidth: 200; Layout.fillHeight: true
                    color: Theme.inputBg; radius: 8; border.color: Theme.borderColor
                    ColumnLayout {
                        anchors { fill: parent; margins: 6 }
                        spacing: 4
                        ListView {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            clip: true
                            model: dlg.commandsModel
                            ScrollBar.vertical: LcScrollBar {}
                            delegate: Rectangle {
                                width: ListView.view.width; height: 30; radius: 4
                                color: cmdName.text === modelData.name ? Theme.highlight : "transparent"
                                Text {
                                    anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
                                    text: "/" + modelData.name; color: Theme.textPrimary; font.pixelSize: 12
                                }
                                MouseArea {
                                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        cmdName.text = modelData.name
                                        cmdBody.text = App.readOpencodeCommand(dlg.scope, dlg.projectDir, modelData.name)
                                    }
                                }
                            }
                        }
                        LcButton {
                            text: "+ Nuevo comando"; secondary: true; Layout.fillWidth: true
                            onClicked: { cmdName.text = ""; cmdBody.text = "---\ndescription: \n---\n\n" }
                        }
                    }
                }
                // editor
                ColumnLayout {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    spacing: 6
                    LcTextField { id: cmdName; Layout.fillWidth: true; placeholderText: "nombre-del-comando (sin .md)" }
                    ScrollView {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true
                        ScrollBar.vertical: LcScrollBar {}
                        TextArea {
                            id: cmdBody
                            wrapMode: TextArea.Wrap
                            color: Theme.textPrimary; font.family: "Consolas"
                            placeholderText: "Contenido del comando (markdown). El cuerpo es el prompt; $ARGUMENTS para args."
                            background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Item { Layout.fillWidth: true }
                        LcButton {
                            text: "Borrar"; danger: true
                            enabled: cmdName.text.trim().length > 0
                            onClicked: { App.deleteOpencodeCommand(dlg.scope, dlg.projectDir, cmdName.text.trim()); cmdName.text = ""; cmdBody.text = ""; dlg.reloadAll() }
                        }
                        LcButton {
                            text: "Guardar comando"
                            enabled: cmdName.text.trim().length > 0
                            onClicked: if (App.writeOpencodeCommand(dlg.scope, dlg.projectDir, cmdName.text.trim(), cmdBody.text)) dlg.reloadAll()
                        }
                    }
                }
            }
        }
    }

    footer: Rectangle {
        color: Theme.popupHeaderBg; height: 50; radius: 12
        Rectangle { anchors.top: parent.top; width: parent.width; height: 12; color: Theme.popupHeaderBg }
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1;  color: Theme.popupHeaderBorder }
        Row {
            anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
            spacing: 10
            LcButton { text: "Cerrar"; secondary: true; onClicked: dlg.close() }
        }
    }
}
