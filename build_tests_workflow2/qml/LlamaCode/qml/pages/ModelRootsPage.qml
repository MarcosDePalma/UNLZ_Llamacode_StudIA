import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import LlamaCode 1.0

Item {
    id: root

    property string modelsMode: "installed"
    property string downloadSearch: ""
    property string downloadSize: "Todos"
    property string downloadFamily: "Todas"
    property string downloadCapability: "Todas"

    function openAddDialog() { addDlg.open() }

    function filteredRecommendations() {
        const q = downloadSearch.trim().toLowerCase()
        let out = []
        for (let i = 0; i < App.modelRecommendations.length; ++i) {
            const m = App.modelRecommendations[i]
            const name = String(m.name ?? "").toLowerCase()
            const repo = String(m.repo ?? "").toLowerCase()
            const family = String(m.family ?? "")
            const caps = String(m.capabilities ?? "")
            const size = Number(m.sizeGb ?? 0)
            if (q.length > 0 && name.indexOf(q) < 0 && repo.indexOf(q) < 0)
                continue
            if (downloadFamily !== "Todas" && family !== downloadFamily)
                continue
            if (downloadCapability !== "Todas" && caps.indexOf(downloadCapability.toLowerCase()) < 0)
                continue
            if (downloadSize === "< 8 GB" && !(size < 8))
                continue
            if (downloadSize === "8-16 GB" && !(size >= 8 && size <= 16))
                continue
            if (downloadSize === "16-24 GB" && !(size > 16 && size <= 24))
                continue
            if (downloadSize === "> 24 GB" && !(size > 24))
                continue
            out.push(m)
        }
        return out
    }

    function featuredRecommendations() {
        const lanes = ["General", "Reasoning", "Código"]
        let out = []
        function hasModel(candidate) {
            for (let i = 0; i < out.length; ++i) {
                if (String(out[i].name ?? "") === String(candidate.name ?? ""))
                    return true
            }
            return false
        }
        for (let l = 0; l < lanes.length; ++l) {
            for (let i = 0; i < App.modelRecommendations.length; ++i) {
                const m = App.modelRecommendations[i]
                if (String(m.recommendationLane ?? "General") !== lanes[l])
                    continue
                if (String(m.fit ?? "") === "No entra")
                    continue
                if (!hasModel(m)) {
                    out.push(m)
                    break
                }
            }
        }
        for (let i = 0; i < App.modelRecommendations.length && out.length < 3; ++i) {
            const m = App.modelRecommendations[i]
            if (String(m.fit ?? "") !== "No entra" && !hasModel(m))
                out.push(m)
        }
        return out
    }

    LcDialog {
        id: addDlg
        title: (App.langV, App.l("models.addRoot"))
        width: 560
        height: 300

        onAccepted: {
            App.rootRegistry.add(pathField.text, labelField.text, scanCombo.currentText, [])
            pathField.text = ""; labelField.text = ""
        }

        contentItem: ColumnLayout {
            width: 520
            spacing: 12

            Text { text: (App.langV, App.l("models.path")); color: Theme.dialogLabel; font.pixelSize: 12; font.bold: true }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                LcTextField { id: pathField; Layout.fillWidth: true; placeholderText: "Folder with GGUF files" }
                LcButton { text: (App.langV, App.l("common.browse")); secondary: true; onClicked: folderDlg.open() }
            }

            Text { text: (App.langV, App.l("models.label")); color: Theme.dialogLabel; font.pixelSize: 12; font.bold: true }
            LcTextField { id: labelField; Layout.fillWidth: true; placeholderText: "Optional label" }

            Text { text: (App.langV, App.l("models.scanMode")); color: Theme.dialogLabel; font.pixelSize: 12; font.bold: true }
            LcComboBox {
                id: scanCombo
                Layout.fillWidth: true
                model: ["manual", "startup", "watch"]
                background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                contentItem: Text { text: scanCombo.displayText; color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
            }

            // Ingesta de modelos ya descargados por Ollama (reusa los blobs GGUF,
            // sin re-descargar). Sólo visible si hay un store detectable.
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                visible: App.ollamaStoreAvailable()
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    text: "Ollama detectado — importar modelos existentes"
                    color: Theme.textSecondary; font.pixelSize: 12; wrapMode: Text.WordWrap
                }
                LcButton {
                    text: "Importar de Ollama"; secondary: true
                    onClicked: { App.importOllamaModels(""); addDlg.close() }
                }
            }
        }

        FolderDialog {
            id: folderDlg
            title: "Select model folder"
            onAccepted: pathField.text = selectedFolder.toString().replace("file:///", "")
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            Layout.fillWidth: true
            title: (App.langV, App.l("models.title"))
            subtitle: {
                const _lang = App.langV
                return App.rootRegistry.count + " " + App.l("models.roots") + " · " + App.modelCatalog.count + " " + App.l("models.modelsCount")
            }
            actionLabel: {
                const _lang = App.langV
                return App.rootRegistry.scanning ? App.l("models.scanning") : App.l("models.addAction")
            }
            onActionClicked: if (!App.rootRegistry.scanning) addDlg.open()
        }

        // ── Perfiles de sistema recomendados para esta computadora ──
        // Mismo auto-instalador rápido que el asistente (baja modelos+binarios).
        Rectangle {
            id: recCard
            Layout.fillWidth: true
            Layout.preferredHeight: recCol.implicitHeight + 24
            readonly property var showcase: (App.hardwareSummary, App.recommendedShowcase())
            readonly property var sysPick: (App.hardwareSummary, App.recommendedSystemProfile())
            visible: showcase.length > 0 || (sysPick.launchId ?? "").length > 0
            radius: 8
            color: Theme.surfaceBg
            border.color: Theme.accent

            ColumnLayout {
                id: recCol
                anchors.fill: parent
                anchors.margins: 12
                spacing: 6
                Text {
                    text: "★ Perfiles recomendados para tu computadora"
                    color: Theme.textPrimary
                    font { pixelSize: 14; bold: true }
                }
                Repeater {
                    model: recCard.showcase
                    Text {
                        Layout.fillWidth: true
                        text: "• " + (modelData.displayName || "")
                        color: Theme.textSecondary; font.pixelSize: 12; wrapMode: Text.WordWrap
                    }
                }
                Text {
                    visible: recCard.showcase.length === 0
                    Layout.fillWidth: true
                    text: (recCard.sysPick.displayName ?? "")
                    color: Theme.textSecondary; font.pixelSize: 12; wrapMode: Text.WordWrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    spacing: 10
                    LcButton {
                        visible: recCard.showcase.length > 0
                        text: "Instalar ambos"
                        enabled: !App.modelDownloadRunning
                        onClicked: App.acceptShowcase()
                    }
                    Repeater {
                        model: recCard.showcase
                        LcButton {
                            text: "Sólo " + (modelData.label || modelData.displayName || ""); secondary: true
                            enabled: !App.modelDownloadRunning && (modelData.launchId || "").length > 0
                            onClicked: App.acceptShowcaseOne(modelData.launchId)
                        }
                    }
                    LcButton {
                        visible: recCard.showcase.length === 0
                        text: "Instalar y usar"
                        enabled: !App.modelDownloadRunning && (recCard.sysPick.launchId ?? "").length > 0
                        onClicked: App.installAndUseSystemProfile(recCard.sysPick.launchId)
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: App.modelDownloadQueue.length > 0 ? 300
                                  : ((App.modelDownloadRunning || App.modelDownloadStatus.length > 0) ? 242 : 210)
            color: Theme.baseBg
            border.color: Theme.borderColor
            border.width: 0

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: "Cookbook local"
                            color: Theme.textPrimary
                            font { pixelSize: 15; bold: true }
                        }
                        Text {
                            text: App.hardwareSummary.summary ?? "Detectando hardware..."
                            color: Theme.textSecondary
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                    LcButton {
                        text: "Rescan"
                        secondary: true
                        onClicked: App.rescanHardware()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 10

                    Repeater {
                        model: root.featuredRecommendations()
                        delegate: Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 7
                            color: Theme.surfaceBg
                            border.color: Theme.borderColor

                            readonly property var item: modelData ?? ({})

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 7

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: (item.recommendationLane ?? "General") + " · " + (item.fit ?? "")
                                        color: (item.fit ?? "") === "No entra" ? Theme.errorText
                                             : ((item.fit ?? "") === "Marginal" ? Theme.warnText : Theme.successText)
                                        font { pixelSize: 10; bold: true }
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        text: (item.score ?? 0) + "/100"
                                        color: Theme.accent
                                        font { pixelSize: 11; bold: true }
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: item.name ?? ""
                                    color: Theme.textPrimary
                                    font { pixelSize: 13; bold: true }
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: (item.params ?? "") + " · " + (item.quant ?? "") + " · " + (item.sizeGb ?? 0).toFixed(1) + " GB · ctx " + (item.ctxK ?? 0) + "k"
                                    color: Theme.textSecondary
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    text: item.notes ?? ""
                                    color: Theme.textMuted
                                    font.pixelSize: 11
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.bottomMargin: 6
                                    spacing: 6
                                    LcButton {
                                        text: "Download"
                                        Layout.preferredHeight: 34
                                        enabled: item.downloadable ?? true
                                        onClicked: App.downloadRecommendedModel(item.repo ?? "", item.fileName ?? "")
                                    }
                                    LcButton {
                                        text: "HF"
                                        Layout.preferredHeight: 34
                                        secondary: true
                                        onClicked: App.openModelRecommendation(item.repo ?? "")
                                    }
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: App.modelDownloadQueue.length === 0
                             && (App.modelDownloadRunning || App.modelDownloadStatus.length > 0)
                    Layout.preferredHeight: visible ? 26 : 0
                    spacing: 8
                    ProgressBar {
                        Layout.preferredWidth: 140
                        Layout.preferredHeight: 8
                        from: 0
                        to: 100
                        value: App.modelDownloadProgress
                        background: Rectangle { color: Theme.inputBg; radius: 4 }
                        contentItem: Item {
                            Rectangle {
                                width: parent.width * (App.modelDownloadProgress / 100)
                                height: parent.height
                                radius: 4
                                color: Theme.accent
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: App.modelDownloadStatus
                        color: App.modelDownloadRunning ? Theme.accent : Theme.textMuted
                        font.pixelSize: 11
                        elide: Text.ElideMiddle
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: App.modelDownloadQueue.length > 0
                    Layout.preferredHeight: visible ? 94 : 0
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "Cola de descargas"
                            color: Theme.textPrimary
                            font { pixelSize: 12; bold: true }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: App.modelDownloadRunning ? "Descargando 1 a la vez" : "Sin descarga activa"
                            color: Theme.textMuted
                            font.pixelSize: 11
                            horizontalAlignment: Text.AlignRight
                        }
                    }

                    ListView {
                        id: downloadQueueList
                        Layout.fillWidth: true
                        Layout.preferredHeight: 66
                        clip: true
                        orientation: ListView.Horizontal
                        spacing: 8
                        model: App.modelDownloadQueue

                        delegate: Rectangle {
                            width: Math.max(280, Math.min(downloadQueueList.width - 8, Math.max(460, downloadQueueList.width * 0.36)))
                            height: 64
                            radius: 7
                            color: (modelData.active ?? false) ? Theme.inputBg : Theme.surfaceBg
                            border.color: (modelData.active ?? false) ? Theme.accent : Theme.borderColor

                            readonly property string state: modelData.state ?? ""
                            readonly property int progress: modelData.progress ?? 0
                            readonly property bool active: modelData.active ?? false
                            readonly property bool paused: state === "paused"
                            readonly property bool movable: !active && state !== "done"

                            ColumnLayout {
                                anchors { fill: parent; margins: 8 }
                                spacing: 6
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.fileName ?? ""
                                        color: Theme.textPrimary
                                        font { pixelSize: 11; bold: true }
                                        elide: Text.ElideMiddle
                                    }
                                    Text {
                                        text: paused ? "Pausada"
                                             : state === "queued" ? "En cola"
                                             : state === "done" ? "Lista"
                                             : state === "error" ? "Error"
                                             : "Activa"
                                        color: state === "error" ? Theme.errorText
                                             : state === "done" ? Theme.successText
                                             : active ? Theme.accent : Theme.textMuted
                                        font.pixelSize: 10
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 5
                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 80
                                        Layout.preferredHeight: 6
                                        radius: 3
                                        color: Theme.baseBg
                                        Rectangle {
                                            width: parent.width * (progress / 100)
                                            height: parent.height
                                            radius: 3
                                            color: state === "error" ? Theme.errorText
                                                 : state === "done" ? Theme.successText : Theme.accent
                                        }
                                    }
                                    Text {
                                        text: progress + "%"
                                        color: Theme.textMuted
                                        font.pixelSize: 10
                                        Layout.preferredWidth: 30
                                        horizontalAlignment: Text.AlignRight
                                    }
                                    LcButton {
                                        text: paused || state === "error" ? "▶" : "Ⅱ"
                                        secondary: true
                                        Layout.preferredWidth: 28
                                        Layout.preferredHeight: 24
                                        visible: state !== "done"
                                        onClicked: paused || state === "error"
                                                   ? App.resumeModelDownload(modelData.id ?? "")
                                                   : App.pauseModelDownload(modelData.id ?? "")
                                    }
                                    LcButton {
                                        text: "↑"
                                        secondary: true
                                        Layout.preferredWidth: 26
                                        Layout.preferredHeight: 24
                                        enabled: movable && index > 0
                                        visible: state !== "done"
                                        onClicked: App.moveModelDownload(modelData.id ?? "", -1)
                                    }
                                    LcButton {
                                        text: "↓"
                                        secondary: true
                                        Layout.preferredWidth: 26
                                        Layout.preferredHeight: 24
                                        enabled: movable && index < App.modelDownloadQueue.length - 1
                                        visible: state !== "done"
                                        onClicked: App.moveModelDownload(modelData.id ?? "", 1)
                                    }
                                    LcButton {
                                        text: "✕"
                                        secondary: true
                                        Layout.preferredWidth: 28
                                        Layout.preferredHeight: 24
                                        onClicked: App.cancelModelDownload(modelData.id ?? "")
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderColor }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            ListView {
                id: rootList
                Layout.preferredWidth: 260
                Layout.fillHeight: true
                clip: true
                model: App.rootRegistry
                ScrollBar.vertical: LcScrollBar {}

                delegate: ItemDelegate {
                    width: rootList.width
                    height: 60
                    highlighted: rootList.currentIndex === index
                    background: Rectangle {
                        color: parent.highlighted ? Theme.highlight : (parent.hovered ? Theme.hoverBg : "transparent")
                    }
                    contentItem: Column {
                        anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                        spacing: 3
                        Row {
                            spacing: 6
                            Text { text: isOnline ? "●" : "○"; font.pixelSize: 10; color: isOnline ? Theme.successText : Theme.errorText }
                            Text { text: label; font.pixelSize: 14; font.bold: true; color: Theme.textPrimary }
                            // Chip que distingue un store de Ollama de una carpeta normal.
                            Rectangle {
                                visible: kind === "ollama"
                                anchors.verticalCenter: parent.verticalCenter
                                radius: 4
                                color: Theme.accent
                                width: ollamaTag.implicitWidth + 10
                                height: ollamaTag.implicitHeight + 4
                                Text {
                                    id: ollamaTag
                                    anchors.centerIn: parent
                                    text: "Ollama"; font.pixelSize: 9; font.bold: true
                                    color: Theme.baseBg
                                }
                            }
                        }
                        Text { width: rootList.width - 32; text: path; font.pixelSize: 11; color: Theme.textMuted; elide: Text.ElideMiddle }
                        Text { text: scanMode; font.pixelSize: 10; color: Theme.textMuted }
                    }
                    onClicked: { rootList.currentIndex = index; App.modelCatalog.filterRootId = rootId }
                }

                Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.borderColor }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    height: 44
                    color: Theme.surfaceBg
                    visible: root.modelsMode === "installed" && rootList.currentIndex >= 0

                    Row {
                        anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                        spacing: 8

                        LcButton {
                            text: (App.langV, App.l("models.scan"))
                            onClicked: {
                                const id = App.rootRegistry.data(App.rootRegistry.index(rootList.currentIndex, 0), 257)
                                App.rootRegistry.scan(id)
                            }
                        }
                        LcButton { text: (App.langV, App.l("models.scanAll")); secondary: true; onClicked: App.rootRegistry.scanAll() }
                        LcButton {
                            text: (App.langV, App.l("models.removeRoot"))
                            danger: true
                            onClicked: {
                                const id = App.rootRegistry.data(App.rootRegistry.index(rootList.currentIndex, 0), 257)
                                App.rootRegistry.remove(id)
                                rootList.currentIndex = -1
                                App.modelCatalog.filterRootId = ""
                            }
                        }
                    }

                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderColor }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 48
                    color: Theme.baseBg
                    RowLayout {
                        anchors { fill: parent; leftMargin: 12; rightMargin: 12; topMargin: 7; bottomMargin: 7 }
                        spacing: 8

                        Rectangle {
                            Layout.preferredWidth: 92
                            Layout.fillHeight: true
                            radius: 6
                            color: root.modelsMode === "installed" ? Theme.highlight : Theme.inputBg
                            border.color: root.modelsMode === "installed" ? Theme.accent : Theme.borderColor
                            Text {
                                anchors.centerIn: parent
                                text: "Instalados"
                                color: root.modelsMode === "installed" ? Theme.accent : Theme.textSecondary
                                font { pixelSize: 12; bold: root.modelsMode === "installed" }
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.modelsMode = "installed"
                            }
                        }
                        Rectangle {
                            Layout.preferredWidth: 92
                            Layout.fillHeight: true
                            radius: 6
                            color: root.modelsMode === "download" ? Theme.highlight : Theme.inputBg
                            border.color: root.modelsMode === "download" ? Theme.accent : Theme.borderColor
                            Text {
                                anchors.centerIn: parent
                                text: "Descargar"
                                color: root.modelsMode === "download" ? Theme.accent : Theme.textSecondary
                                font { pixelSize: 12; bold: root.modelsMode === "download" }
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.modelsMode = "download"
                            }
                        }

                        LcTextField {
                            Layout.preferredWidth: root.modelsMode === "installed" ? 180 : 210
                            Layout.fillHeight: true
                            visible: root.modelsMode === "installed"
                            placeholderText: (App.langV, App.l("models.filterFamily"))
                            onTextChanged: App.modelCatalog.filterFamily = text
                        }
                        CheckBox {
                            visible: root.modelsMode === "installed"
                            text: (App.langV, App.l("models.visionOnly"))
                            contentItem: Text { text: parent.text; color: Theme.textSecondary; font.pixelSize: 12; leftPadding: parent.indicator.width + 6 }
                            onCheckedChanged: App.modelCatalog.filterVisionOnly = checked
                        }

                        LcTextField {
                            Layout.preferredWidth: 210
                            Layout.fillHeight: true
                            visible: root.modelsMode === "download"
                            placeholderText: "Buscar modelo..."
                            onTextChanged: root.downloadSearch = text
                        }
                        LcComboBox {
                            Layout.preferredWidth: 112
                            Layout.fillHeight: true
                            visible: root.modelsMode === "download"
                            model: ["Todos", "< 8 GB", "8-16 GB", "16-24 GB", "> 24 GB"]
                            onCurrentTextChanged: root.downloadSize = currentText
                            background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                            contentItem: Text { text: parent.displayText; color: Theme.textPrimary; font.pixelSize: 11; leftPadding: 8; verticalAlignment: Text.AlignVCenter }
                        }
                        LcComboBox {
                            Layout.preferredWidth: 110
                            Layout.fillHeight: true
                            visible: root.modelsMode === "download"
                            model: ["Todas", "Qwen", "Mistral", "DeepSeek", "GPT OSS", "Gemma", "Phi"]
                            onCurrentTextChanged: root.downloadFamily = currentText
                            background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                            contentItem: Text { text: parent.displayText; color: Theme.textPrimary; font.pixelSize: 11; leftPadding: 8; verticalAlignment: Text.AlignVCenter }
                        }
                        LcComboBox {
                            Layout.preferredWidth: 116
                            Layout.fillHeight: true
                            visible: root.modelsMode === "download"
                            model: ["Todas", "Code", "Chat", "Reasoning", "MoE", "Edge"]
                            onCurrentTextChanged: root.downloadCapability = currentText
                            background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                            contentItem: Text { text: parent.displayText; color: Theme.textPrimary; font.pixelSize: 11; leftPadding: 8; verticalAlignment: Text.AlignVCenter }
                        }

                        Item { Layout.fillWidth: true }
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderColor }
                }

                ListView {
                    id: modelList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    visible: root.modelsMode === "installed"
                    model: App.modelCatalog
                    ScrollBar.vertical: LcScrollBar {}

                    delegate: Rectangle {
                        width: modelList.width
                        height: 56
                        color: modelList.currentIndex === index ? Theme.highlight : (hovered ? Theme.hoverBg : "transparent")
                        property bool hovered: false
                        HoverHandler { onHoveredChanged: parent.hovered = hovered }
                        MouseArea { anchors.fill: parent; onClicked: modelList.currentIndex = index }

                        Row {
                            anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                            spacing: 10
                            Column {
                                spacing: 3
                                Row {
                                    spacing: 6
                                    Text { text: isVision ? "👁" : (isDraft ? "⚡" : "📄"); font.pixelSize: 14 }
                                    Text { text: fileName; font.pixelSize: 13; font.bold: true; color: isAvailable ? Theme.textPrimary : Theme.textMuted }
                                }
                                Row {
                                    spacing: 8
                                    Text { text: family;    font.pixelSize: 11; color: Theme.accent }
                                    // Quant real (composición de tensores). Si difiere del nombre
                                    // de archivo, lo marcamos: el sufijo del archivo miente seguido.
                                    Text {
                                        text: (quantReal && quantReal.length > 0)
                                              ? (quantMismatch ? "⚠ " + quant + "→" + quantReal : quantReal)
                                              : quant
                                        font.pixelSize: 11
                                        color: quantMismatch ? Theme.warnText : Theme.successText
                                    }
                                    Text {
                                        visible: bpw > 0
                                        text: bpw.toFixed(2) + " bpw"
                                        font.pixelSize: 11; color: Theme.textMuted
                                    }
                                    Text { text: sizeLabel; font.pixelSize: 11; color: Theme.textMuted }
                                    Text {
                                        visible: trainedContext > 0
                                        text: (architecture || "GGUF") + " · ctx entrenado "
                                              + (trainedContext >= 1024
                                                 ? Math.round(trainedContext / 1024) + "k"
                                                 : trainedContext)
                                        font.pixelSize: 11; color: Theme.textMuted
                                    }
                                    // Gemma QAT q4_0 crudo: degradado en llama.cpp; preferir UD-Q4_K_XL.
                                    Text {
                                        visible: {
                                            const fn = (fileName || "").toLowerCase()
                                            if (!fn.includes("qat")) return false
                                            if (!(family || "").toLowerCase().includes("gemma")) return false
                                            if ((quantReal || "").toLowerCase() !== "q4_0") return false
                                            return !(fn.includes("ud-") || fn.includes("ud_")
                                                     || fn.includes("unsloth") || fn.includes("k_xl"))
                                        }
                                        text: "⚠ raw QAT → preferí UD-Q4_K_XL"
                                        font.pixelSize: 11; color: Theme.warnText
                                    }
                                }
                            }
                        }

                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.surfaceBg }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: parent.count === 0
                        text: {
                            const _lang = App.langV
                            return rootList.currentIndex < 0 ? App.l("models.selectRoot") : App.l("models.noModels")
                        }
                        color: Theme.textMuted
                        font.pixelSize: 14
                    }
                }

                ListView {
                    id: downloadList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    visible: root.modelsMode === "download"
                    model: root.filteredRecommendations()
                    ScrollBar.vertical: LcScrollBar {}

                    delegate: Rectangle {
                        width: downloadList.width
                        height: 78
                        color: hovered ? Theme.hoverBg : "transparent"
                        property bool hovered: false
                        HoverHandler { onHoveredChanged: parent.hovered = hovered }

                        readonly property var item: modelData ?? ({})

                        RowLayout {
                            anchors { fill: parent; leftMargin: 16; rightMargin: 16; topMargin: 9; bottomMargin: 9 }
                            spacing: 12

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        text: item.fit ?? ""
                                        color: (item.fit ?? "") === "No entra" ? Theme.errorText
                                             : ((item.fit ?? "") === "Marginal" ? Theme.warnText : Theme.successText)
                                        font { pixelSize: 10; bold: true }
                                    }
                                    Text {
                                        text: "· " + (item.family ?? "")
                                        color: Theme.accent
                                        font.pixelSize: 10
                                    }
                                    Text {
                                        text: "· " + (item.capabilities ?? "")
                                        color: Theme.textMuted
                                        font.pixelSize: 10
                                    }
                                    Item { Layout.fillWidth: true }
                                    Text {
                                        text: (item.score ?? 0) + "/100"
                                        color: Theme.accent
                                        font { pixelSize: 11; bold: true }
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: item.name ?? ""
                                    color: Theme.textPrimary
                                    font { pixelSize: 13; bold: true }
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                text: (item.params ?? "") + " · " + (item.quant ?? "") + " · " +
                                          (item.requiredGb ?? item.sizeGb ?? 0).toFixed(1) + " GB · ctx " + (item.ctxK ?? 0) + "k · " +
                                          (item.notes ?? "")
                                    color: Theme.textMuted
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                            }

                            LcButton {
                                text: "Download"
                                Layout.preferredHeight: 34
                                enabled: item.downloadable ?? true
                                onClicked: App.downloadRecommendedModel(item.repo ?? "", item.fileName ?? "")
                            }
                            LcButton {
                                text: "HF"
                                Layout.preferredHeight: 34
                                secondary: true
                                onClicked: App.openModelRecommendation(item.repo ?? "")
                            }
                        }

                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.surfaceBg }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: parent.count === 0
                        text: "Sin resultados para esos filtros"
                        color: Theme.textMuted
                        font.pixelSize: 14
                    }
                }
            }
        }
    }
}
