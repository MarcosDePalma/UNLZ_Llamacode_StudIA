import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0

Item {
    id: root
    property string selectedId: ""
    property var edit: ({})
    property var revisionRows: []
    property var feedbackRows: []
    property var metricRows: ({})

    component FieldLabel: Text {
        color: Theme.textSecondary
        font.pixelSize: 12
    }

    component ThemedTextArea: TextArea {
        color: Theme.textPrimary
        placeholderTextColor: Theme.textMuted
        selectionColor: Theme.accent
        selectedTextColor: Theme.btnPrimaryText
        font.pixelSize: 13
        padding: 10
        background: Rectangle {
            radius: 6
            color: Theme.inputBg
            border.color: parent.activeFocus ? Theme.inputBorderFocus : Theme.inputBorderColor
            border.width: parent.activeFocus ? 2 : 1
        }
    }

    function selectAgent(id) {
        selectedId = id || ""
        edit = selectedId ? App.agentDefinitions.get(selectedId) : ({})
        nameField.text = edit.name || ""
        descriptionField.text = edit.description || ""
        instructionsField.text = edit.instructions || ""
        profileField.text = edit.profileId || ""
        launchField.text = edit.launchProfileId || ""
        workspaceField.text = edit.workspaceId || ""
        taskField.text = (edit.taskIds || []).join(", ")
        skillsField.text = (edit.skillIds || []).join(", ")
        revisionRows = selectedId ? App.agentDefinitions.revisions(selectedId) : []
        feedbackRows = selectedId ? App.agentDefinitions.pendingFeedback(selectedId) : []
        metricRows = selectedId ? App.agentDefinitionMetrics(selectedId) : ({})
    }

    function splitIds(text) {
        return text.split(",").map(v => v.trim()).filter(v => v.length > 0)
    }

    function saveAgent(reason) {
        const id = App.agentDefinitions.save(selectedId, {
            name: nameField.text, description: descriptionField.text,
            instructions: instructionsField.text, profileId: profileField.text,
            launchProfileId: launchField.text, workspaceId: workspaceField.text,
            taskIds: splitIds(taskField.text), skillIds: splitIds(skillsField.text),
            mcpServers: edit.mcpServers || [], toolPermissions: edit.toolPermissions || {},
            triggerIds: edit.triggerIds || []
        }, reason || "Edición desde Agentes")
        if (id) selectAgent(id)
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 14

        Rectangle {
            Layout.preferredWidth: 260
            Layout.fillHeight: true
            color: Theme.surfaceBg
            radius: 8
            border.color: Theme.borderColor
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 10; spacing: 8
                RowLayout {
                    Text { text: "Agentes"; color: Theme.textPrimary; font.pixelSize: 19; font.bold: true }
                    Item { Layout.fillWidth: true }
                    LcButton {
                        text: "+"
                        implicitWidth: 40
                        Accessible.name: "Crear agente"
                        onClicked: {
                            root.selectedId = ""
                            root.edit = ({})
                            root.selectAgent("")
                            nameField.forceActiveFocus()
                        }
                    }
                }
                ListView {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    model: App.agentDefinitions
                    clip: true
                    delegate: ItemDelegate {
                        id: agentDelegate
                        required property string id
                        required property string name
                        required property int currentRevision
                        width: ListView.view.width
                        highlighted: root.selectedId === id
                        contentItem: Text {
                            text: agentDelegate.name + "  · r" + agentDelegate.currentRevision
                            color: agentDelegate.highlighted ? Theme.btnPrimaryText : Theme.textPrimary
                            font.pixelSize: 13
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 6
                            color: agentDelegate.highlighted ? Theme.accent
                                  : (agentDelegate.hovered ? Theme.inputBg : "transparent")
                        }
                        onClicked: root.selectAgent(id)
                    }
                }
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ColumnLayout {
                width: Math.max(620, parent.width - 16)
                spacing: 10

                Text { text: selectedId ? "Definición persistente" : "Nuevo agente"; color: Theme.textPrimary; font.pixelSize: 20; font.bold: true }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: quickHelp.implicitHeight + 24
                    radius: 8
                    color: Theme.surfaceBg
                    border.color: Theme.borderColor
                    Text {
                        id: quickHelp
                        anchors.fill: parent
                        anchors.margins: 12
                        color: Theme.textSecondary
                        wrapMode: Text.Wrap
                        text: selectedId
                              ? "Editá la identidad o las instrucciones y guardá una nueva revisión. Activar hace que esta definición sea la usada por el agente nativo."
                              : "Creá una identidad reutilizable para el agente: completá nombre, propósito e instrucciones. Los perfiles, workspace, Tasks, skills y triggers son opcionales."
                    }
                }

                FieldLabel { text: "Nombre *" }
                LcTextField { id: nameField; Layout.fillWidth: true; placeholderText: "Ej.: Asistente de investigación" }
                FieldLabel { text: "Descripción y propósito" }
                LcTextField { id: descriptionField; Layout.fillWidth: true; placeholderText: "Qué rol cumple y para qué conviene usarlo" }
                FieldLabel { text: "Instrucciones permanentes" }
                ThemedTextArea {
                    id: instructionsField; Layout.fillWidth: true; Layout.preferredHeight: 130
                    placeholderText: "Ej.: Respondé en español, citá fuentes y pedí confirmación antes de acciones externas."
                    wrapMode: TextEdit.Wrap
                }
                Text {
                    Layout.fillWidth: true
                    color: Theme.textMuted
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                    text: "Configuración avanzada (opcional). Usá los IDs existentes de Perfiles, Lanzar, Tasks y Skills; si los dejás vacíos, el agente hereda la configuración activa."
                }
                GridLayout {
                    Layout.fillWidth: true; columns: 2; columnSpacing: 8; rowSpacing: 8
                    LcTextField { id: profileField; Layout.fillWidth: true; placeholderText: "ID del perfil de agente" }
                    LcTextField { id: launchField; Layout.fillWidth: true; placeholderText: "ID del perfil de lanzamiento" }
                    LcTextField { id: workspaceField; Layout.fillWidth: true; placeholderText: "ID del workspace" }
                    LcTextField { id: taskField; Layout.fillWidth: true; placeholderText: "IDs de Tasks, separados por coma" }
                    LcTextField { id: skillsField; Layout.columnSpan: 2; Layout.fillWidth: true; placeholderText: "IDs de Skills, separados por coma" }
                }
                RowLayout {
                    LcButton {
                        text: selectedId ? "Guardar nueva revisión" : "Crear agente"
                        enabled: nameField.text.trim().length > 0
                        onClicked: root.saveAgent()
                    }
                    LcButton {
                        text: App.activeAgentDefinitionId === selectedId ? "Activo" : "Activar"
                        enabled: selectedId.length > 0 && App.activeAgentDefinitionId !== selectedId
                        onClicked: App.activateAgentDefinition(selectedId)
                    }
                    LcButton {
                        secondary: true
                        text: "Duplicar"; enabled: selectedId.length > 0
                        onClicked: root.selectAgent(App.agentDefinitions.duplicate(selectedId))
                    }
                    LcButton {
                        danger: true
                        text: "Eliminar"; enabled: selectedId.length > 0
                        onClicked: { App.agentDefinitions.remove(selectedId); root.selectAgent("") }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor }
                Text { text: "Resultado operativo"; color: Theme.textPrimary; font.pixelSize: 16; font.bold: true }
                Text {
                    Layout.fillWidth: true; color: Theme.textSecondary; wrapMode: Text.Wrap
                    text: "Corridas: " + (metricRows.runs || 0)
                          + " · éxito: " + Number(metricRows.successRate || 0).toFixed(1) + "%"
                          + " · tokens: " + Number((metricRows.promptTokens || 0)
                                                  + (metricRows.generatedTokens || 0)).toFixed(0)
                }

                Text { text: "Feedback supervisado"; color: Theme.textPrimary; font.pixelSize: 16; font.bold: true }
                Text {
                    Layout.fillWidth: true; color: Theme.textSecondary; wrapMode: Text.Wrap
                    text: "Proponé una corrección observada durante el uso. Después podés aprobarla para incorporarla como una nueva revisión o rechazarla."
                }
                RowLayout {
                    LcTextField { id: feedbackField; Layout.fillWidth: true; placeholderText: "Ej.: Siempre incluir el enlace de cada fuente" }
                    LcButton {
                        text: "Proponer"; enabled: selectedId.length > 0 && feedbackField.text.trim().length > 0
                        onClicked: {
                            App.agentDefinitions.proposeFeedback(selectedId, feedbackField.text, "agent")
                            feedbackField.clear()
                            root.selectAgent(selectedId)
                        }
                    }
                }
                Repeater {
                    model: feedbackRows
                    delegate: RowLayout {
                        Layout.fillWidth: true
                        Text { Layout.fillWidth: true; text: modelData.feedback; color: Theme.textSecondary; wrapMode: Text.Wrap }
                        LcButton {
                            text: "Aprobar"
                            onClicked: { App.agentDefinitions.approveFeedback(modelData.id); root.selectAgent(selectedId) }
                        }
                        LcButton {
                            secondary: true
                            text: "Rechazar"
                            onClicked: { App.agentDefinitions.rejectFeedback(modelData.id); root.selectAgent(selectedId) }
                        }
                    }
                }

                Text { text: "Historial de configuración"; color: Theme.textPrimary; font.pixelSize: 16; font.bold: true }
                Repeater {
                    model: revisionRows.slice().reverse()
                    delegate: RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true; color: Theme.textSecondary
                            text: "r" + modelData.number + " · " + modelData.reason + " · " + modelData.createdAt
                        }
                        LcButton {
                            secondary: true
                            text: "Restaurar"; enabled: modelData.number !== edit.currentRevision
                            onClicked: { App.agentDefinitions.restoreRevision(selectedId, modelData.number); root.selectAgent(selectedId) }
                        }
                    }
                }

                Text { text: "Triggers normalizados"; color: Theme.textPrimary; font.pixelSize: 16; font.bold: true }
                Text {
                    Layout.fillWidth: true; color: Theme.textSecondary; wrapMode: Text.Wrap
                    text: "Opcional: ejecutá una Task cuando cambie una ruta local, llegue un webhook o la app emita un evento. Necesitás el ID de una Task ya creada."
                }
                RowLayout {
                    LcComboBox { id: triggerType; model: ["filesystem", "webhook", "appEvent"] }
                    LcTextField { id: triggerValue; Layout.fillWidth: true; placeholderText: triggerType.currentText === "filesystem" ? "Ruta a vigilar" : "Clave o nombre del evento" }
                    LcTextField { id: triggerTask; Layout.fillWidth: true; placeholderText: "ID de la Task" }
                    LcButton {
                        text: "Agregar"
                        enabled: selectedId.length > 0 && triggerValue.text.length > 0 && triggerTask.text.length > 0
                        onClicked: {
                            const cfg = triggerType.currentText === "filesystem"
                                ? {path: triggerValue.text}
                                : (triggerType.currentText === "webhook"
                                   ? {key: triggerValue.text} : {name: triggerValue.text})
                            const tid = App.triggerManager.save("", {
                                name: triggerType.currentText + ": " + triggerValue.text,
                                agentId: selectedId, taskId: triggerTask.text,
                                type: triggerType.currentText, enabled: true,
                                debounceMs: 1500, config: cfg
                            })
                            if (tid) {
                                const a = App.agentDefinitions.get(selectedId)
                                const ids = (a.triggerIds || []).slice()
                                ids.push(tid); a.triggerIds = ids
                                App.agentDefinitions.save(selectedId, a, "Trigger agregado")
                                triggerValue.clear(); triggerTask.clear(); root.selectAgent(selectedId)
                            }
                        }
                    }
                }
                Repeater {
                    model: App.triggerManager
                    delegate: RowLayout {
                        required property string id
                        required property string agentId
                        required property string name
                        visible: agentId === root.selectedId
                        Layout.fillWidth: visible
                        Text { Layout.fillWidth: true; text: name; color: Theme.textSecondary }
                        LcButton {
                            danger: true
                            text: "Eliminar"
                            onClicked: { App.triggerManager.remove(id); root.selectAgent(root.selectedId) }
                        }
                    }
                }
                Item { Layout.preferredHeight: 20 }
            }
        }
    }
}
