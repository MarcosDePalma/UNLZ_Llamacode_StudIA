import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0

Item {
    id: root

    property string selectedReportId: ""
    property string selectedReportText: ""
    property string researchMode: "auto"
    property var workspaceOptions: [{ projectId: "", projectName: "Sin workspace" }]
    readonly property bool researchBackendReady:
        App.serverRunning && App.serverReady && App.agentRunning && !App.agentStarting

    function modeLabel(mode) {
        if (mode === "product") return "Product"
        if (mode === "compare") return "Compare"
        if (mode === "howto") return "How-to"
        if (mode === "factcheck") return "Fact-check"
        return "Auto"
    }

    function start() {
        const topic = topicInput.text.trim()
        if (topic.length === 0) return
        const workspace = workspaceCombo.currentIndex >= 0
            ? workspaceOptions[workspaceCombo.currentIndex]
            : workspaceOptions[0]
        App.startResearch(topic, researchMode, depthCombo.currentValue ?? 6,
                          workspace.projectId ?? "", workspace.projectName ?? "")
        topicInput.text = ""
    }

    function selectReport(id) {
        selectedReportId = id ?? ""
        selectedReportText = selectedReportId.length > 0 ? App.readResearchReport(selectedReportId) : ""
    }

    function refreshWorkspaces() {
        const items = [{ projectId: "", projectName: "Sin workspace" }]
        const projects = App.chatProjects()
        for (let i = 0; i < projects.length; ++i) items.push(projects[i])
        workspaceOptions = items
    }

    Component.onCompleted: {
        refreshWorkspaces()
    }

    Connections {
        target: App
        function onResearchFinished(id, title) {
            App.refreshResearchReports()
            root.selectReport(id)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            Layout.fillWidth: true
            title: "Deep Research"
            subtitle: App.researchRunning
                ? App.researchStatus
                : "Investigación web, síntesis local y reportes persistentes"
            action2Label: selectedReportId.length > 0 ? "Abrir .md" : ""
            onAction2Clicked: App.openResearchReport(selectedReportId)
            actionLabel: workspaceCombo.currentIndex > 0 ? "Exportar workspace" : ""
            onActionClicked: {
                const workspace = workspaceOptions[workspaceCombo.currentIndex]
                App.exportWorkspace(workspace.projectId ?? "", workspace.projectName ?? "")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                Layout.preferredWidth: 360
                Layout.fillHeight: true
                color: Theme.surfaceBg

                ColumnLayout {
                    anchors { fill: parent; margins: 16 }
                    spacing: 14

                    Text {
                        text: "CONSULTA"
                        color: Theme.textSecondary
                        font.pixelSize: 10
                        font.bold: true
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 128
                        radius: 8
                        color: Theme.inputBg
                        border.color: topicInput.activeFocus ? Theme.inputBorderFocus : Theme.borderColor
                        clip: true

                        ScrollView {
                            anchors.fill: parent
                            anchors.margins: 2
                            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                            TextArea {
                                id: topicInput
                                placeholderText: "p.ej. compará frameworks locales para agentes con tool-calling, privacidad, costo y madurez"
                                color: Theme.textPrimary
                                placeholderTextColor: Theme.textMuted
                                font.pixelSize: 13
                                wrapMode: TextArea.WrapAtWordBoundaryOrAnywhere
                                background: null
                                padding: 10
                                selectByMouse: true
                                enabled: !App.researchRunning
                                Keys.onPressed: (event) => {
                                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                                        && (event.modifiers & Qt.ControlModifier)) {
                                        event.accepted = true
                                        root.start()
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        text: "WORKSPACE"
                        color: Theme.textSecondary
                        font.pixelSize: 10
                        font.bold: true
                    }

                    LcComboBox {
                        id: workspaceCombo
                        Layout.fillWidth: true
                        enabled: !App.researchRunning
                        model: root.workspaceOptions
                        textRole: "projectName"
                        valueRole: "projectId"
                        background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                        contentItem: Text {
                            text: workspaceCombo.displayText
                            color: Theme.textPrimary
                            font.pixelSize: 12
                            leftPadding: 8
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                    }

                    Text {
                        text: "MODO"
                        color: Theme.textSecondary
                        font.pixelSize: 10
                        font.bold: true
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: 6

                        Repeater {
                            model: [
                                { key: "auto", label: "Auto" },
                                { key: "product", label: "Product" },
                                { key: "compare", label: "Compare" },
                                { key: "howto", label: "How-to" },
                                { key: "factcheck", label: "Fact-check" }
                            ]
                            delegate: Rectangle {
                                height: 30
                                radius: 7
                                color: root.researchMode === modelData.key ? Theme.highlight : "transparent"
                                border.color: root.researchMode === modelData.key ? Theme.accent : Theme.borderColor
                                implicitWidth: chipText.implicitWidth + 18
                                Text {
                                    id: chipText
                                    anchors.centerIn: parent
                                    text: modelData.label
                                    color: root.researchMode === modelData.key ? Theme.accent : Theme.textSecondary
                                    font { pixelSize: 12; bold: root.researchMode === modelData.key }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    enabled: !App.researchRunning
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.researchMode = modelData.key
                                }
                            }
                        }
                    }

                    CheckBox {
                        id: researchThinkingCheck
                        text: "Thinking"
                        checked: App.thinkingEnabled
                        enabled: !App.researchRunning
                        onToggled: App.thinkingEnabled = checked
                        contentItem: Text {
                            text: researchThinkingCheck.text
                            color: Theme.textPrimary
                            font.pixelSize: 12
                            leftPadding: researchThinkingCheck.indicator.width + 6
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Text {
                            text: "PROFUNDIDAD"
                            color: Theme.textSecondary
                            font.pixelSize: 10
                            font.bold: true
                            Layout.fillWidth: true
                        }

                        LcComboBox {
                            id: depthCombo
                            Layout.preferredWidth: 120
                            enabled: !App.researchRunning
                            model: [
                                { label: "Auto", value: 10 },
                                { label: "Light", value: 6 },
                                { label: "Deep", value: 12 },
                                { label: "Max", value: 16 }
                            ]
                            textRole: "label"
                            valueRole: "value"
                            background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                            contentItem: Text {
                                text: depthCombo.displayText
                                color: Theme.textPrimary
                                font.pixelSize: 12
                                leftPadding: 8
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        visible: App.researchRunning

                        ProgressBar {
                            Layout.fillWidth: true
                            from: 0
                            to: 100
                            value: App.researchProgress
                        }
                        Text {
                            Layout.fillWidth: true
                            text: App.researchProgress + "% · " + App.researchStatus
                            color: Theme.textMuted
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }

                    Item { Layout.fillHeight: true }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        LcButton {
                            Layout.fillWidth: true
                            text: App.researchRunning ? "Cancelar" : "Start"
                            danger: App.researchRunning
                            enabled: App.researchRunning
                                || (root.researchBackendReady && topicInput.text.trim().length > 0)
                            onClicked: App.researchRunning ? App.cancelResearch() : root.start()
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: root.researchBackendReady
                            ? "Modelo y agente listos para investigar."
                            : App.agentStarting || (App.serverRunning && !App.serverReady)
                                ? "Esperando que terminen de iniciar el modelo y el agente…"
                                : App.serverRunning && App.serverReady
                                    ? "Iniciá el agente para habilitar Deep Research."
                                    : "Iniciá el modelo y el agente en Lanzar."
                        color: Theme.textMuted
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Rectangle { width: 1; Layout.fillHeight: true; color: Theme.divider }

            Rectangle {
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                color: Theme.baseBg

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        height: 42
                        color: Theme.baseBg
                        RowLayout {
                            anchors { fill: parent; leftMargin: 12; rightMargin: 8 }
                            Text {
                                text: "Reportes"
                                color: Theme.textPrimary
                                font { pixelSize: 13; bold: true }
                                Layout.fillWidth: true
                            }
                            LcButton {
                                text: "↻"
                                secondary: true
                                implicitWidth: 32
                                onClicked: App.refreshResearchReports()
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

                    ListView {
                        id: reportsList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: App.researchReports
                        ScrollBar.vertical: LcScrollBar { policy: ScrollBar.AsNeeded }

                        Text {
                            anchors.centerIn: parent
                            width: parent.width - 32
                            visible: reportsList.count === 0
                            text: "Sin reportes todavía."
                            color: Theme.textMuted
                            font.pixelSize: 12
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                        }

                        delegate: Rectangle {
                            readonly property bool belongsToWorkspace:
                                (modelData.workspaceId ?? "")
                                === (root.workspaceOptions[workspaceCombo.currentIndex].projectId ?? "")
                            width: reportsList.width
                            height: belongsToWorkspace ? 66 : 0
                            visible: belongsToWorkspace
                            color: (modelData.id ?? "") === root.selectedReportId
                                ? Theme.highlight
                                : (reportHover.containsMouse ? Theme.hoverBg : "transparent")

                            Rectangle {
                                width: 3
                                height: parent.height
                                color: (modelData.id ?? "") === root.selectedReportId ? Theme.accent : "transparent"
                            }

                            ColumnLayout {
                                anchors {
                                    verticalCenter: parent.verticalCenter
                                    left: parent.left; leftMargin: 14
                                    right: parent.right; rightMargin: 10
                                }
                                spacing: 3

                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.title ?? "Research"
                                    color: Theme.textPrimary
                                    font { pixelSize: 12; bold: (modelData.id ?? "") === root.selectedReportId }
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: (modelData.modeLabel ?? root.modeLabel(modelData.mode ?? "auto"))
                                        + " · " + (modelData.sourceCount ?? 0) + " fuentes"
                                        + ((modelData.workspaceName ?? "").length
                                            ? " · " + modelData.workspaceName
                                            : "")
                                        + ((modelData.dateLabel ?? "").length
                                            ? " · " + modelData.dateLabel
                                            : "")
                                    color: Theme.textMuted
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }
                            }

                            MouseArea {
                                id: reportHover
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onClicked: (mouse) => {
                                    root.selectReport(modelData.id ?? "")
                                    if (mouse.button === Qt.RightButton) reportMenu.popup()
                                }
                            }

                            Menu {
                                id: reportMenu
                                MenuItem { text: "Abrir .md"; onTriggered: App.openResearchReport(modelData.id ?? "") }
                                MenuItem {
                                    text: "Borrar"
                                    onTriggered: {
                                        App.deleteResearchReport(modelData.id ?? "")
                                        if ((modelData.id ?? "") === root.selectedReportId)
                                            root.selectReport("")
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle { width: 1; Layout.fillHeight: true; color: Theme.divider }

            Rectangle {
                id: reportPane
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.baseBg

                Flickable {
                    id: reportFlick
                    anchors {
                        top: parent.top
                        bottom: parent.bottom
                        left: parent.left
                        right: parent.right
                        margins: 18
                        rightMargin: 26
                    }
                    clip: true
                    contentWidth: width
                    contentHeight: reportText.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: LcScrollBar {
                        parent: reportPane
                        anchors {
                            top: parent.top
                            bottom: parent.bottom
                            right: parent.right
                            margins: 4
                        }
                        policy: ScrollBar.AsNeeded
                    }

                    TextEdit {
                        id: reportText
                        width: reportFlick.width
                        height: implicitHeight
                        text: root.selectedReportText.length > 0
                            ? root.selectedReportText
                            : "Seleccioná un reporte para previsualizarlo."
                        color: root.selectedReportText.length > 0 ? Theme.textPrimary : Theme.textMuted
                        font { family: "Segoe UI"; pixelSize: 13 }
                        wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                        readOnly: true
                        selectByMouse: true
                    }
                }
            }
        }
    }
}
