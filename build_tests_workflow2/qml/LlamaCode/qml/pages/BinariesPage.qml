import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import LlamaCode 1.0

Item {
    id: root
    function openAddDialog() { addDlg.open() }

    LcDialog {
        id: addDlg
        title: (App.langV, App.l("binaries.addBinary"))
        width: 560
        height: 340

        onAccepted: {
            const newId = App.binaryRegistry.add(pathField.text, nameField.text,
                                                  flavorCombo.currentText, backendCombo.currentText, "")
            if (newId.length > 0) App.binaryRegistry.detectCapabilities(newId)
            pathField.text = ""; nameField.text = ""
        }

        contentItem: ColumnLayout {
            width: 520
            spacing: 12

            Text { text: (App.langV, App.l("binaries.path")); color: Theme.dialogLabel; font.pixelSize: 12; font.bold: true }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                LcTextField { id: pathField; Layout.fillWidth: true; placeholderText: "Path to llama-server.exe" }
                LcButton { text: (App.langV, App.l("common.browse")); secondary: true; onClicked: fileDlg.open() }
            }

            Text { text: (App.langV, App.l("binaries.name")); color: Theme.dialogLabel; font.pixelSize: 12; font.bold: true }
            LcTextField { id: nameField; Layout.fillWidth: true; placeholderText: "Optional display name" }

            RowLayout {
                Layout.fillWidth: true
                spacing: 14

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Text { text: (App.langV, App.l("binaries.flavor")); color: Theme.dialogLabel; font.pixelSize: 12; font.bold: true }
                    LcComboBox {
                        id: flavorCombo
                        Layout.fillWidth: true
                        model: ["official", "mtp-fork", "ninfer-3090", "custom"]
                        background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                        contentItem: Text { text: flavorCombo.displayText; color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Text { text: (App.langV, App.l("binaries.backend")); color: Theme.dialogLabel; font.pixelSize: 12; font.bold: true }
                    LcComboBox {
                        id: backendCombo
                        Layout.fillWidth: true
                        model: ["cpu", "cuda", "vulkan", "metal"]
                        background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                        contentItem: Text { text: backendCombo.displayText; color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
                    }
                }
            }
        }

        FileDialog {
            id: fileDlg
            title: (App.langV, App.l("binaries.selectTitle"))
            nameFilters: ["Executables (*.exe *.bin *)", "All files (*)"]
            onAccepted: {
                pathField.text = selectedFile.toString().replace(/^file:\/\/\//, "")
                // Auto-seleccionar backend según las DLLs junto al exe.
                const det = App.binaryRegistry.detectBackend(pathField.text)
                if (det.length > 0) {
                    const i = ["cpu", "cuda", "vulkan", "metal"].indexOf(det)
                    if (i >= 0) backendCombo.currentIndex = i
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            Layout.fillWidth: true
            title: (App.langV, App.l("binaries.title"))
            subtitle: {
                const _lang = App.langV
                return App.binaryRegistry.count + " " + App.l("binaries.registered")
            }
            action2Label: {
                const _lang = App.langV
                return App.installingOfficialBinary
                    ? App.l("binaries.downloading")
                    : App.l("binaries.downloadLatest")
            }
            onAction2Clicked: {
                if (!App.installingOfficialBinary) App.installOfficialBinary()
            }
            actionLabel: (App.langV, App.l("binaries.addAction"))
            onActionClicked: addDlg.open()
        }

        // Build MTP (Anbeeld/beellama.cpp): DFlash/MTP, solo Windows CUDA. Habilita
        // los flags de speculative decoding de los perfiles de sistema en NVIDIA.
        LcButton {
            text: App.installingOfficialBinary ? "Descargando…" : "Instalar build MTP (NVIDIA · beellama)"
            secondary: true
            enabled: !App.installingOfficialBinary
            onClicked: App.installMtpBinary()
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 150
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 8
            color: Theme.surfaceBg
            border.color: Theme.borderColor
            radius: 6
            clip: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "Catálogo de motores"
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "Instalá official/MTP o compilá forks llama-server desde source cuando no hay prebuilt."
                        color: Theme.textMuted
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    orientation: ListView.Horizontal
                    spacing: 10
                    clip: true
                    model: App.engineCatalog
                    ScrollBar.horizontal: LcScrollBar {}
                    delegate: Rectangle {
                        width: 300
                        height: 96
                        color: Theme.inputBg
                        border.color: modelData.recommended ? Theme.accent : Theme.borderColor
                        radius: 6

                        property var firstVariant: (modelData.variants && modelData.variants.length > 0)
                                                   ? modelData.variants[0] : ({})
                        property bool sourceBuild: firstVariant.buildFromSource ?? false

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 6
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.name + (modelData.recommended ? " · recomendado" : "")
                                    color: Theme.textPrimary
                                    font.pixelSize: 13
                                    font.bold: true
                                    elide: Text.ElideRight
                                }
                                Text {
                                    text: modelData.support
                                    color: modelData.support === "stable" ? Theme.successText : Theme.warnText
                                    font.pixelSize: 11
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.compatible
                                      ? modelData.description
                                      : (modelData.incompatibleReason || modelData.description)
                                color: modelData.compatible ? Theme.textSecondary : Theme.errorText
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: sourceBuild ? "Build from source" : (firstVariant.label || "Prebuilt / manual")
                                    color: Theme.textMuted
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                                LcButton {
                                    text: sourceBuild ? "Compilar" : "Instalar"
                                    secondary: !modelData.recommended
                                    enabled: !App.installingOfficialBinary && modelData.compatible
                                    onClicked: App.installCatalogEngine(modelData.id)
                                }
                            }
                        }
                    }
                }
            }
        }

        // download progress banner
        RowLayout {
            id: downloadBanner
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 6
            Layout.bottomMargin: 2
            spacing: 8
            visible: App.installingOfficialBinary || App.officialBinaryInstallStatus.length > 0

            BusyIndicator {
                running: App.installingOfficialBinary
                visible: App.installingOfficialBinary
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
            }
            Text {
                Layout.fillWidth: true
                text: App.officialBinaryInstallStatus
                color: App.installingOfficialBinary ? Theme.accent : Theme.textMuted
                font.pixelSize: 12
                elide: Text.ElideRight
            }
            LcButton {
                visible: App.installingOfficialBinary
                text: (App.langV, App.l("setup.cancel"))
                secondary: true
                onClicked: App.cancelOfficialBinaryInstall()
            }
            LcButton {
                visible: !App.installingOfficialBinary && App.officialBinaryInstallStatus.length > 0
                text: (App.langV, App.l("setup.viewLog"))
                secondary: true
                onClicked: installLogDlg.open()
            }
        }

        LcDialog {
            id: installLogDlg
            title: (App.langV, App.l("setup.installLog"))
            width: 680
            height: 420

            contentItem: ScrollView {
                clip: true
                TextArea {
                    readOnly: true
                    wrapMode: TextArea.WrapAnywhere
                    text: App.officialBinaryInstallLog
                    color: Theme.textPrimary
                    font.family: "Consolas"
                    font.pixelSize: 12
                    background: Rectangle { color: Theme.inputBg; radius: 6 }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            ListView {
                id: listView
                Layout.preferredWidth: 280
                Layout.fillHeight: true
                clip: true
                model: App.binaryRegistry
                currentIndex: -1
                ScrollBar.vertical: LcScrollBar {}

                delegate: ItemDelegate {
                    width: listView.width
                    height: 64
                    highlighted: listView.currentIndex === index
                    background: Rectangle {
                        color: parent.highlighted ? Theme.highlight : (parent.hovered ? Theme.hoverBg : "transparent")
                    }
                    contentItem: Column {
                        anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                        spacing: 3
                        Row {
                            spacing: 6
                            Text {
                                text: pathValid ? "●" : "○"
                                font.pixelSize: 10
                                color: pathValid ? Theme.successText : Theme.errorText
                            }
                            Text { text: name; font.pixelSize: 14; font.bold: true; color: Theme.textPrimary }
                        }
                        Text {
                            text: backend.toUpperCase() + " · " + flavor
                            font.pixelSize: 11
                            color: Theme.textMuted
                        }
                    }
                    onClicked: listView.currentIndex = index
                }

                Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.borderColor }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                Text {
                    anchors.centerIn: parent
                    visible: listView.currentIndex < 0
                    text: (App.langV, App.l("binaries.selectBinary"))
                    color: Theme.textMuted
                    font.pixelSize: 14
                }

                BinaryDetail {
                    anchors.fill: parent
                    visible: listView.currentIndex >= 0
                    binId: listView.currentIndex >= 0
                           ? App.binaryRegistry.data(App.binaryRegistry.index(listView.currentIndex, 0), Qt.UserRole + 1)
                           : ""
                }
            }
        }
    }

    component BinaryDetail: Item {
        property string binId: ""
        property var binData: binId.length > 0 ? App.binaryRegistry.get(binId) : ({})

        onBinIdChanged: nameEditField.text = binData.name ?? ""
        onBinDataChanged: { if (!nameEditField.activeFocus) nameEditField.text = binData.name ?? "" }

        // ── Edit dialog ───────────────────────────────────────────────────────
        LcDialog {
            id: editDlg
            title: "Editar binario"
            width: 520
            height: 420

            onOpened: {
                editNameField.text    = binData.name          ?? ""
                editVersionField.text = binData.versionHint   ?? ""
                editWorkDirField.text = binData.workingDirectory ?? ""
                editBackendCombo.currentIndex  = ["cpu","cuda","vulkan","metal"].indexOf(binData.backend ?? "cpu")
                editFlavorCombo.currentIndex   = ["official","mtp-fork","ninfer-3090","custom"].indexOf(binData.flavor ?? "official")
                if (editBackendCombo.currentIndex  < 0) editBackendCombo.currentIndex  = 0
                if (editFlavorCombo.currentIndex   < 0) editFlavorCombo.currentIndex   = 0
            }

            onAccepted: {
                const backends = ["cpu","cuda","vulkan","metal"]
                const flavors  = ["official","mtp-fork","ninfer-3090","custom"]
                App.binaryRegistry.update(
                    binId,
                    editNameField.text.trim(),
                    flavors[editFlavorCombo.currentIndex],
                    backends[editBackendCombo.currentIndex],
                    editVersionField.text.trim(),
                    editWorkDirField.text.trim()
                )
            }

            contentItem: ColumnLayout {
                width: 480
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Text { text: "Backend"; color: Theme.dialogLabel; font.pixelSize: 12; font.bold: true }
                        LcComboBox {
                            id: editBackendCombo
                            Layout.fillWidth: true
                            model: ["cpu", "cuda", "vulkan", "metal"]
                            background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                            contentItem: Text { text: editBackendCombo.displayText; color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Text { text: "Variante"; color: Theme.dialogLabel; font.pixelSize: 12; font.bold: true }
                        LcComboBox {
                            id: editFlavorCombo
                            Layout.fillWidth: true
                            model: ["official", "mtp-fork", "ninfer-3090", "custom"]
                            background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                            contentItem: Text { text: editFlavorCombo.displayText; color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
                        }
                    }
                }

                Text { text: "Nombre"; color: Theme.dialogLabel; font.pixelSize: 12; font.bold: true }
                LcTextField { id: editNameField; Layout.fillWidth: true; placeholderText: "Nombre visible" }

                Text { text: "Versión (opcional)"; color: Theme.dialogLabel; font.pixelSize: 12; font.bold: true }
                LcTextField { id: editVersionField; Layout.fillWidth: true; placeholderText: "ej: b4839-cuda12.4" }

                Text { text: "Working directory (opcional)"; color: Theme.dialogLabel; font.pixelSize: 12; font.bold: true }
                LcTextField { id: editWorkDirField; Layout.fillWidth: true; placeholderText: "Dejar vacío para usar el dir del binario" }
            }
        }

        ColumnLayout {
            anchors { fill: parent; margins: 24 }
            spacing: 16

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                LcTextField {
                    id: nameEditField
                    Layout.fillWidth: true
                    text: binData.name ?? ""
                    font.pixelSize: 18
                    font.bold: true
                    Keys.onReturnPressed: applyRename()
                    Keys.onEnterPressed: applyRename()
                    function applyRename() {
                        const t = nameEditField.text.trim()
                        if (t.length > 0 && t !== binData.name)
                            App.binaryRegistry.update(binId, t, binData.flavor ?? "official", binData.backend ?? "cpu", binData.versionHint ?? "", binData.workingDirectory ?? "")
                    }
                }
                LcButton {
                    text: (App.langV, App.l("binaries.rename"))
                    secondary: true
                    enabled: nameEditField.text.trim().length > 0 && nameEditField.text.trim() !== (binData.name ?? "")
                    onClicked: nameEditField.applyRename()
                }
            }

            GridLayout {
                columns: 2; columnSpacing: 16; rowSpacing: 8
                Layout.fillWidth: true

                Text { text: (App.langV, App.l("binaries.path")) + ":";    color: Theme.textMuted; font.pixelSize: 12 }
                Text { text: binData.path ?? ""; color: Theme.textSecondary; font.pixelSize: 12; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }

                Text { text: (App.langV, App.l("binaries.backend")) + ":"; color: Theme.textMuted; font.pixelSize: 12 }
                Text { text: (binData.backend ?? "").toUpperCase(); color: Theme.accent; font.pixelSize: 12 }

                Text { text: (App.langV, App.l("binaries.flavor")) + ":";  color: Theme.textMuted; font.pixelSize: 12 }
                Text { text: binData.flavor ?? ""; color: Theme.textSecondary; font.pixelSize: 12 }

                Text { text: (App.langV, App.l("binaries.status")) + ":";  color: Theme.textMuted; font.pixelSize: 12 }
                Text {
                    text: {
                        const _lang = App.langV
                        return (binData.pathValid ?? false) ? App.l("binaries.found") : App.l("binaries.notFound")
                    }
                    color: (binData.pathValid ?? false) ? Theme.successText : Theme.errorText
                    font.pixelSize: 12
                }

                Text { text: (App.langV, App.l("binaries.flags")) + ":";   color: Theme.textMuted; font.pixelSize: 12 }
                Text {
                    text: {
                        const _lang = App.langV
                        return (binData.hasCapabilities ?? false)
                            ? ((binData.supportedFlags?.length ?? 0) + " " + App.l("binaries.detected"))
                            : App.l("binaries.notDetected")
                    }
                    color: (binData.hasCapabilities ?? false) ? Theme.successText : Theme.textMuted
                    font.pixelSize: 12
                }
            }

            Row {
                spacing: 8
                LcButton {
                    text: "Editar"
                    secondary: true
                    onClicked: editDlg.open()
                }
                LcButton {
                    text: (App.langV, App.l("binaries.detectCaps"))
                    enabled: binData.pathValid ?? false
                    onClicked: App.binaryRegistry.detectCapabilities(binId)
                }
                LcButton {
                    text: (App.langV, App.l("binaries.remove"))
                    danger: true
                    onClicked: { App.binaryRegistry.remove(binId); listView.currentIndex = -1 }
                }
            }

            Rectangle {
                visible: (binData.supportedFlags?.length ?? 0) > 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.inputBg
                radius: 6
                border.color: Theme.borderColor
                clip: true

                ListView {
                    anchors { fill: parent; margins: 8 }
                    model: binData.supportedFlags ?? []
                    delegate: Text {
                        text: modelData
                        font { family: Theme.codeFont; pixelSize: 12 }
                        color: Theme.textSecondary
                        height: 20
                    }
                    ScrollBar.vertical: LcScrollBar {}
                }
            }

            Item { Layout.fillHeight: true }
        }
    }
}
