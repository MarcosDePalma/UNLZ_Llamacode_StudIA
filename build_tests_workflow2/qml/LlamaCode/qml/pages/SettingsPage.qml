import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import LlamaCode 1.0

Item {
    id: root

    LcColorPicker {
        id: colorPicker
        property var target: null
        onPicked: function (hex) { if (target) target.value = hex }
    }

    property var gpuRows: []
    property var selectedVramGpus: []

    function refreshGpuInventory() {
        const info = App.gpuInventory()
        gpuRows = info.gpus ?? []
        const raw = String(App.readSetting("gpu/vramIndices", ""))
        selectedVramGpus = raw.length === 0 ? [] : raw.split(",").map(function(v) { return Number(v) })
    }

    function isVramGpuSelected(index) { return selectedVramGpus.indexOf(index) >= 0 }
    function setVramGpuSelected(index, selected) {
        let values = selectedVramGpus.slice()
        const pos = values.indexOf(index)
        if (selected && pos < 0) values.push(index)
        if (!selected && pos >= 0) values.splice(pos, 1)
        values.sort(function(a, b) { return a - b })
        selectedVramGpus = values
        App.writeSetting("gpu/vramIndices", values.join(","))
    }

    Component.onCompleted: refreshGpuInventory()

    FolderDialog {
        id: openCodeProjectDialog
        title: "Elegí el proyecto para OpenCode"
        onAccepted: {
            var path = selectedFolder.toString().replace("file:///", "")
            var err = App.launchOpenCode(path, openCodeProfile.currentValue ?? "")
            gwMsg.text = err.length === 0
                ? "OpenCode Desktop lanzado contra " + App.gatewayBaseUrl()
                : err
            gwMsg.ok = err.length === 0
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            Layout.fillWidth: true
            title: (App.langV, App.l("settings.title"))
        }

        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: scroll.availableWidth

            Item {
                width: scroll.availableWidth
                implicitHeight: col.implicitHeight + 48

                ColumnLayout {
                    id: col
                    anchors { left: parent.left; right: parent.right; top: parent.top }
                    anchors.margins: 24
                    spacing: 28

                    // ── Appearance ───────────────────────────────────────────
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Text {
                            text: (App.langV, App.l("settings.appearance")).toUpperCase()
                            color: Theme.accent
                            font.pixelSize: 11
                            font.bold: true
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            color: Theme.surfaceBg
                            border.color: Theme.borderColor
                            radius: 10
                            implicitHeight: themeInner.implicitHeight + 32

                            ColumnLayout {
                                id: themeInner
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                                spacing: 14

                                Text {
                                    text: (App.langV, App.l("settings.theme"))
                                    color: Theme.textPrimary
                                    font.pixelSize: 14
                                    font.bold: true
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10

                                    Repeater {
                                        model: [
                                            { key: "dark",  labelKey: "settings.dark",  bg: "#1e1e2e", dotColor: "#313244" },
                                            { key: "light", labelKey: "settings.light", bg: "#eff1f5", dotColor: "#ccd0da" },
                                            { key: "oled",  labelKey: "settings.oled",  bg: "#000000", dotColor: "#111111" },
                                        ]

                                        delegate: Rectangle {
                                            id: themeSwatch
                                            // Color de los 3 puntos de muestra; el Repeater interno
                                            // (model: 3) sombrea modelData con el índice, así que se
                                            // captura acá el dotColor del tema.
                                            property color dotColor: modelData.dotColor
                                            Layout.fillWidth: true
                                            height: 70
                                            radius: 8
                                            color: modelData.bg
                                            border.color: Theme.theme === modelData.key ? Theme.accent : Theme.divider
                                            border.width: Theme.theme === modelData.key ? 2 : 1

                                            Rectangle {
                                                visible: Theme.theme === modelData.key
                                                anchors { top: parent.top; right: parent.right; margins: 6 }
                                                width: 18; height: 18; radius: 9
                                                color: Theme.accent
                                                Text {
                                                    anchors.centerIn: parent
                                                    text: "✓"
                                                    color: "#000000"
                                                    font.pixelSize: 10
                                                    font.bold: true
                                                }
                                            }

                                            ColumnLayout {
                                                anchors.centerIn: parent
                                                spacing: 6

                                                Row {
                                                    Layout.alignment: Qt.AlignHCenter
                                                    spacing: 4
                                                    Repeater {
                                                        model: 3
                                                        Rectangle {
                                                            width: 7; height: 7; radius: 4
                                                            color: themeSwatch.dotColor
                                                        }
                                                    }
                                                }

                                                Text {
                                                    Layout.alignment: Qt.AlignHCenter
                                                    text: (App.langV, App.l(modelData.labelKey))
                                                    color: modelData.key === "light" ? "#4c4f69" : "#cdd6f4"
                                                    font.pixelSize: 12
                                                    font.bold: Theme.theme === modelData.key
                                                }
                                            }

                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: Theme.theme = modelData.key
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // ── Temas custom ─────────────────────────────────────────
                    ColumnLayout {
                        id: customSection
                        Layout.fillWidth: true
                        spacing: 10

                        property var list: Theme.customThemes
                        Connections {
                            target: Theme
                            function onCustomThemesChanged() { customSection.list = Theme.customThemes }
                        }

                        Text {
                            text: "TEMAS CUSTOM"
                            color: Theme.accent
                            font.pixelSize: 11
                            font.bold: true
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            color: Theme.surfaceBg
                            border.color: Theme.borderColor
                            radius: 10
                            implicitHeight: customInner.implicitHeight + 32

                            ColumnLayout {
                                id: customInner
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                                spacing: 12

                                Text {
                                    text: "Creá tus propios temas: elegí acento, fondo, primer plano, contraste y fuentes. Se guardan y los podés retomar cuando quieras."
                                    color: Theme.textSecondary
                                    font.pixelSize: 12
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }

                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: 3
                                    rowSpacing: 8
                                    columnSpacing: 8

                                    Repeater {
                                        model: customSection.list
                                        delegate: Rectangle {
                                            required property var modelData
                                            Layout.fillWidth: true
                                            height: 74
                                            radius: 8
                                            color: modelData.background || Theme.inputBg
                                            border.width: Theme.currentCustomId === modelData.id ? 2 : 1
                                            border.color: Theme.currentCustomId === modelData.id
                                                ? Theme.accent : Theme.divider

                                            ColumnLayout {
                                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
                                                spacing: 6
                                                RowLayout {
                                                    Layout.fillWidth: true
                                                    spacing: 6
                                                    Rectangle {
                                                        width: 14; height: 14; radius: 7
                                                        color: modelData.accent || "#888"
                                                        border.color: "#80000000"
                                                    }
                                                    Text {
                                                        text: modelData.name || "(sin nombre)"
                                                        color: modelData.foreground || Theme.textPrimary
                                                        font.pixelSize: 12
                                                        font.bold: true
                                                        elide: Text.ElideRight
                                                        Layout.fillWidth: true
                                                    }
                                                }
                                                RowLayout {
                                                    spacing: 6
                                                    Layout.fillWidth: true
                                                    LcButton {
                                                        text: "Editar"
                                                        secondary: true
                                                        implicitHeight: 26
                                                        onClicked: themeEditor.openEdit(modelData)
                                                    }
                                                    LcButton {
                                                        text: "✕"
                                                        danger: true
                                                        implicitHeight: 26
                                                        onClicked: Theme.deleteCustomTheme(modelData.id)
                                                    }
                                                }
                                            }

                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                z: -1
                                                onClicked: Theme.applyCustomTheme(modelData.id)
                                            }
                                        }
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        height: 74
                                        radius: 8
                                        color: Theme.inputBg
                                        border.color: Theme.divider
                                        border.width: 1
                                        Text {
                                            anchors.centerIn: parent
                                            text: "＋ Nuevo tema"
                                            color: Theme.textSecondary
                                            font.pixelSize: 13
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: themeEditor.openNew()
                                        }
                                    }
                                }

                                Text {
                                    visible: customSection.list.length === 0
                                    text: "Sin temas custom todavía."
                                    color: Theme.textMuted
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }

                    // ── Language ─────────────────────────────────────────────
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Text {
                            text: (App.langV, App.l("settings.language")).toUpperCase()
                            color: Theme.accent
                            font.pixelSize: 11
                            font.bold: true
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            color: Theme.surfaceBg
                            border.color: Theme.borderColor
                            radius: 10
                            implicitHeight: langInner.implicitHeight + 32

                            ColumnLayout {
                                id: langInner
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                                spacing: 14

                                Text {
                                    text: (App.langV, App.l("settings.language"))
                                    color: Theme.textPrimary
                                    font.pixelSize: 14
                                    font.bold: true
                                }

                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: 3
                                    rowSpacing: 8
                                    columnSpacing: 8

                                    Repeater {
                                        model: [
                                            { code: "es", label: "Español",  flag: "🇦🇷" },
                                            { code: "en", label: "English",  flag: "🇺🇸" },
                                            { code: "zh", label: "中文",       flag: "🇨🇳" },
                                            { code: "fr", label: "Français", flag: "🇫🇷" },
                                            { code: "it", label: "Italiano", flag: "🇮🇹" },
                                            { code: "de", label: "Deutsch",  flag: "🇩🇪" },
                                        ]

                                        delegate: Rectangle {
                                            Layout.fillWidth: true
                                            height: 48
                                            radius: 8
                                            color: App.language === modelData.code ? Theme.accent : Theme.inputBg
                                            border.color: App.language === modelData.code ? Theme.accent : Theme.borderColor
                                            border.width: 1

                                            Row {
                                                anchors.centerIn: parent
                                                spacing: 8
                                                Text { text: modelData.flag; font.pixelSize: 20 }
                                                Text {
                                                    text: modelData.label
                                                    color: App.language === modelData.code
                                                        ? Theme.btnPrimaryText : Theme.textPrimary
                                                    font.pixelSize: 13
                                                    font.bold: App.language === modelData.code
                                                    anchors.verticalCenter: parent.verticalCenter
                                                }
                                            }

                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: App.language = modelData.code
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // ── Sistema / bandeja ────────────────────────────────────
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Text { text: "GPU · INFERENCIA"; color: Theme.accent; font.pixelSize: 11; font.bold: true }

                        Rectangle {
                            Layout.fillWidth: true
                            color: Theme.surfaceBg; border.color: Theme.borderColor; radius: 10
                            implicitHeight: gpuInner.implicitHeight + 32

                            ColumnLayout {
                                id: gpuInner
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                                spacing: 12

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: "GPU de procesamiento"; color: Theme.textPrimary; font.pixelSize: 14; font.bold: true; Layout.fillWidth: true }
                                    ComboBox {
                                        id: processingGpu
                                        Layout.preferredWidth: 250
                                        model: ["Automática"].concat(root.gpuRows.map(function(g) { return "GPU " + g.index + " · " + g.name }))
                                        Component.onCompleted: {
                                            const configured = Number(App.readSetting("gpu/processingIndex", -1))
                                            currentIndex = configured >= 0 ? configured + 1 : 0
                                        }
                                        onActivated: App.writeSetting("gpu/processingIndex", currentIndex - 1)
                                    }
                                }
                                Text {
                                    text: "Se usa para operaciones principales y muestreo de llama.cpp ( --main-gpu )."
                                    color: Theme.textMuted; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true
                                }
                                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor }
                                Text { text: "GPUs de VRAM"; color: Theme.textPrimary; font.pixelSize: 14; font.bold: true }
                                Text {
                                    text: "Elegí qué GPUs reciben el modelo. Si no marcás ninguna, llama.cpp decide automáticamente."
                                    color: Theme.textMuted; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true
                                }
                                Repeater {
                                    model: root.gpuRows
                                    delegate: CheckBox {
                                        text: "GPU " + modelData.index + " · " + modelData.name + " · " + Math.round(modelData.totalMb / 1024) + " GB"
                                        checked: root.isVramGpuSelected(modelData.index)
                                        onToggled: root.setVramGpuSelected(modelData.index, checked)
                                        Layout.fillWidth: true
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        Layout.fillWidth: true
                                        text: root.gpuRows.length === 0 ? "No se detectaron GPUs NVIDIA o nvidia-smi no está disponible." : "Cambios aplicados al próximo inicio del servidor."
                                        color: Theme.textMuted; font.pixelSize: 11; wrapMode: Text.WordWrap
                                    }
                                    LcButton { text: "Actualizar"; secondary: true; onClicked: root.refreshGpuInventory() }
                                }
                            }
                        }
                    }

                    // ── Sistema / bandeja ────────────────────────────────────
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Text {
                            text: (App.langV, App.l("settings.system")).toUpperCase()
                            color: Theme.accent
                            font.pixelSize: 11
                            font.bold: true
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            color: Theme.surfaceBg
                            border.color: Theme.borderColor
                            radius: 10
                            implicitHeight: trayInner.implicitHeight + 32

                            ColumnLayout {
                                id: trayInner
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                                spacing: 14

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 12

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 3
                                        Text {
                                            text: (App.langV, App.l("settings.minimizeToTray"))
                                            color: Theme.textPrimary
                                            font.pixelSize: 14
                                            font.bold: true
                                        }
                                        Text {
                                            text: (App.langV, App.l("settings.minimizeToTrayDesc"))
                                            color: Theme.textMuted
                                            font.pixelSize: 11
                                            wrapMode: Text.WordWrap
                                            Layout.fillWidth: true
                                        }
                                    }

                                    LcSwitch {
                                        checked: App.readSetting("window/minimizeToTray", false)
                                        onToggled: {
                                            App.writeSetting("window/minimizeToTray", checked)
                                            if (Window.window)
                                                Window.window.minimizeToTray = checked
                                        }
                                    }
                                }

                                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 12

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 3
                                        Text {
                                            text: (App.langV, App.l("settings.startWithWindows"))
                                            color: Theme.textPrimary
                                            font.pixelSize: 14
                                            font.bold: true
                                        }
                                        Text {
                                            text: (App.langV, App.l("settings.startWithWindowsDesc"))
                                            color: Theme.textMuted
                                            font.pixelSize: 11
                                            wrapMode: Text.WordWrap
                                            Layout.fillWidth: true
                                        }
                                    }

                                    LcSwitch {
                                        checked: App.startWithWindowsEnabled()
                                        onToggled: {
                                            const error = App.setStartWithWindowsEnabled(checked)
                                            if (error.length > 0)
                                                checked = App.startWithWindowsEnabled()
                                        }
                                    }
                                }

                                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 12

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 3
                                        Text {
                                            text: "Iniciar el agente al abrir la app"
                                            color: Theme.textPrimary
                                            font.pixelSize: 14
                                            font.bold: true
                                        }
                                        Text {
                                            text: "Levanta el server y el agente del último perfil al arrancar, así las Tasks programadas (cron) disparan a horario sin esperar la carga del modelo."
                                            color: Theme.textMuted
                                            font.pixelSize: 11
                                            wrapMode: Text.WordWrap
                                            Layout.fillWidth: true
                                        }
                                    }

                                    LcSwitch {
                                        checked: App.autoStartAgentOnLaunch
                                        onToggled: App.autoStartAgentOnLaunch = checked
                                    }
                                }

                                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 12

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 3
                                        Text {
                                            text: "Auto-stop por inactividad"
                                            color: Theme.textPrimary
                                            font.pixelSize: 14
                                            font.bold: true
                                        }
                                        Text {
                                            text: "Descarga el modelo y libera VRAM tras N minutos sin chat/agente/API. 0 = nunca."
                                            color: Theme.textMuted
                                            font.pixelSize: 11
                                            wrapMode: Text.WordWrap
                                            Layout.fillWidth: true
                                        }
                                    }

                                    SpinBox {
                                        from: 0; to: 1440; stepSize: 5
                                        value: App.idleAutoStopMin
                                        onValueModified: App.idleAutoStopMin = value
                                        textFromValue: function(v) { return v === 0 ? "off" : v + " min" }
                                    }
                                }
                            }
                        }
                    }

                    // ── Gateway / API (OpenAI + Anthropic + auto-load) ───────
                    ColumnLayout {
                        id: gatewaySection
                        Layout.fillWidth: true
                        spacing: 10

                        Text {
                            text: "GATEWAY · API"
                            color: Theme.accent
                            font.pixelSize: 11
                            font.bold: true
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            color: Theme.surfaceBg
                            border.color: Theme.borderColor
                            radius: 10
                            implicitHeight: gwInner.implicitHeight + 32

                            ColumnLayout {
                                id: gwInner
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                                spacing: 12

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 12
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 3
                                        Text {
                                            text: "Gateway de modelos"
                                            color: Theme.textPrimary
                                            font.pixelSize: 14
                                            font.bold: true
                                        }
                                        Text {
                                            text: "API local para OpenCode y Claude Code con auto-load: cada request puede cargar el perfil pedido. " + (App.gatewayRunning ? ("Activo en " + App.gatewayBaseUrl()) : "Apagado.")
                                            color: App.gatewayRunning ? Theme.accent : Theme.textMuted
                                            font.pixelSize: 11
                                            wrapMode: Text.WordWrap
                                            Layout.fillWidth: true
                                        }
                                    }
                                    LcSwitch {
                                        checked: App.gatewayEnabled
                                        onToggled: App.gatewayEnabled = checked
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    visible: App.gatewayEnabled
                                    spacing: 8
                                    Text { text: "Puerto"; color: Theme.textSecondary; font.pixelSize: 12; Layout.preferredWidth: 90 }
                                    SpinBox {
                                        from: 1024; to: 65535
                                        value: App.gatewayPort
                                        editable: true
                                        onValueModified: App.gatewayPort = value
                                    }
                                    Text { text: "keep hot"; color: Theme.textSecondary; font.pixelSize: 12 }
                                    SpinBox {
                                        from: 1; to: 8
                                        value: App.gatewayKeepN
                                        onValueModified: App.gatewayKeepN = value
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    visible: App.gatewayEnabled
                                    spacing: 8
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Text { text: "Auto-load del modelo pedido"; color: Theme.textPrimary; font.pixelSize: 13 }
                                        Text {
                                            text: "Si el request nombra otro modelo, lo carga (swap del activo)."
                                            color: Theme.textMuted; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true
                                        }
                                    }
                                    LcSwitch {
                                        checked: App.gatewayAutoSwap
                                        onToggled: App.gatewayAutoSwap = checked
                                    }
                                }

                                LcTextField {
                                    Layout.fillWidth: true
                                    visible: App.gatewayEnabled
                                    placeholderText: "API key (opcional, para exponer en LAN)"
                                    text: App.gatewayApiKey
                                    echoMode: TextInput.Password
                                    onEditingFinished: App.gatewayApiKey = text
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    visible: App.gatewayEnabled
                                    spacing: 8
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Text {
                                            text: "Compartir server en la red local (LAN)"
                                            color: Theme.textPrimary
                                            font.pixelSize: 13
                                        }
                                        Text {
                                            text: App.gatewayLanEnabled
                                                ? (App.gatewayLanBaseUrl().length > 0
                                                   ? "Escuchando para otros dispositivos en " + App.gatewayLanBaseUrl()
                                                   : "No se encontró una dirección IPv4 de red local.")
                                                : "Sólo esta PC puede conectarse. Al activar LAN se genera una API key si está vacía."
                                            color: App.gatewayLanEnabled ? Theme.accent : Theme.textMuted
                                            font.pixelSize: 11
                                            wrapMode: Text.WordWrap
                                            Layout.fillWidth: true
                                        }
                                    }
                                    LcSwitch {
                                        checked: App.gatewayLanEnabled
                                        onToggled: App.gatewayLanEnabled = checked
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    visible: App.gatewayEnabled && App.gatewayLanEnabled
                                    spacing: 8

                                    Text {
                                        text: "OPCIÓN 1 · OTRO LLAMACODE EN LAN"
                                        color: Theme.accent
                                        font.pixelSize: 11
                                        font.bold: true
                                    }
                                    Text {
                                        text: "En el otro LlamaCode creá un Backend tipo Cloud/OpenAI-compatible. Base URL: "
                                              + App.gatewayLanBaseUrl()
                                              + " · Modelo: el ID del perfil elegido abajo · Var de key: LLAMACODE_GATEWAY_API_KEY."
                                        color: Theme.textSecondary
                                        font.pixelSize: 11
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                    }
                                    RowLayout {
                                        spacing: 8
                                        LcButton {
                                            text: "Copiar URL para LlamaCode"
                                            onClicked: {
                                                App.copyToClipboard(App.gatewayLanBaseUrl())
                                                gwMsg.text = "URL LAN copiada."
                                                gwMsg.ok = true
                                            }
                                        }
                                        LcButton {
                                            text: "Copiar API key"
                                            secondary: true
                                            onClicked: {
                                                App.copyToClipboard(App.gatewayApiKey)
                                                gwMsg.text = "API key copiada. Compartila sólo con dispositivos confiables."
                                                gwMsg.ok = true
                                            }
                                        }
                                    }

                                    Text {
                                        text: "OPCIÓN 2 · OPENCODE EN LAN"
                                        color: Theme.accent
                                        font.pixelSize: 11
                                        font.bold: true
                                    }
                                    Text {
                                        text: "Copiá la configuración en opencode.json del otro dispositivo. Incluye la URL LAN, los perfiles disponibles y la API key."
                                        color: Theme.textSecondary
                                        font.pixelSize: 11
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                    }
                                    LcButton {
                                        text: "Copiar configuración de OpenCode"
                                        enabled: openCodeProfile.currentValue !== undefined
                                        onClicked: {
                                            App.copyToClipboard(App.gatewayLanOpenCodeConfig(openCodeProfile.currentValue ?? ""))
                                            gwMsg.text = "Configuración LAN de OpenCode copiada."
                                            gwMsg.ok = true
                                        }
                                    }
                                    Text {
                                        text: "Windows puede pedir permiso de firewall la primera vez. Permití acceso sólo en redes privadas."
                                        color: Theme.textMuted
                                        font.pixelSize: 10
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                    }
                                }

                                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor; visible: App.gatewayEnabled }

                                RowLayout {
                                    Layout.fillWidth: true
                                    visible: App.gatewayEnabled
                                    spacing: 8
                                    ComboBox {
                                        id: openCodeProfile
                                        Layout.fillWidth: true
                                        property var launchMenu: App.profileManager.launchProfilesForMenu()
                                        model: launchMenu
                                        textRole: "displayName"
                                        valueRole: "id"
                                        Component.onCompleted: {
                                            var idx = indexOfValue(App.activeLaunchId)
                                            currentIndex = idx >= 0 ? idx : 0
                                        }
                                        Connections {
                                            target: App.profileManager
                                            function onLaunchesChanged() {
                                                openCodeProfile.launchMenu =
                                                    App.profileManager.launchProfilesForMenu()
                                            }
                                        }
                                    }
                                    LcButton {
                                        text: "Lanzar Claude Code en mi GPU"
                                        onClicked: {
                                            var err = App.launchClaudeCode()
                                            gwMsg.text = err.length === 0
                                                ? "Claude Code lanzado contra " + App.gatewayBaseUrl()
                                                : err
                                            gwMsg.ok = err.length === 0
                                        }
                                    }
                                    LcButton {
                                        text: "Abrir OpenCode GUI en mi GPU"
                                        enabled: openCodeProfile.currentValue !== undefined
                                        onClicked: openCodeProjectDialog.open()
                                    }
                                }
                                Text {
                                    id: gwMsg
                                    property bool ok: false
                                    visible: text.length > 0
                                    text: ""
                                    color: ok ? Theme.accent : Theme.btnDangerBg
                                    font.pixelSize: 11
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                                Text {
                                    visible: App.gatewayEnabled
                                    text: "Manual: OpenAI base URL " + App.gatewayBaseUrl() + "/v1  ·  endpoints /v1/models, /v1/chat/completions y /v1/messages."
                                    color: Theme.textMuted
                                    font.pixelSize: 11
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }

                    // ── Chat / Mermaid ───────────────────────────────────────
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Text {
                            text: "CHAT"
                            color: Theme.accent
                            font.pixelSize: 11
                            font.bold: true
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            color: Theme.surfaceBg
                            border.color: Theme.borderColor
                            radius: 10
                            implicitHeight: mermaidInner.implicitHeight + 32

                            RowLayout {
                                id: mermaidInner
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                                spacing: 12

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 3
                                    Text {
                                        text: "Diagramas Mermaid"
                                        color: Theme.textPrimary
                                        font.pixelSize: 14
                                        font.bold: true
                                    }
                                    Text {
                                        text: Mermaid.available
                                            ? "Renderiza bloques ```mermaid del chat como imagen."
                                            : "Requiere mermaid-cli (npm i -g @mermaid-js/mermaid-cli)."
                                        color: Theme.textMuted
                                        font.pixelSize: 11
                                        wrapMode: Text.WordWrap
                                        Layout.fillWidth: true
                                    }
                                }

                                LcSwitch {
                                    checked: App.mermaidEnabled
                                    enabled: Mermaid.available
                                    onToggled: App.mermaidEnabled = checked
                                }
                            }
                        }

                        // ── Automatización de browser (MCP Playwright) ───────
                        Rectangle {
                            Layout.fillWidth: true
                            color: Theme.surfaceBg
                            border.color: Theme.borderColor
                            radius: 10
                            implicitHeight: browserInner.implicitHeight + 32

                            ColumnLayout {
                                id: browserInner
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                                spacing: 12

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 12
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 3
                                        Text {
                                            text: "Automatización de browser"
                                            color: Theme.textPrimary
                                            font.pixelSize: 14
                                            font.bold: true
                                        }
                                        Text {
                                            text: "Da al agente tools de navegador vía MCP Playwright " +
                                                  "(navegar, click, escribir, snapshot). Requiere Node/npx. " +
                                                  "Override on/off por perfil en Perfiles."
                                            color: Theme.textMuted
                                            font.pixelSize: 11
                                            wrapMode: Text.WordWrap
                                            Layout.fillWidth: true
                                        }
                                    }
                                    LcSwitch {
                                        checked: App.browserAutomationEnabled
                                        onToggled: App.browserAutomationEnabled = checked
                                    }
                                }

                                LcTextField {
                                    Layout.fillWidth: true
                                    visible: App.browserAutomationEnabled
                                    placeholderText: "Comando del server MCP"
                                    text: App.browserMcpCommand
                                    onEditingFinished: App.browserMcpCommand = text
                                }

                            }
                        }
                    }

                    // ── GPU power limit (nvidia-smi) ─────────────────────────
                    ColumnLayout {
                        id: gpuSection
                        Layout.fillWidth: true
                        spacing: 10

                        property var info: ({ available: false, gpus: [] })
                        property string msg: ""
                        property bool msgOk: false
                        function reload() { info = App.gpuPowerInfo() }
                        Component.onCompleted: reload()

                        Text {
                            text: "GPU · POWER LIMIT"
                            color: Theme.accent
                            font.pixelSize: 11
                            font.bold: true
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            color: Theme.surfaceBg
                            border.color: Theme.borderColor
                            radius: 10
                            implicitHeight: gpuInner.implicitHeight + 32

                            ColumnLayout {
                                id: gpuPowerInner
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                                spacing: 12

                                Text {
                                    text: "Límite de potencia de la GPU (W) vía nvidia-smi. Bajarlo reduce consumo, temperatura y ruido con poca pérdida en inferencia. Se aplica al iniciar el server. Requiere admin (UAC)."
                                    color: Theme.textSecondary
                                    font.pixelSize: 12
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }

                                Text {
                                    visible: !gpuSection.info.available
                                    text: "No se detectó nvidia-smi (¿GPU NVIDIA / drivers instalados?)."
                                    color: Theme.textMuted
                                    font.pixelSize: 12
                                }

                                Repeater {
                                    model: gpuSection.info.available ? gpuSection.info.gpus : []
                                    delegate: ColumnLayout {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        spacing: 8

                                        Text {
                                            text: "GPU " + modelData.index + " · " + modelData.name
                                            color: Theme.textPrimary
                                            font.pixelSize: 13
                                            font.bold: true
                                        }
                                        Text {
                                            text: "actual " + Math.round(modelData.currentW) + " W · default " + Math.round(modelData.defaultW)
                                                  + " W · rango " + Math.round(modelData.minW) + "–" + Math.round(modelData.maxW) + " W"
                                            color: Theme.textMuted
                                            font.pixelSize: 11
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 10
                                            Slider {
                                                id: plSlider
                                                Layout.fillWidth: true
                                                from: modelData.minW
                                                to: modelData.maxW
                                                stepSize: 5
                                                value: modelData.currentW
                                            }
                                            Text {
                                                text: Math.round(plSlider.value) + " W"
                                                color: Theme.textPrimary
                                                font.pixelSize: 13
                                                font.bold: true
                                                Layout.preferredWidth: 60
                                            }
                                            LcButton {
                                                text: "Aplicar"
                                                onClicked: {
                                                    var err = App.setGpuPowerLimit(Math.round(plSlider.value), modelData.index)
                                                    gpuSection.msgOk = (err === "")
                                                    gpuSection.msg = (err === "")
                                                        ? ("Power limit fijado en " + Math.round(plSlider.value) + " W.")
                                                        : err
                                                    gpuSection.reload()
                                                }
                                            }
                                            LcButton {
                                                text: "Default"
                                                secondary: true
                                                onClicked: {
                                                    var err = App.setGpuPowerLimit(Math.round(modelData.defaultW), modelData.index)
                                                    gpuSection.msgOk = (err === "")
                                                    gpuSection.msg = (err === "") ? "Restaurado al default." : err
                                                    gpuSection.reload()
                                                }
                                            }
                                        }
                                    }
                                }

                                Text {
                                    visible: gpuSection.msg.length > 0
                                    text: gpuSection.msg
                                    color: gpuSection.msgOk ? Theme.accent : Theme.btnDangerBg
                                    font.pixelSize: 11
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }

                    // ── Integrations ─────────────────────────────────────────
                    ColumnLayout {
                        id: intgSection
                        Layout.fillWidth: true
                        spacing: 10

                        property var items: []
                        property string testMsgId: ""
                        property string testMsg: ""
                        property bool testOk: false
                        function reload() { items = App.integrations() }
                        Component.onCompleted: reload()

                        Connections {
                            target: App
                            function onIntegrationsChanged() { intgSection.reload() }
                            function onIntegrationTestResult(id, ok, message) {
                                intgSection.testMsgId = id; intgSection.testOk = ok; intgSection.testMsg = message
                            }
                        }

                        Text {
                            text: "INTEGRATIONS"
                            color: Theme.accent
                            font.pixelSize: 11
                            font.bold: true
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            color: Theme.surfaceBg
                            border.color: Theme.borderColor
                            radius: 10
                            implicitHeight: intgInner.implicitHeight + 32

                            ColumnLayout {
                                id: intgInner
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                                spacing: 14

                                Text {
                                    text: "Conexiones a servicios externos en un solo lugar (MCP + API)."
                                    color: Theme.textSecondary
                                    font.pixelSize: 12
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }

                                // Lista de integraciones
                                Text {
                                    visible: intgSection.items.length === 0
                                    text: "Sin integraciones configuradas."
                                    color: Theme.textMuted
                                    font.pixelSize: 12
                                }

                                Repeater {
                                    model: intgSection.items
                                    delegate: Rectangle {
                                        Layout.fillWidth: true
                                        radius: 8
                                        color: Theme.inputBg
                                        border.color: Theme.borderColor
                                        implicitHeight: rowCol.implicitHeight + 20

                                        ColumnLayout {
                                            id: rowCol
                                            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                                            spacing: 4

                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 8
                                                Text {
                                                    text: modelData.type === "mcp" ? "🧩" : "🔌"
                                                    font.pixelSize: 14
                                                }
                                                Text {
                                                    text: modelData.name
                                                    color: Theme.textPrimary
                                                    font.pixelSize: 13
                                                    font.bold: true
                                                }
                                                Rectangle {
                                                    radius: 4
                                                    color: Theme.surfaceBg
                                                    implicitWidth: tBadge.width + 12
                                                    implicitHeight: tBadge.height + 6
                                                    Text {
                                                        id: tBadge
                                                        anchors.centerIn: parent
                                                        text: modelData.type === "mcp" ? "MCP" : "API"
                                                        color: Theme.textSecondary
                                                        font.pixelSize: 10
                                                    }
                                                }
                                                Item { Layout.fillWidth: true }
                                                LcSwitch {
                                                    checked: modelData.enabled
                                                    onToggled: App.setIntegrationEnabled(modelData.id, checked)
                                                }
                                                LcButton {
                                                    text: "Test"
                                                    secondary: true
                                                    implicitHeight: 30
                                                    onClicked: App.testIntegration(modelData.id)
                                                }
                                                LcButton {
                                                    text: "✕"
                                                    danger: true
                                                    implicitHeight: 30
                                                    onClicked: App.removeIntegration(modelData.id)
                                                }
                                            }
                                            Text {
                                                text: modelData.summary
                                                color: Theme.textMuted
                                                font.pixelSize: 11
                                                elide: Text.ElideRight
                                                Layout.fillWidth: true
                                            }
                                            Text {
                                                visible: intgSection.testMsgId === modelData.id && intgSection.testMsg.length > 0
                                                text: intgSection.testMsg
                                                color: intgSection.testOk ? Theme.accent : Theme.btnDangerBg
                                                font.pixelSize: 11
                                                wrapMode: Text.WordWrap
                                                Layout.fillWidth: true
                                            }
                                        }
                                    }
                                }

                                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor }

                                // Add integration
                                Text {
                                    text: "Agregar integración"
                                    color: Theme.textPrimary
                                    font.pixelSize: 13
                                    font.bold: true
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Text { text: "Tipo"; color: Theme.textSecondary; font.pixelSize: 12 }
                                    LcComboBox {
                                        id: typeCombo
                                        Layout.preferredWidth: 200
                                        model: [
                                            { label: "MCP Tool Server", value: "mcp" },
                                            { label: "API Service",     value: "api_service" },
                                        ]
                                        textRole: "label"
                                        valueRole: "value"
                                    }
                                }

                                // Campos MCP
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    visible: typeCombo.currentValue === "mcp"
                                    LcTextField { id: mcpName; Layout.fillWidth: true; placeholderText: "Nombre (ej. filesystem)" }
                                    LcTextField { id: mcpCmd;  Layout.fillWidth: true; placeholderText: "Comando stdio (ej. npx -y @modelcontextprotocol/server-filesystem .)" }
                                    LcButton {
                                        text: "Agregar MCP"
                                        enabled: mcpName.text.trim().length > 0 && mcpCmd.text.trim().length > 0
                                        onClicked: {
                                            if (App.saveMcpIntegration(mcpName.text.trim(), "local", mcpCmd.text.trim())) {
                                                mcpName.text = ""; mcpCmd.text = ""
                                            }
                                        }
                                    }
                                }

                                // Campos API Service
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    visible: typeCombo.currentValue === "api_service"
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text { text: "Uso"; color: Theme.textSecondary; font.pixelSize: 12 }
                                        LcComboBox {
                                            id: apiProvider
                                            Layout.fillWidth: true
                                            model: [
                                                { label: "API genérica", value: "generic" },
                                                { label: "Camofox (web_fetch opcional)", value: "camofox" },
                                                { label: "CloakBrowser (externo/manual)", value: "cloakbrowser" }
                                            ]
                                            textRole: "label"
                                            valueRole: "value"
                                        }
                                    }
                                    LcTextField { id: apiName; Layout.fillWidth: true; placeholderText: "Nombre" }
                                    LcTextField {
                                        id: apiUrl
                                        Layout.fillWidth: true
                                        placeholderText: apiProvider.currentValue === "camofox"
                                                         ? "Base URL (ej. http://127.0.0.1:9377)"
                                                         : "Base URL (https://…)"
                                    }
                                    LcTextField { id: apiKey;  Layout.fillWidth: true; placeholderText: "API key (opcional)"; echoMode: TextInput.Password }
                                    Text {
                                        visible: apiProvider.currentValue === "cloakbrowser"
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        color: Theme.textMuted
                                        font.pixelSize: 11
                                        text: "Se guarda desactivado. LlamaCode no descarga su binario ni lo usa automáticamente; registralo sólo para documentar una instalación externa revisada."
                                    }
                                    LcButton {
                                        text: "Agregar API Service"
                                        enabled: apiName.text.trim().length > 0 && apiUrl.text.trim().length > 0
                                        onClicked: {
                                            if (App.saveApiService("", apiName.text.trim(), apiUrl.text.trim(),
                                                                   apiKey.text, true, apiProvider.currentValue)) {
                                                apiName.text = ""; apiUrl.text = ""; apiKey.text = ""
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // ── Perfiles de agente (capacidades + directivas) ────────
                    ColumnLayout {
                        id: agentProfilesSection
                        Layout.fillWidth: true
                        spacing: 10

                        // Copia de trabajo del perfil en edición. enabledTools/
                        // directives son arrays JS; "*" = todo (presets de sistema).
                        property var edit: ({ id: "", name: "", system: false,
                                              enabledTools: [], directives: [],
                                              approvalMode: "ask", thinking: false,
                                              temperature: -1, systemExtra: "",
                                              personalityProfileIds: [], styleProfileIds: [],
                                              injectStyleExamples: true, styleExampleLimit: 2,
                                              styleContextLimit: 6000,
                                              thinkingLeakGuard: false,
                                              progressCredits: 8, progressMaxCredits: 16,
                                              progressReplanAfter: 3, progressStopAfter: 5,
                                              quickToolTimeoutSec: 15 })
                        property var toolGroups: []     // agrupado por categoría
                        property var directiveItems: [] // catálogo de directivas
                        property int enabledCount: 0
                        property int enabledTokens: 0
                        property var styleEdit: ({ id: "", name: "", kind: "writing-style",
                                                   description: "", styleCard: "", examples: [] })
                        readonly property bool isSystem: edit.system === true

                        function allToolNames() {
                            var list = App.agentToolCatalog(), out = []
                            for (var i = 0; i < list.length; ++i) out.push(list[i].name)
                            return out
                        }
                        function allDirectiveKeys() {
                            var cat = App.agentDirectiveCatalog(), out = []
                            for (var i = 0; i < cat.length; ++i) out.push(cat[i].key)
                            return out
                        }
                        function isToolOn(name) {
                            return edit.enabledTools.indexOf("*") >= 0
                                || edit.enabledTools.indexOf(name) >= 0
                        }
                        function isDirOn(key) {
                            // honey es opt-in LITERAL: no lo implica el sentinel "*"
                            // (Máximo) — hay que nombrarlo. Coincide con el gateo del
                            // backend (buildSystemPrompt sólo lo activa por contains).
                            if (key === "honey") return edit.directives.indexOf("honey") >= 0
                            return edit.directives.indexOf("*") >= 0
                                || edit.directives.indexOf(key) >= 0
                        }
                        function selectStyleProfile(id) {
                            var p = App.profileManager.getPersonaStyleProfile(id)
                            if (!p || !p.id) return
                            styleEdit = { id: p.id, name: p.name, kind: p.kind || "writing-style",
                                          description: p.description || "", styleCard: p.styleCard || "",
                                          examples: (p.examples || []).slice() }
                            styleNameField.text = styleEdit.name
                            styleDescriptionField.text = styleEdit.description
                            styleCardField.text = styleEdit.styleCard
                            styleExamplesField.text = styleEdit.examples.length ? styleEdit.examples[0] : ""
                            var ki = styleKindCombo.indexOfValue(styleEdit.kind)
                            if (ki >= 0) styleKindCombo.currentIndex = ki
                        }
                        function saveStyleProfile() {
                            if (!styleEdit.id) return
                            App.profileManager.updatePersonaStyleProfile({
                                id: styleEdit.id, name: styleNameField.text.trim(), kind: styleKindCombo.currentValue,
                                description: styleDescriptionField.text, styleCard: styleCardField.text,
                                examples: styleExamplesField.text.trim().length ? [styleExamplesField.text] : []
                            })
                        }
                        function createStyleProfile() {
                            var id = App.profileManager.addPersonaStyleProfile("Nuevo estilo", "writing-style")
                            styleProfileSelector.currentIndex = styleProfileSelector.indexOfValue(id)
                            selectStyleProfile(id)
                        }
                        function setToolOn(name, on) {
                            var arr = edit.enabledTools.slice()
                            if (arr.indexOf("*") >= 0) arr = allToolNames()   // expandir antes de editar
                            var i = arr.indexOf(name)
                            if (on && i < 0) arr.push(name)
                            else if (!on && i >= 0) arr.splice(i, 1)
                            edit.enabledTools = arr
                            recount()
                        }
                        function setDirOn(key, on) {
                            var arr = edit.directives.slice()
                            // Expandir "*" SIN honey: no se debe activar al desmarcar
                            // otra directiva de un preset Máximo (honey = opt-in literal).
                            if (arr.indexOf("*") >= 0)
                                arr = allDirectiveKeys().filter(function(k){ return k !== "honey" })
                            var i = arr.indexOf(key)
                            if (on && i < 0) arr.push(key)
                            else if (!on && i >= 0) arr.splice(i, 1)
                            edit.directives = arr
                        }
                        function recount() {
                            var list = App.agentToolCatalog(), cnt = 0, tok = 0
                            for (var i = 0; i < list.length; ++i)
                                if (isToolOn(list[i].name)) { cnt += 1; tok += list[i].approxTokens }
                            enabledCount = cnt; enabledTokens = tok
                        }
                        function rebuildGroups() {
                            var list = App.agentToolCatalog()
                            var order = [], byGroup = {}
                            for (var i = 0; i < list.length; ++i) {
                                var t = list[i]
                                if (byGroup[t.group] === undefined) { byGroup[t.group] = []; order.push(t.group) }
                                byGroup[t.group].push(t)
                            }
                            var g = []
                            for (var k = 0; k < order.length; ++k)
                                g.push({ name: order[k], tools: byGroup[order[k]] })
                            toolGroups = g
                            directiveItems = App.agentDirectiveCatalog()
                            recount()
                        }
                        function selectProfile(id) {
                            var p = App.profileManager.getAgentProfile(id)
                            if (!p || !p.id) return
                            edit = { id: p.id, name: p.name, system: p.system === true,
                                     enabledTools: (p.enabledTools || []).slice(),
                                     directives: (p.directives || []).slice(),
                                     approvalMode: p.approvalMode || "ask",
                                     thinking: p.thinking === true,
                                     thinkingLeakGuard: p.thinkingLeakGuard === true,
                                     progressCredits: p.progressCredits || 8,
                                     progressMaxCredits: p.progressMaxCredits || 16,
                                     progressReplanAfter: p.progressReplanAfter || 3,
                                     progressStopAfter: p.progressStopAfter || 5,
                                     quickToolTimeoutSec: p.quickToolTimeoutSec || 15,
                                     temperature: (p.temperature === undefined ? -1 : p.temperature),
                                     systemExtra: p.systemExtra || "",
                                     personalityProfileIds: (p.personalityProfileIds || []).slice(),
                                     styleProfileIds: (p.styleProfileIds || []).slice(),
                                     injectStyleExamples: p.injectStyleExamples !== false,
                                     styleExampleLimit: p.styleExampleLimit || 2,
                                     styleContextLimit: p.styleContextLimit || 6000 }
                            apNameField.text = edit.name
                            extraField.text = edit.systemExtra
                            tempField.text = edit.temperature >= 0 ? String(edit.temperature) : ""
                            approvalCombo.currentIndex = Math.max(0, approvalCombo.indexOfValue(edit.approvalMode))
                            progressCreditsField.text = String(edit.progressCredits)
                            progressMaxField.text = String(edit.progressMaxCredits)
                            progressReplanField.text = String(edit.progressReplanAfter)
                            progressStopField.text = String(edit.progressStopAfter)
                            quickToolTimeoutField.text = String(edit.quickToolTimeoutSec)
                            examplesSwitch.checked = edit.injectStyleExamples !== false
                            styleExampleLimitField.text = String(edit.styleExampleLimit || 2)
                            styleContextLimitField.text = String(edit.styleContextLimit || 6000)
                            var pid = (edit.personalityProfileIds || []).length ? edit.personalityProfileIds[0] : ""
                            var sid = (edit.styleProfileIds || []).length ? edit.styleProfileIds[0] : ""
                            var pi = personalitySelector.indexOfValue(pid); if (pi >= 0) personalitySelector.currentIndex = pi
                            var si = styleSelector.indexOfValue(sid); if (si >= 0) styleSelector.currentIndex = si
                            rebuildGroups()  // recrea delegates → switches re-evaluan checked
                        }
                        function save() {
                            if (isSystem) return
                            App.profileManager.updateAgentProfile({
                                "id": edit.id,
                                "name": apNameField.text.trim().length ? apNameField.text.trim() : edit.name,
                                "enabledTools": edit.enabledTools,
                                "directives": edit.directives,
                                "approvalMode": approvalCombo.currentValue || "ask",
                                "thinking": thinkingSwitch.checked,
                                "thinkingLeakGuard": thinkingLeakGuardSwitch.checked,
                                "progressCredits": parseInt(progressCreditsField.text) || 8,
                                "progressMaxCredits": parseInt(progressMaxField.text) || 16,
                                "progressReplanAfter": parseInt(progressReplanField.text) || 3,
                                "progressStopAfter": parseInt(progressStopField.text) || 5,
                                "quickToolTimeoutSec": parseInt(quickToolTimeoutField.text) || 15,
                                "temperature": (tempField.text.trim().length && !isNaN(parseFloat(tempField.text)))
                                               ? parseFloat(tempField.text) : -1,
                                "systemExtra": extraField.text,
                                "personalityProfileIds": agentProfilesSection.edit.personalityProfileIds,
                                "styleProfileIds": agentProfilesSection.edit.styleProfileIds,
                                "injectStyleExamples": examplesSwitch.checked,
                                "styleExampleLimit": parseInt(styleExampleLimitField.text) || 2,
                                "styleContextLimit": parseInt(styleContextLimitField.text) || 6000
                            })
                            App.profileManager.saveProfiles()
                        }

                        Component.onCompleted: {
                            // Arrancar editando el perfil activo resuelto.
                            var id = App.activeAgentProfileId
                            var idx = profileSelector.indexOfValue(id)
                            if (idx >= 0) profileSelector.currentIndex = idx
                            selectProfile(id)
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: "PERFILES DE AGENTE"
                                color: Theme.accent
                                font.pixelSize: 11
                                font.bold: true
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: agentProfilesSection.enabledCount + " tools · ~"
                                      + agentProfilesSection.enabledTokens + " tok"
                                color: Theme.textMuted
                                font.pixelSize: 11
                            }
                        }

                        // Selector de perfil + acciones.
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            LcComboBox {
                                id: profileSelector
                                Layout.fillWidth: true
                                model: App.profileManager.agentProfiles
                                textRole: "name"; valueRole: "profileId"
                                onActivated: agentProfilesSection.selectProfile(currentValue)
                                background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                                contentItem: Text { text: profileSelector.displayText; color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
                            }
                            LcButton {
                                text: "Nuevo"
                                secondary: true
                                onClicked: {
                                    var id = App.profileManager.addAgentProfile("Nuevo perfil")
                                    var idx = profileSelector.indexOfValue(id)
                                    if (idx >= 0) profileSelector.currentIndex = idx
                                    agentProfilesSection.selectProfile(id)
                                }
                            }
                            LcButton {
                                text: "Duplicar"
                                secondary: true
                                enabled: agentProfilesSection.edit.id.length > 0
                                onClicked: {
                                    var id = App.profileManager.duplicateAgentProfile(agentProfilesSection.edit.id)
                                    if (!id) return
                                    var idx = profileSelector.indexOfValue(id)
                                    if (idx >= 0) profileSelector.currentIndex = idx
                                    agentProfilesSection.selectProfile(id)
                                }
                            }
                            LcButton {
                                text: "Eliminar"
                                danger: true
                                enabled: agentProfilesSection.edit.id.length > 0 && !agentProfilesSection.isSystem
                                onClicked: {
                                    App.profileManager.removeAgentProfile(agentProfilesSection.edit.id)
                                    profileSelector.currentIndex = 0
                                    agentProfilesSection.selectProfile(
                                        profileSelector.currentValue || App.activeAgentProfileId)
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            color: Theme.surfaceBg
                            border.color: Theme.borderColor
                            radius: 10
                            implicitHeight: profInner.implicitHeight + 32

                            ColumnLayout {
                                id: profInner
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                                spacing: 14

                                // Aviso para perfiles de sistema (solo lectura).
                                Rectangle {
                                    Layout.fillWidth: true
                                    visible: agentProfilesSection.isSystem
                                    color: Theme.inputBg
                                    border.color: Theme.warnText
                                    radius: 8
                                    implicitHeight: sysNote.implicitHeight + 16
                                    Text {
                                        id: sysNote
                                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 10 }
                                        text: "Perfil de sistema (solo lectura). Duplicalo para crear una copia editable."
                                        color: Theme.warnText
                                        font.pixelSize: 12
                                        wrapMode: Text.WordWrap
                                    }
                                }

                                // Ajustes generales del perfil.
                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: 2
                                    rowSpacing: 8
                                    columnSpacing: 10
                                    enabled: !agentProfilesSection.isSystem

                                    Text { text: "Nombre"; color: Theme.textSecondary; font.pixelSize: 12 }
                                    LcTextField { id: apNameField; Layout.fillWidth: true }

                                    Text { text: "Aprobación"; color: Theme.textSecondary; font.pixelSize: 12 }
                                    LcComboBox {
                                        id: approvalCombo
                                        Layout.fillWidth: true
                                        textRole: "label"; valueRole: "key"
                                        model: [
                                            { key: "auto",   label: "Aprobar todo" },
                                            { key: "ask",    label: "Pedir escritura" },
                                            { key: "manual", label: "Pedir todo" },
                                            { key: "super",  label: "Super Agente ⚠" },
                                            { key: "plan",   label: "Plan (solo lectura)" }
                                        ]
                                        background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                                        contentItem: Text { text: approvalCombo.displayText; color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
                                    }

                                    Text { text: "Razonar"; color: Theme.textSecondary; font.pixelSize: 12 }
                                    LcSwitch {
                                        id: thinkingSwitch
                                        checked: agentProfilesSection.edit.thinking
                                    }

                                    Text { text: "Compatibilidad thinking"; color: Theme.textSecondary; font.pixelSize: 12 }
                                    LcSwitch {
                                        id: thinkingLeakGuardSwitch
                                        checked: agentProfilesSection.edit.thinkingLeakGuard === true
                                        ToolTip.visible: hovered
                                        ToolTip.text: "Opt-in por perfil: no preserva razonamiento previo y corta colas repetidas después de un </think> huérfano. Dejalo apagado salvo que el modelo presente esa fuga."
                                    }

                                    Text { text: "Indicador de escritorio"; color: Theme.textSecondary; font.pixelSize: 12 }
                                    LcSwitch {
                                        checked: App.desktopIndicatorVisible
                                        onToggled: App.desktopIndicatorVisible = checked
                                        ToolTip.visible: hovered
                                        ToolTip.text: "Muestra una señal siempre visible mientras la IA controla mouse, teclado o ventanas."
                                    }

                                    Text { text: "Temperatura"; color: Theme.textSecondary; font.pixelSize: 12 }
                                    LcTextField { id: tempField; Layout.fillWidth: true; placeholderText: "vacío = heredar del modelo" }

                                    Text { text: "Créditos de progreso"; color: Theme.textSecondary; font.pixelSize: 12 }
                                    LcTextField { id: progressCreditsField; Layout.fillWidth: true; placeholderText: "8" }

                                    Text { text: "Máximo renovable"; color: Theme.textSecondary; font.pixelSize: 12 }
                                    LcTextField { id: progressMaxField; Layout.fillWidth: true; placeholderText: "16" }

                                    Text { text: "Replantear tras"; color: Theme.textSecondary; font.pixelSize: 12 }
                                    LcTextField { id: progressReplanField; Layout.fillWidth: true; placeholderText: "3 acciones sin progreso" }

                                    Text { text: "Cerrar tras replanteo"; color: Theme.textSecondary; font.pixelSize: 12 }
                                    LcTextField { id: progressStopField; Layout.fillWidth: true; placeholderText: "5 acciones sin progreso" }

                                    Text { text: "Timeout tool rápida (s)"; color: Theme.textSecondary; font.pixelSize: 12 }
                                    LcTextField { id: quickToolTimeoutField; Layout.fillWidth: true; placeholderText: "15" }

                                    Text { text: "Instrucciones extra"; color: Theme.textSecondary; font.pixelSize: 12 }
                                    LcTextField { id: extraField; Layout.fillWidth: true; placeholderText: "opcional, se añade al system prompt" }

                                    Text { text: "Personalidad"; color: Theme.textSecondary; font.pixelSize: 12 }
                                    LcComboBox {
                                        id: personalitySelector; Layout.fillWidth: true
                                        model: App.profileManager.personalityProfiles; textRole: "name"; valueRole: "profileId"
                                        onActivated: agentProfilesSection.edit.personalityProfileIds = currentValue ? [currentValue] : []
                                        background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                                        contentItem: Text { text: personalitySelector.displayText || "Ninguna"; color: Theme.textPrimary; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
                                    }

                                    Text { text: "Estilo de escritura"; color: Theme.textSecondary; font.pixelSize: 12 }
                                    LcComboBox {
                                        id: styleSelector; Layout.fillWidth: true
                                        model: App.profileManager.writingStyleProfiles; textRole: "name"; valueRole: "profileId"
                                        onActivated: agentProfilesSection.edit.styleProfileIds = currentValue ? [currentValue] : []
                                        background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                                        contentItem: Text { text: styleSelector.displayText || "Ninguno"; color: Theme.textPrimary; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
                                    }

                                    Text { text: "Inyectar ejemplos"; color: Theme.textSecondary; font.pixelSize: 12 }
                                    LcSwitch { id: examplesSwitch; checked: agentProfilesSection.edit.injectStyleExamples !== false }

                                    Text { text: "Máximo de ejemplos"; color: Theme.textSecondary; font.pixelSize: 12 }
                                    LcTextField { id: styleExampleLimitField; Layout.fillWidth: true; text: "2"; placeholderText: "2" }

                                    Text { text: "Límite de contexto de estilo"; color: Theme.textSecondary; font.pixelSize: 12 }
                                    LcTextField { id: styleContextLimitField; Layout.fillWidth: true; text: "6000"; placeholderText: "6000 caracteres" }
                                }

                                Text { text: "PERFILES DE PERSONALIDAD Y ESTILO"; color: Theme.accent; font.pixelSize: 11; font.bold: true }
                                RowLayout {
                                    Layout.fillWidth: true; spacing: 8
                                    LcComboBox {
                                        id: styleProfileSelector; Layout.fillWidth: true
                                        model: App.profileManager.personaStyleProfiles; textRole: "name"; valueRole: "profileId"
                                        onActivated: agentProfilesSection.selectStyleProfile(currentValue)
                                        background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                                        contentItem: Text { text: styleProfileSelector.displayText || "Seleccioná un perfil"; color: Theme.textPrimary; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
                                    }
                                    LcButton { text: "Nuevo"; secondary: true; onClicked: agentProfilesSection.createStyleProfile() }
                                    LcButton { text: "Guardar estilo"; secondary: true; enabled: !!agentProfilesSection.styleEdit.id; onClicked: agentProfilesSection.saveStyleProfile() }
                                    LcButton { text: "Exportar JSON"; secondary: true; enabled: !!agentProfilesSection.styleEdit.id; onClicked: styleJsonField.text = App.profileManager.exportPersonaStyleProfile(agentProfilesSection.styleEdit.id) }
                                    LcButton { text: "Importar JSON"; secondary: true; enabled: styleJsonField.text.trim().length > 0; onClicked: { var id = App.profileManager.importPersonaStyleProfile(styleJsonField.text); if (id) { styleProfileSelector.currentIndex = styleProfileSelector.indexOfValue(id); agentProfilesSection.selectStyleProfile(id) } } }
                                }
                                GridLayout {
                                    Layout.fillWidth: true; columns: 2; columnSpacing: 10; rowSpacing: 8
                                    enabled: !!agentProfilesSection.styleEdit.id && !agentProfilesSection.isSystem
                                    Text { text: "Nombre"; color: Theme.textSecondary; font.pixelSize: 12 }
                                    LcTextField { id: styleNameField; Layout.fillWidth: true }
                                    Text { text: "Tipo"; color: Theme.textSecondary; font.pixelSize: 12 }
                                    LcComboBox {
                                        id: styleKindCombo; Layout.fillWidth: true; textRole: "label"; valueRole: "value"
                                        model: [{label: "Estilo de escritura", value: "writing-style"}, {label: "Personalidad", value: "personality"}]
                                        background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                                        contentItem: Text { text: styleKindCombo.displayText; color: Theme.textPrimary; leftPadding: 10; verticalAlignment: Text.AlignVCenter }
                                    }
                                    Text { text: "Descripción"; color: Theme.textSecondary; font.pixelSize: 12 }
                                    LcTextField { id: styleDescriptionField; Layout.fillWidth: true; placeholderText: "Para qué conviene usarlo" }
                                    Text { text: "Ficha"; color: Theme.textSecondary; font.pixelSize: 12 }
                                    TextArea { id: styleCardField; Layout.fillWidth: true; Layout.minimumHeight: 72; wrapMode: TextArea.Wrap; color: Theme.textPrimary; placeholderText: "Tono, ritmo, vocabulario, preferencias y cosas a evitar" }
                                    Text { text: "Muestra"; color: Theme.textSecondary; font.pixelSize: 12 }
                                    TextArea { id: styleExamplesField; Layout.fillWidth: true; Layout.minimumHeight: 100; wrapMode: TextArea.Wrap; color: Theme.textPrimary; placeholderText: "Pegá un ejemplo. Se usa sólo como referencia y queda local." }
                                }
                                TextArea {
                                    id: styleJsonField; Layout.fillWidth: true; Layout.minimumHeight: 70
                                    wrapMode: TextArea.WrapAnywhere; color: Theme.textPrimary
                                    placeholderText: "JSON para importar/exportar (queda local)"
                                }
                                RowLayout {
                                    Layout.fillWidth: true; spacing: 8
                                    LcButton {
                                        text: "Completar ficha desde muestra"; secondary: true
                                        enabled: styleExamplesField.text.trim().length > 0 && !!agentProfilesSection.styleEdit.id
                                        onClicked: styleCardField.text = App.profileManager.heuristicStyleCard(styleExamplesField.text)
                                    }
                                    LcButton {
                                        text: "Analizar con modelo"; secondary: true
                                        enabled: styleExamplesField.text.trim().length > 0 && !!agentProfilesSection.styleEdit.id && App.personaStyleAnalysisStatus !== "running"
                                        onClicked: App.analyzePersonaStyleProfile(agentProfilesSection.styleEdit.id, styleExamplesField.text)
                                    }
                                    Text {
                                        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textMuted; font.pixelSize: 11
                                        text: App.personaStyleAnalysisStatus === "running" ? "Analizando…" : (App.personaStyleAnalysisError || "Revisá la ficha antes de guardarla.")
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textMuted; font.pixelSize: 11
                                    text: "La ficha se inyecta como preferencia de expresión. No modifica permisos, herramientas ni guardrails. Podés generarla localmente o analizar la muestra con el modelo activo; revisala antes de guardarla."
                                }

                                // Guardrail global (no per-perfil): acciones destructivas
                                // requieren aprobación humana aun en modo auto. En "Super
                                // Agente" no aplica (autonomía total).
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Text { text: "Guardrail de acciones destructivas"; color: Theme.textPrimary; font.pixelSize: 13 }
                                        Text {
                                            text: "Fuerza aprobación humana antes de acciones irreversibles (borrado recursivo, format, git push --force, DROP de DB, borrar memoria, clicks de escritorio delete/eliminar), incluso en modo automático y en sub-agentes. No aplica en Super Agente."
                                            color: Theme.textMuted; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true
                                        }
                                    }
                                    LcSwitch {
                                        checked: App.hitlDestructive
                                        onToggled: App.hitlDestructive = checked
                                    }
                                }

                                // ── Directivas ──
                                Text {
                                    text: "DIRECTIVAS (system prompt)"
                                    color: Theme.accent
                                    font.pixelSize: 11
                                    font.bold: true
                                }
                                Repeater {
                                    model: agentProfilesSection.directiveItems
                                    delegate: Rectangle {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        radius: 8
                                        color: Theme.inputBg
                                        border.color: Theme.borderColor
                                        implicitHeight: dRow.implicitHeight + 16
                                        opacity: agentProfilesSection.isSystem ? 0.6 : 1.0

                                        RowLayout {
                                            id: dRow
                                            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 10 }
                                            spacing: 8
                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 2
                                                Text {
                                                    text: modelData.name
                                                    color: Theme.textPrimary
                                                    font.pixelSize: 13
                                                    font.bold: true
                                                }
                                                Text {
                                                    text: modelData.description
                                                    color: Theme.textMuted
                                                    font.pixelSize: 11
                                                    wrapMode: Text.WordWrap
                                                    Layout.fillWidth: true
                                                }
                                            }
                                            LcSwitch {
                                                enabled: !agentProfilesSection.isSystem
                                                checked: agentProfilesSection.isDirOn(modelData.key)
                                                onToggled: agentProfilesSection.setDirOn(modelData.key, checked)
                                            }
                                        }
                                    }
                                }

                                // ── Capacidades (tools) ──
                                Text {
                                    text: "CAPACIDADES (tools)"
                                    color: Theme.accent
                                    font.pixelSize: 11
                                    font.bold: true
                                }
                                Text {
                                    text: "Apagar las que no usás ahorra contexto (clave en modelos locales chicos)."
                                    color: Theme.textSecondary
                                    font.pixelSize: 12
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }
                                Repeater {
                                    model: agentProfilesSection.toolGroups
                                    delegate: ColumnLayout {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        spacing: 6
                                        Text {
                                            text: modelData.name
                                            color: Theme.textPrimary
                                            font.pixelSize: 13
                                            font.bold: true
                                        }
                                        Repeater {
                                            model: modelData.tools
                                            delegate: Rectangle {
                                                required property var modelData
                                                Layout.fillWidth: true
                                                radius: 8
                                                color: Theme.inputBg
                                                border.color: Theme.borderColor
                                                implicitHeight: tRow.implicitHeight + 16
                                                opacity: agentProfilesSection.isSystem ? 0.6 : 1.0

                                                RowLayout {
                                                    id: tRow
                                                    anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 10 }
                                                    spacing: 8
                                                    ColumnLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 2
                                                        RowLayout {
                                                            spacing: 8
                                                            Text {
                                                                text: modelData.name
                                                                color: Theme.textPrimary
                                                                font.pixelSize: 13
                                                                font.bold: true
                                                                font.family: Theme.codeFont
                                                            }
                                                            Text {
                                                                text: "~" + modelData.approxTokens
                                                                color: Theme.textMuted
                                                                font.pixelSize: 10
                                                            }
                                                        }
                                                        Text {
                                                            text: modelData.description
                                                            color: Theme.textMuted
                                                            font.pixelSize: 11
                                                            elide: Text.ElideRight
                                                            Layout.fillWidth: true
                                                        }
                                                    }
                                                    LcSwitch {
                                                        enabled: !agentProfilesSection.isSystem
                                                        checked: agentProfilesSection.isToolOn(modelData.name)
                                                        onToggled: agentProfilesSection.setToolOn(modelData.name, checked)
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                LcButton {
                                    text: "Guardar perfil"
                                    Layout.alignment: Qt.AlignRight
                                    enabled: !agentProfilesSection.isSystem && agentProfilesSection.edit.id.length > 0
                                    onClicked: agentProfilesSection.save()
                                }
                            }
                        }
                    }

                    // ── Modelo maestro (ask_teacher) ─────────────────────────
                    ColumnLayout {
                        id: teacherSection
                        Layout.fillWidth: true
                        spacing: 10

                        Text {
                            text: "MODELO MAESTRO (ask_teacher)"
                            color: Theme.accent
                            font.pixelSize: 11
                            font.bold: true
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            color: Theme.surfaceBg
                            border.color: Theme.borderColor
                            radius: 10
                            implicitHeight: teacherInner.implicitHeight + 32

                            ColumnLayout {
                                id: teacherInner
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                                spacing: 12

                                Text {
                                    text: "Endpoint OpenAI-compatible de un modelo más capaz. La tool ask_teacher lo consulta para sub-problemas difíciles. Vacío = se usan las env vars LLAMACODE_TEACHER_*."
                                    color: Theme.textSecondary
                                    font.pixelSize: 12
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }

                                Text { text: "Base URL"; color: Theme.textSecondary; font.pixelSize: 12 }
                                LcTextField {
                                    Layout.fillWidth: true
                                    text: App.agentTeacherUrl
                                    placeholderText: "https://api.openai.com  ·  http://localhost:8081"
                                    onEditingFinished: App.agentTeacherUrl = text
                                }

                                Text { text: "Modelo"; color: Theme.textSecondary; font.pixelSize: 12 }
                                LcTextField {
                                    Layout.fillWidth: true
                                    text: App.agentTeacherModel
                                    placeholderText: "gpt-4o  ·  qwen2.5-coder-32b  ·  default"
                                    onEditingFinished: App.agentTeacherModel = text
                                }

                                Text { text: "API key (opcional)"; color: Theme.textSecondary; font.pixelSize: 12 }
                                LcTextField {
                                    Layout.fillWidth: true
                                    text: App.agentTeacherKey
                                    placeholderText: "sk-…  (vacío para servers locales)"
                                    echoMode: TextInput.Password
                                    onEditingFinished: App.agentTeacherKey = text
                                }
                            }
                        }
                    }

                    // ── Correo (email_send / email_list / email_read) ───────
                    ColumnLayout {
                        id: mailSection
                        Layout.fillWidth: true
                        spacing: 10

                        // Lista de cuentas (se refresca con bump()).
                        property var accounts: App.listMailAccounts()
                        property int bump: 0
                        function refresh() { accounts = App.listMailAccounts(); bump++ }

                        Text {
                            text: "CORREO"
                            color: Theme.accent
                            font.pixelSize: 11
                            font.bold: true
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            color: Theme.surfaceBg
                            border.color: Theme.borderColor
                            radius: 10
                            implicitHeight: mailInner.implicitHeight + 32

                            ColumnLayout {
                                id: mailInner
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                                spacing: 12

                                Text {
                                    text: "Cuentas que el agente puede usar para enviar (SMTP) y leer (IMAP/POP3) correo. Para Gmail/Outlook usá una CONTRASEÑA DE APLICACIÓN (no la del login normal). La contraseña se guarda cifrada, fuera del repo."
                                    color: Theme.textSecondary
                                    font.pixelSize: 12
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }

                                // Toggle auto-enviar
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Text { text: "Permitir auto-enviar sin aprobación"; color: Theme.textPrimary; font.pixelSize: 13 }
                                        Text {
                                            text: "Riesgo: el agente manda correos sin pedirte confirmación. Por defecto, email_send siempre pide aprobación."
                                            color: Theme.textMuted; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true
                                        }
                                    }
                                    LcSwitch {
                                        checked: App.mailAutoSend
                                        onToggled: App.mailAutoSend = checked
                                    }
                                }

                                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor }

                                // Cuentas existentes
                                Repeater {
                                    model: mailSection.accounts
                                    delegate: Rectangle {
                                        Layout.fillWidth: true
                                        color: Theme.inputBg
                                        border.color: Theme.borderColor
                                        radius: 8
                                        implicitHeight: acctRow.implicitHeight + 16
                                        RowLayout {
                                            id: acctRow
                                            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 8 }
                                            spacing: 8
                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 2
                                                Text { text: modelData.name + "  ·  " + modelData.email; color: Theme.textPrimary; font.pixelSize: 13 }
                                                Text {
                                                    text: (modelData.provider || "custom") + "  ·  " + (modelData.recvProto || "imap")
                                                          + (modelData.hasPassword ? "" : "  ·  ⚠ sin contraseña")
                                                    color: Theme.textMuted; font.pixelSize: 11
                                                }
                                                Text {
                                                    visible: mailSection.bump >= 0 && testMsg.length > 0
                                                    property string testMsg: ""
                                                    id: acctTest
                                                    text: testMsg
                                                    color: text.indexOf("OK") === 0 ? Theme.accent : Theme.btnDangerBg
                                                    font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true
                                                }
                                            }
                                            LcButton {
                                                text: "Probar"
                                                implicitHeight: 30
                                                onClicked: {
                                                    var err = App.testMailAccount(modelData.name)
                                                    acctTest.testMsg = (err.length === 0) ? "OK · conexión y login correctos" : ("Error: " + err)
                                                }
                                            }
                                            LcButton {
                                                text: "✕"
                                                implicitHeight: 30
                                                onClicked: { App.removeMailAccount(modelData.name); mailSection.refresh() }
                                            }
                                        }
                                    }
                                }

                                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor }

                                // Alta de cuenta
                                Text { text: "Agregar cuenta"; color: Theme.textPrimary; font.pixelSize: 13; font.bold: true }

                                LcTextField { id: maName;  Layout.fillWidth: true; placeholderText: "Nombre interno (ej. trabajo)" }
                                LcTextField { id: maEmail; Layout.fillWidth: true; placeholderText: "Dirección (ej. yo@gmail.com)" }
                                LcTextField { id: maDisplay; Layout.fillWidth: true; placeholderText: "Nombre a mostrar (opcional)" }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Text { text: "Proveedor"; color: Theme.textSecondary; font.pixelSize: 12 }
                                    LcComboBox {
                                        id: maProvider
                                        Layout.preferredWidth: 160
                                        model: [
                                            { label: "Auto (por dominio)", value: "auto" },
                                            { label: "Gmail",   value: "gmail" },
                                            { label: "Outlook/Hotmail", value: "outlook" },
                                            { label: "Personalizado", value: "custom" },
                                        ]
                                        textRole: "label"
                                        valueRole: "value"
                                    }
                                    Text { text: "Recepción"; color: Theme.textSecondary; font.pixelSize: 12 }
                                    LcComboBox {
                                        id: maRecvProto
                                        Layout.preferredWidth: 110
                                        model: [ { label: "IMAP", value: "imap" }, { label: "POP3", value: "pop3" } ]
                                        textRole: "label"
                                        valueRole: "value"
                                    }
                                }

                                // Campos avanzados (sólo personalizado / override)
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    visible: maProvider.currentValue === "custom"
                                    RowLayout {
                                        Layout.fillWidth: true; spacing: 8
                                        LcTextField { id: maSmtpHost; Layout.fillWidth: true; placeholderText: "SMTP host (ej. mail.dominio.com)" }
                                        LcTextField { id: maSmtpPort; Layout.preferredWidth: 90; placeholderText: "puerto" }
                                        LcComboBox {
                                            id: maSmtpSec
                                            Layout.preferredWidth: 130
                                            model: [ { label: "SSL/TLS", value: "ssl" }, { label: "STARTTLS", value: "starttls" } ]
                                            textRole: "label"; valueRole: "value"
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true; spacing: 8
                                        LcTextField { id: maRecvHost; Layout.fillWidth: true; placeholderText: "IMAP/POP3 host" }
                                        LcTextField { id: maRecvPort; Layout.preferredWidth: 90; placeholderText: "puerto" }
                                    }
                                }

                                LcTextField { id: maUser; Layout.fillWidth: true; placeholderText: "Usuario (vacío = la dirección)" }
                                LcTextField {
                                    id: maPass; Layout.fillWidth: true
                                    placeholderText: "Contraseña / app password"
                                    echoMode: TextInput.Password
                                }

                                LcButton {
                                    text: "Agregar cuenta"
                                    enabled: maName.text.trim().length > 0 && maEmail.text.trim().length > 0
                                    onClicked: {
                                        var def = {
                                            "email": maEmail.text.trim(),
                                            "displayName": maDisplay.text.trim(),
                                            "provider": maProvider.currentValue,
                                            "recvProto": maRecvProto.currentValue,
                                            "smtpHost": maSmtpHost.text.trim(),
                                            "smtpPort": parseInt(maSmtpPort.text) || 0,
                                            "smtpSecurity": maSmtpSec.currentValue,
                                            "recvHost": maRecvHost.text.trim(),
                                            "recvPort": parseInt(maRecvPort.text) || 0,
                                            "recvSsl": true,
                                            "user": maUser.text.trim(),
                                            "password": maPass.text
                                        }
                                        if (App.setMailAccount(maName.text.trim(), def)) {
                                            maName.text = ""; maEmail.text = ""; maDisplay.text = ""
                                            maSmtpHost.text = ""; maSmtpPort.text = ""; maRecvHost.text = ""
                                            maRecvPort.text = ""; maUser.text = ""; maPass.text = ""
                                            mailSection.refresh()
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // ── Data maintenance ───────────────────────────────────
                    ColumnLayout {
                        id: dataSection
                        Layout.fillWidth: true
                        spacing: 10

                        property var wipeItems: App.wipeCategories()

                        Text {
                            text: "DATA"
                            color: Theme.accent
                            font.pixelSize: 11
                            font.bold: true
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            color: Theme.surfaceBg
                            border.color: Theme.borderColor
                            radius: 10
                            implicitHeight: dataInner.implicitHeight + 32

                            ColumnLayout {
                                id: dataInner
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
                                spacing: 14

                                Text {
                                    text: "Data Backup"
                                    color: Theme.textPrimary
                                    font.pixelSize: 14
                                    font.bold: true
                                }

                                Text {
                                    text: "Exportá o importá tus chats, perfiles, settings, integraciones, skills y resultados guardados como JSON."
                                    color: Theme.textSecondary
                                    font.pixelSize: 12
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    LcButton {
                                        text: "Export Data"
                                        onClicked: App.exportUserData()
                                    }
                                    LcButton {
                                        text: "Import Data"
                                        secondary: true
                                        onClicked: App.importUserData()
                                    }
                                    Item { Layout.fillWidth: true }
                                }

                                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.borderColor }

                                Text {
                                    text: "Danger Zone"
                                    color: Theme.btnDangerBg
                                    font.pixelSize: 14
                                    font.bold: true
                                }

                                Text {
                                    text: "Irreversible. Cada wipe apunta a una categoría; elegí exactamente qué querés borrar."
                                    color: Theme.textSecondary
                                    font.pixelSize: 12
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                }

                                Repeater {
                                    model: dataSection.wipeItems
                                    delegate: Rectangle {
                                        Layout.fillWidth: true
                                        radius: 8
                                        color: Theme.inputBg
                                        border.color: Theme.borderColor
                                        implicitHeight: wipeRow.implicitHeight + 18

                                        RowLayout {
                                            id: wipeRow
                                            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                                            spacing: 12

                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 3
                                                Text {
                                                    text: modelData.title
                                                    color: Theme.textPrimary
                                                    font.pixelSize: 13
                                                    font.bold: true
                                                }
                                                Text {
                                                    text: modelData.description
                                                    color: Theme.textMuted
                                                    font.pixelSize: 11
                                                    Layout.fillWidth: true
                                                    wrapMode: Text.WordWrap
                                                }
                                            }

                                            LcButton {
                                                text: "Wipe"
                                                danger: true
                                                implicitHeight: 30
                                                onClicked: {
                                                    wipeDialog.kind = modelData.kind
                                                    wipeDialog.label = modelData.title
                                                    wipeDialog.description = modelData.description
                                                    wipeConfirm.text = ""
                                                    wipeDialog.open()
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Editor de tema custom ───────────────────────────────────────────────
    LcDialog {
        id: themeEditor
        title: "Editor de tema"
        standardButtons: Dialog.NoButton
        width: Math.min(520, root.width - 48)

        property string editId: ""
        property string baseTheme: "dark"
        property bool translucent: false

        function loadDef(def) {
            editId = def.id || ""
            nameField.text = def.name || ""
            baseTheme = def.base || "dark"
            baseCombo.currentIndex = baseCombo.indexOfValue(baseTheme)
            accentRow.value = def.accent || ""
            bgRow.value = def.background || ""
            fgRow.value = def.foreground || ""
            uiFontField.text = def.uiFont || ""
            codeFontField.text = def.codeFont || ""
            contrastSlider.value = def.contrast !== undefined ? def.contrast : 30
            translucent = def.translucent === true
        }
        function openNew() { loadDef(Theme.defaultCustomDef("dark")); open() }
        function openEdit(def) { loadDef(def); open() }
        function collect() {
            return {
                "id": editId,
                "name": nameField.text.trim(),
                "base": baseCombo.currentValue,
                "accent": accentRow.value.trim(),
                "background": bgRow.value.trim(),
                "foreground": fgRow.value.trim(),
                "uiFont": uiFontField.text.trim(),
                "codeFont": codeFontField.text.trim(),
                "contrast": Math.round(contrastSlider.value),
                "translucent": themeEditor.translucent
            }
        }

        contentItem: ColumnLayout {
            spacing: 12

            LcTextField {
                id: nameField
                Layout.fillWidth: true
                placeholderText: "Nombre del tema"
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text { text: "Base"; color: Theme.textSecondary; font.pixelSize: 12 }
                LcComboBox {
                    id: baseCombo
                    Layout.preferredWidth: 160
                    model: [
                        { label: "Oscuro", value: "dark" },
                        { label: "Claro",  value: "light" },
                        { label: "OLED",   value: "oled" },
                    ]
                    textRole: "label"
                    valueRole: "value"
                }
                Text {
                    Layout.fillWidth: true
                    text: "Define estados (error/ok) y overlay."
                    color: Theme.textMuted; font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }

            // Anclas de color: swatch clickeable (abre el selector) + hex.
            LcColorRow { id: accentRow; label: "Acento";       picker: colorPicker }
            LcColorRow { id: bgRow;     label: "Fondo";        picker: colorPicker }
            LcColorRow { id: fgRow;     label: "Primer plano"; picker: colorPicker }

            LcTextField {
                id: uiFontField
                Layout.fillWidth: true
                placeholderText: "Fuente de la interfaz (ej. Inter)"
            }
            LcTextField {
                id: codeFontField
                Layout.fillWidth: true
                placeholderText: "Fuente de código (ej. JetBrains Mono)"
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text { text: "Barra lateral translúcida"; color: Theme.textPrimary; font.pixelSize: 13; Layout.fillWidth: true }
                LcSwitch {
                    checked: themeEditor.translucent
                    onToggled: themeEditor.translucent = checked
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text { text: "Contraste"; color: Theme.textPrimary; font.pixelSize: 13 }
                Slider {
                    id: contrastSlider
                    Layout.fillWidth: true
                    from: 0; to: 100; stepSize: 1
                    value: 30
                }
                Text {
                    text: Math.round(contrastSlider.value)
                    color: Theme.textSecondary; font.pixelSize: 13
                    Layout.preferredWidth: 30
                }
            }
        }

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
                    text: (App.langV, App.l("common.cancel"))
                    secondary: true
                    onClicked: themeEditor.close()
                }
                LcButton {
                    text: "Guardar y aplicar"
                    onClicked: {
                        var id = Theme.saveCustomTheme(themeEditor.collect())
                        Theme.applyCustomTheme(id)
                        themeEditor.close()
                    }
                }
            }
        }
    }

    LcDialog {
        id: wipeDialog
        title: "Confirm wipe"
        property string kind: ""
        property string label: ""
        property string description: ""
        standardButtons: Dialog.NoButton
        width: Math.min(520, root.width - 48)

        contentItem: ColumnLayout {
            spacing: 12
            Text {
                text: wipeDialog.label
                color: Theme.textPrimary
                font.pixelSize: 14
                font.bold: true
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            Text {
                text: wipeDialog.description
                color: Theme.textSecondary
                font.pixelSize: 12
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            Text {
                text: "Escribí WIPE para confirmar."
                color: Theme.textSecondary
                font.pixelSize: 12
            }
            LcTextField {
                id: wipeConfirm
                Layout.fillWidth: true
                placeholderText: "WIPE"
                onAccepted: {
                    if (text.trim() === "WIPE") {
                        App.wipeUserData(wipeDialog.kind, text.trim())
                        wipeDialog.close()
                    }
                }
            }
        }

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
                    text: (App.langV, App.l("common.cancel"))
                    secondary: true
                    onClicked: wipeDialog.close()
                }
                LcButton {
                    text: "Wipe"
                    danger: true
                    enabled: wipeConfirm.text.trim() === "WIPE"
                    onClicked: {
                        App.wipeUserData(wipeDialog.kind, wipeConfirm.text.trim())
                        wipeDialog.close()
                    }
                }
            }
        }
    }
}
