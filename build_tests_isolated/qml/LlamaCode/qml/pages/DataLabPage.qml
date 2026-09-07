import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id: root
    property string selectedJobId: ""
    property string lastMessage: ""
    property var selectedJob: ({})
    property string selectedDocumentId: ""

    function refreshJob() {
        selectedJob = selectedJobId.length > 0 ? App.dataLab.job(selectedJobId) : ({})
        const docs = selectedJob.documents || []
        selectedDocumentId = docs.length > 0 ? (docs[0].id || "") : ""
    }

    function refreshSelection() {
        if (selectedJobId.length === 0 && App.dataLab.jobs.length > 0)
            selectedJobId = App.dataLab.jobs[0].id || ""
        refreshJob()
    }

    Component.onCompleted: refreshSelection()
    Connections {
        target: App.dataLab
        function onJobsChanged() { root.refreshSelection() }
        function onJobChanged(id) { if (id === root.selectedJobId) { root.refreshJob(); root.lastMessage = "Job actualizado" } }
        function onExtractionFinished(jobId, documentId, ok, message) {
            if (jobId === root.selectedJobId) { root.refreshJob(); root.lastMessage = message }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            Layout.fillWidth: true
            title: "Data Lab"
            subtitle: "Documentos → datos estructurados verificables"
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                Layout.preferredWidth: 330
                Layout.fillHeight: true
                color: Theme.surfaceBg

                ColumnLayout {
                    anchors { fill: parent; margins: 16 }
                    spacing: 10
                    Text { text: "JOBS"; color: Theme.textSecondary; font.pixelSize: 10; font.bold: true }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: App.dataLab.jobs
                        delegate: Rectangle {
                            width: ListView.view.width
                            height: 58
                            radius: 6
                            color: (modelData.id || "") === root.selectedJobId ? Theme.highlight : "transparent"
                            Column {
                                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 10 }
                                Text { text: modelData.name || "Data Lab"; color: Theme.textPrimary; font.bold: true; elide: Text.ElideRight; width: parent.width }
                                Text { text: modelData.status || "created"; color: Theme.textMuted; font.pixelSize: 11 }
                            }
                            MouseArea { anchors.fill: parent; onClicked: { root.selectedJobId = modelData.id || ""; root.refreshJob() } }
                        }
                    }
                    LcButton {
                        Layout.fillWidth: true
                        text: "Nuevo job"
                        onClicked: { newJobDialog.open() }
                    }
                }
            }

            Rectangle { width: 1; Layout.fillHeight: true; color: Theme.divider }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 12
                anchors.margins: 18

                Text {
                    Layout.fillWidth: true
                    text: root.selectedJobId.length > 0 ? "Job seleccionado" : "Creá un job para comenzar"
                    color: Theme.textPrimary
                    font { pixelSize: 16; bold: true }
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: (root.selectedJob.documents || []).length > 0
                    Text { text: "Documento"; color: Theme.textSecondary; font.pixelSize: 11 }
                    ComboBox {
                        id: documentCombo
                        Layout.fillWidth: true
                        model: root.selectedJob.documents || []
                        textRole: "path"
                        onActivated: root.selectedDocumentId = (model[currentIndex].id || "")
                    }
                    LcButton {
                        text: "Prompt"
                        secondary: true
                        enabled: root.selectedDocumentId.length > 0
                        onClicked: {
                            const prompt = App.dataLab.extractionPrompt(root.selectedJobId, root.selectedDocumentId)
                            if (App.agentRunning) App.sendToAgent(prompt)
                            else root.lastMessage = "Iniciá el agente para enviar el prompt"
                        }
                    }
                    LcButton {
                        text: "Extraer con modelo"
                        enabled: root.selectedDocumentId.length > 0 && App.serverRunning
                        onClicked: App.dataLab.runExtraction(root.selectedJobId, root.selectedDocumentId,
                                                             App.serverBaseUrl, App.activeLaunchId)
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: root.selectedDocumentId.length > 0
                    TextArea {
                        id: recordJson
                        Layout.fillWidth: true
                        Layout.preferredHeight: 90
                        placeholderText: "Pegá aquí el JSON devuelto por el modelo para validarlo"
                        wrapMode: TextEdit.WrapAnywhere
                    }
                    LcButton {
                        text: "Validar"
                        enabled: recordJson.text.trim().length > 0
                        onClicked: {
                            try {
                                const parsed = JSON.parse(recordJson.text)
                                const result = Array.isArray(parsed)
                                        ? App.dataLab.validateRecords(root.selectedJobId, root.selectedDocumentId, recordJson.text)
                                        : App.dataLab.validateRecord(root.selectedJobId, root.selectedDocumentId, recordJson.text)
                                root.lastMessage = result.status + ((result.errors || []).length ? " · " + result.errors.join("; ") : "")
                                root.refreshJob()
                            } catch (e) {
                                root.lastMessage = "JSON inválido: " + e
                            }
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: root.lastMessage
                    color: Theme.accent
                    visible: text.length > 0
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    LcButton {
                        text: "Extraer documentos"
                        enabled: root.selectedJobId.length > 0
                        onClicked: {
                            const result = App.dataLab.processJob(root.selectedJobId)
                            root.lastMessage = result.ok ? (result.extracted + " extraídos · " + result.failed + " fallidos") : (result.error || "Error")
                        }
                    }
                    LcButton {
                        text: "Exportar JSON"
                        secondary: true
                        enabled: root.selectedJobId.length > 0
                        onClicked: { exportFormat = "json"; exportDialog.open() }
                    }
                    LcButton {
                        text: "Exportar CSV"
                        secondary: true
                        enabled: root.selectedJobId.length > 0
                        onClicked: { exportFormat = "csv"; exportDialog.open() }
                    }
                    LcButton {
                        text: "Exportar SQLite"
                        secondary: true
                        enabled: root.selectedJobId.length > 0
                        onClicked: { exportFormat = "sqlite"; exportDialog.open() }
                    }
                    LcButton {
                        text: "Eliminar"
                        secondary: true
                        enabled: root.selectedJobId.length > 0
                        onClicked: {
                            if (App.dataLab.deleteJob(root.selectedJobId)) {
                                root.selectedJobId = ""
                                root.lastMessage = "Job eliminado"
                            }
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: root.selectedJobId.length > 0
                    text: {
                        const m = App.dataLab.jobMetrics(root.selectedJobId)
                        return "Documentos: " + (m.total || 0)
                            + " · extraídos: " + (m.extracted || 0)
                            + " · válidos: " + (m.valid || 0)
                            + " · revisión: " + (m.needsReview || 0)
                            + " · fallidos: " + (m.failed || 0)
                    }
                    color: Theme.textMuted
                    font.pixelSize: 12
                }
                Text {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    text: root.selectedJobId.length > 0
                            ? JSON.stringify(root.selectedJob, null, 2)
                        : "El job guarda el esquema, los documentos, la extracción, la validación y la evidencia."
                    color: Theme.textSecondary
                    font.family: "Consolas"
                    font.pixelSize: 12
                    wrapMode: Text.WrapAnywhere
                    verticalAlignment: Text.AlignTop
                    elide: Text.ElideNone
                }
            }
        }
    }

    Dialog {
        id: newJobDialog
        modal: true
        width: Math.min(root.width - 40, 680)
        title: "Nuevo Data Lab"
        standardButtons: Dialog.Ok | Dialog.Cancel
        property string defaultSchema: '{"fields":{"cliente":{"type":"string","required":true},"fecha":{"type":"date","required":true},"importe":{"type":"number","required":true},"moneda":{"type":"enum","values":["ARS","USD","EUR"]}}}'
        contentItem: ColumnLayout {
            spacing: 8
            TextField { id: jobName; Layout.fillWidth: true; placeholderText: "Nombre del job" }
            RowLayout {
                Layout.fillWidth: true
                TextField { id: filesField; Layout.fillWidth: true; placeholderText: "Rutas separadas por ;" }
                LcButton { text: "Elegir"; secondary: true; onClicked: inputFiles.open() }
            }
            TextArea { id: schemaField; Layout.fillWidth: true; Layout.preferredHeight: 160; text: newJobDialog.defaultSchema; wrapMode: TextEdit.Wrap }
            Text { text: "El esquema debe contener fields con type y required opcional."; color: Theme.textMuted; font.pixelSize: 11 }
        }
        onAccepted: {
            const files = filesField.text.split(";").map(function(v) { return v.trim() }).filter(function(v) { return v.length > 0 })
            const id = App.dataLab.createJob(jobName.text, schemaField.text, files)
            root.lastMessage = id.length > 0 ? "Job creado" : "No se pudo crear el job: revisá esquema y archivos"
            if (id.length > 0) root.selectedJobId = id
        }
    }

    FileDialog {
        id: inputFiles
        title: "Seleccionar documentos"
        fileMode: FileDialog.OpenFiles
        onAccepted: {
            const paths = selectedFiles.map(function(url) { return url.toLocalFile ? url.toLocalFile() : url.toString() })
            filesField.text = paths.join(";")
        }
    }

    property string exportFormat: "json"
    FileDialog {
        id: exportDialog
        title: "Exportar Data Lab"
        fileMode: FileDialog.SaveFile
        onAccepted: {
            const url = selectedFile
            const path = url && url.toLocalFile ? url.toLocalFile() : String(url)
            const result = App.dataLab.exportJob(root.selectedJobId, path, root.exportFormat)
            root.lastMessage = result.length > 0 ? "Exportado: " + result : "No se pudo exportar"
        }
    }
}
