import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform as Platform
import LlamaCode 1.0

ApplicationWindow {
    id: window
    title: "UNLZ_Llamacode"
    width: 1200
    height: 760
    minimumWidth: 900
    minimumHeight: 600
    // Arranca oculta: la geometría guardada se aplica en Component.onCompleted
    // y recién ahí mostramos, así no se ve el salto de tamaño (default→guardado).
    visible: false
    color: Theme.windowBg
    flags: Qt.Window | Qt.FramelessWindowHint

    property color frameBorderColor: active ? Theme.frameBorderActive : Theme.frameBorderInact
    property color frameBgColor: Theme.baseBg
    property color titleBarColor: Theme.titleBg
    property int resizeHandleSize: 8
    property bool restoringWindowState: true
    // Minimizar a la bandeja de notificación al cerrar (en vez de salir).
    property bool minimizeToTray: Boolean(App.readSetting("window/minimizeToTray", false))
    // Bandera para forzar salida real desde el menú del tray.
    property bool forceQuit: false
    property bool autoCreatingInitialProfile: false

    function showFromTray() {
        if (Boolean(App.readSetting("window/maximized", false)))
            window.showMaximized()
        else
            window.showNormal()
        window.show()
        window.raise()
        window.requestActivate()
    }

    function maybeCreateInitialProfile() {
        if (autoCreatingInitialProfile)
            return
        if (!App.hasAnyBinary || !App.hasAnyModel || App.hasAnyLaunch)
            return
        autoCreatingInitialProfile = true
        const id = App.createRecommendedLaunchProfile()
        autoCreatingInitialProfile = false
        if (id.length > 0) {
            errorToast.show("Perfil inicial creado.")
            stack.currentIndex = 0
        }
    }

    function saveWindowState() {
        if (restoringWindowState)
            return
        if (window.visibility === Window.Maximized) {
            App.writeSetting("window/maximized", true)
            return
        }
        if (window.visibility !== Window.Windowed)
            return
        App.writeSetting("window/maximized", false)
        App.writeSetting("window/x", x)
        App.writeSetting("window/y", y)
        App.writeSetting("window/width", width)
        App.writeSetting("window/height", height)
    }

    function startResize(edges) {
        if (window.visibility === Window.Maximized)
            return
        window.startSystemResize(edges)
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 0
        radius: 0
        color: window.frameBgColor
        border.width: 0
        clip: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                id: titleBar
                Layout.fillWidth: true
                Layout.preferredHeight: 38
                color: window.titleBarColor

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 0
                    spacing: 0

                    Label {
                        text: "UNLZ_Llamacode"
                        color: Theme.textPrimary
                        font.pixelSize: 13
                        font.bold: true
                        Layout.alignment: Qt.AlignVCenter
                    }

                    Item { Layout.fillWidth: true }

                    ToolButton {
                        text: "\uE921"
                        flat: true
                        onClicked: window.showMinimized()
                        contentItem: Text {
                            text: parent.text
                            color: Theme.textPrimary
                            font.family: "Segoe MDL2 Assets"
                            font.pixelSize: 10
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle { color: parent.hovered ? Theme.frameBorderInact : "transparent" }
                        Layout.preferredWidth: 46
                        Layout.fillHeight: true
                    }

                    ToolButton {
                        text: window.visibility === Window.Maximized ? "\uE923" : "\uE922"
                        flat: true
                        onClicked: window.visibility === Window.Maximized ? window.showNormal() : window.showMaximized()
                        contentItem: Text {
                            text: parent.text
                            color: Theme.textPrimary
                            font.family: "Segoe MDL2 Assets"
                            font.pixelSize: 11
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle { color: parent.hovered ? Theme.frameBorderInact : "transparent" }
                        Layout.preferredWidth: 46
                        Layout.fillHeight: true
                    }

                    ToolButton {
                        text: "\uE8BB"
                        flat: true
                        onClicked: window.close()
                        contentItem: Text {
                            text: parent.text
                            color: Theme.errorText
                            font.family: "Segoe MDL2 Assets"
                            font.pixelSize: 10
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle { color: parent.hovered ? Theme.closeHoverBg : "transparent" }
                        Layout.preferredWidth: 46
                        Layout.fillHeight: true
                    }
                }

                TapHandler {
                    onDoubleTapped: window.visibility === Window.Maximized ? window.showNormal() : window.showMaximized()
                }
                DragHandler {
                    target: null
                    onActiveChanged: if (active) window.startSystemMove()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                NavBar {
                    Layout.fillHeight: true
                    currentIndex: stack.currentIndex
                    onPageSelected: function(idx) { stack.currentIndex = idx }
                }

                Rectangle { width: 1; Layout.fillHeight: true; color: Theme.divider }

                StackLayout {
                    id: stack
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: 0

                    LaunchPage      {}
                    ProfilesPage    {}
                    ModelRootsPage  { id: modelRootsPage }
                    BinariesPage    { id: binariesPage }
                    ChatPage        {}
                    AgentPage       {}
                    ResearchPage    {}
                    TasksPage       {}
                    BenchmarkPage   {}
                    CharlaPage      {}
                    StudiaPage      {}
                    SettingsPage    {}
                }
            }
        }
    }

    MouseArea {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: window.resizeHandleSize
        hoverEnabled: true
        cursorShape: Qt.SizeHorCursor
        onPressed: window.startResize(Qt.LeftEdge)
    }
    MouseArea {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: window.resizeHandleSize
        hoverEnabled: true
        cursorShape: Qt.SizeHorCursor
        onPressed: window.startResize(Qt.RightEdge)
    }
    MouseArea {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: window.resizeHandleSize
        hoverEnabled: true
        cursorShape: Qt.SizeVerCursor
        onPressed: window.startResize(Qt.TopEdge)
    }
    MouseArea {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: window.resizeHandleSize
        hoverEnabled: true
        cursorShape: Qt.SizeVerCursor
        onPressed: window.startResize(Qt.BottomEdge)
    }
    MouseArea {
        anchors.left: parent.left
        anchors.top: parent.top
        width: window.resizeHandleSize
        height: window.resizeHandleSize
        hoverEnabled: true
        cursorShape: Qt.SizeFDiagCursor
        onPressed: window.startResize(Qt.TopEdge | Qt.LeftEdge)
    }
    MouseArea {
        anchors.right: parent.right
        anchors.top: parent.top
        width: window.resizeHandleSize
        height: window.resizeHandleSize
        hoverEnabled: true
        cursorShape: Qt.SizeBDiagCursor
        onPressed: window.startResize(Qt.TopEdge | Qt.RightEdge)
    }
    MouseArea {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        width: window.resizeHandleSize
        height: window.resizeHandleSize
        hoverEnabled: true
        cursorShape: Qt.SizeBDiagCursor
        onPressed: window.startResize(Qt.BottomEdge | Qt.LeftEdge)
    }
    MouseArea {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: window.resizeHandleSize
        height: window.resizeHandleSize
        hoverEnabled: true
        cursorShape: Qt.SizeFDiagCursor
        onPressed: window.startResize(Qt.BottomEdge | Qt.RightEdge)
    }

    // Global error toast
    Popup {
        id: errorToast
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 380
        height: 60
        modal: false

        property string message: ""
        function show(msg) { message = msg; open(); closeTimer.start() }

        background: Rectangle {
            color: Theme.errorBg; radius: 8
            border.color: Theme.errorBorder; border.width: 1
        }

        Text {
            anchors.centerIn: parent
            text: errorToast.message
            color: Theme.errorText; font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            width: parent.width - 24
        }

        Timer { id: closeTimer; interval: 4000; onTriggered: errorToast.close() }
    }

    Popup {
        id: setupPopup
        parent: Overlay.overlay
        modal: true
        clip: true
        closePolicy: Popup.NoAutoClose
        width: 760
        height: 640
        padding: 18
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)

        background: Rectangle {
            color: Theme.popupBg
            radius: 12
            border.width: 1
            border.color: Theme.popupBorderColor
        }

        contentItem: ColumnLayout {
            width: setupPopup.availableWidth
            height: setupPopup.availableHeight
            spacing: 12

            Text {
                text: (App.langV, App.l("setup.title"))
                color: Theme.textPrimary
                font.pixelSize: 20
                font.bold: true
            }
            Text {
                text: (App.langV, App.l("setup.description"))
                color: Theme.textSecondary
                Layout.fillWidth: true
                Layout.preferredWidth: setupPopup.availableWidth
                Layout.maximumWidth: setupPopup.availableWidth
                clip: true
                wrapMode: Text.WordWrap
                font.pixelSize: 13
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

            // Step 1: binary
            Text {
                text: (App.hasAnyBinary ? "✓ " : "1. ") + "llama-server"
                color: App.hasAnyBinary ? Theme.accent : Theme.textPrimary
                font.pixelSize: 13
                font.bold: true
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                enabled: !App.hasAnyBinary
                opacity: App.hasAnyBinary ? 0.5 : 1.0
                LcButton {
                    text: (App.langV, App.l("setup.locateBinary"))
                    secondary: true
                    onClicked: { stack.currentIndex = 3; binariesPage.openAddDialog() }
                }
                LcButton {
                    text: {
                        const _lang = App.langV
                        return App.installingOfficialBinary ? App.l("setup.installing") : App.l("setup.installBinary")
                    }
                    enabled: !App.installingOfficialBinary
                    onClicked: App.installOfficialBinary()
                }
                LcButton {
                    visible: App.installingOfficialBinary
                    text: (App.langV, App.l("setup.cancel"))
                    secondary: true
                    onClicked: App.cancelOfficialBinaryInstall()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: App.installingOfficialBinary || App.officialBinaryInstallStatus.length > 0
                spacing: 8
                BusyIndicator {
                    running: App.installingOfficialBinary
                    visible: App.installingOfficialBinary
                    Layout.preferredWidth: 22
                    Layout.preferredHeight: 22
                }
                Text {
                    Layout.fillWidth: true
                    text: App.officialBinaryInstallStatus
                    color: App.installingOfficialBinary ? Theme.accent : Theme.errorText
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
                LcButton {
                    visible: !App.installingOfficialBinary && App.officialBinaryInstallStatus.length > 0
                    text: (App.langV, App.l("setup.viewLog"))
                    secondary: true
                    onClicked: installLogPopup.open()
                }
            }

            // Step 2: model
            Text {
                text: (App.hasAnyModel ? "✓ " : "2. ") + ".gguf"
                color: App.hasAnyModel ? Theme.accent : Theme.textPrimary
                font.pixelSize: 13
                font.bold: true
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                enabled: !App.hasAnyModel
                opacity: App.hasAnyModel ? 0.5 : 1.0
                LcButton {
                    text: (App.langV, App.l("setup.locateModel"))
                    secondary: true
                    onClicked: { stack.currentIndex = 2; modelRootsPage.openAddDialog() }
                }
                LcButton {
                    text: (App.langV, App.l("setup.downloadModel"))
                    secondary: true
                    onClicked: Qt.openUrlExternally("https://huggingface.co/models?library=gguf")
                }
                LcButton {
                    text: (App.langV, App.l("setup.goToModels"))
                    secondary: true
                    onClicked: stack.currentIndex = 2
                }
            }

            Rectangle {
                id: setupReco
                Layout.fillWidth: true
                Layout.preferredHeight: 154
                visible: !App.hasAnyModel && App.modelRecommendations.length > 0
                radius: 8
                color: Theme.surfaceBg
                border.color: Theme.borderColor

                readonly property var pick: App.modelRecommendations[0] ?? ({})

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: "Recomendado para esta computadora"
                                color: Theme.textPrimary
                                font { pixelSize: 13; bold: true }
                            }
                            Text {
                                text: App.hardwareSummary.summary ?? ""
                                color: Theme.textMuted
                                font.pixelSize: 11
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
                        Layout.bottomMargin: 6
                        spacing: 10
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Text {
                                Layout.fillWidth: true
                                text: setupReco.pick.name ?? ""
                                color: Theme.textPrimary
                                font { pixelSize: 14; bold: true }
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: (setupReco.pick.fit ?? "") + " · " +
                                      (setupReco.pick.params ?? "") + " · " +
                                      (setupReco.pick.quant ?? "") + " · " +
                                      ((setupReco.pick.sizeGb ?? 0).toFixed(1)) + " GB"
                                color: Theme.textSecondary
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: setupReco.pick.notes ?? ""
                                color: Theme.textMuted
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                        }
                        LcButton {
                            text: App.modelDownloadRunning ? "Agregar a cola" : "Descargar"
                            Layout.preferredHeight: 34
                            enabled: setupReco.pick.downloadable ?? true
                            onClicked: App.downloadRecommendedModel(setupReco.pick.repo ?? "",
                                                                    setupReco.pick.fileName ?? "")
                        }
                        LcButton {
                            text: "HF"
                            Layout.preferredHeight: 34
                            secondary: true
                            onClicked: App.openModelRecommendation(setupReco.pick.repo ?? "")
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: App.modelDownloadRunning || App.modelDownloadStatus.length > 0
                        spacing: 8
                        ProgressBar {
                            Layout.preferredWidth: 150
                            from: 0
                            to: 100
                            value: App.modelDownloadProgress
                        }
                        Text {
                            Layout.fillWidth: true
                            text: App.modelDownloadStatus
                            color: App.modelDownloadRunning ? Theme.accent : Theme.textMuted
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                        }
                    }
                }
            }

            // Step 3: launch profile
            Text {
                text: (App.hasAnyLaunch ? "✓ " : "3. ") + "Perfil de lanzamiento"
                color: App.hasAnyLaunch ? Theme.accent : Theme.textPrimary
                font.pixelSize: 13
                font.bold: true
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                enabled: App.hasAnyBinary && App.hasAnyModel && !App.hasAnyLaunch
                opacity: App.hasAnyLaunch ? 0.5 : 1.0
                BusyIndicator {
                    running: App.hasAnyBinary && App.hasAnyModel && !App.hasAnyLaunch
                    visible: running
                    Layout.preferredWidth: 22
                    Layout.preferredHeight: 22
                }
                Text {
                    Layout.fillWidth: true
                    text: App.hasAnyLaunch
                          ? "Listo para lanzar."
                          : (App.hasAnyBinary && App.hasAnyModel
                             ? "Creando automáticamente Backend, Model, Runtime y perfil de lanzamiento."
                             : "Primero completá binario y modelo.")
                    color: Theme.textMuted
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }

            Item { Layout.fillHeight: true }
            Text {
                visible: App.needsSetup
                text: (App.langV, App.l("setup.tip"))
                color: Theme.textMuted
                font.pixelSize: 12
            }
        }
    }

    Popup {
        id: updatePopup
        parent: Overlay.overlay
        modal: true
        clip: true
        closePolicy: Popup.NoAutoClose
        width: Math.min(window.width - 80, 620)
        height: Math.min(window.height - 80, 430)
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        padding: 0

        background: Rectangle {
            color: Theme.popupBg
            radius: 10
            border.width: 1
            border.color: Theme.popupBorderColor
        }

        contentItem: ColumnLayout {
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 58
                color: Theme.popupHeaderBg
                radius: 10
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 10; color: Theme.popupHeaderBg }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.popupHeaderBorder }
                Text {
                    anchors { left: parent.left; leftMargin: 20; verticalCenter: parent.verticalCenter }
                    text: App.updateInfo.title ?? "Nueva version disponible"
                    color: Theme.textPrimary
                    font.pixelSize: 15
                    font.bold: true
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 18
                spacing: 12

                Text {
                    Layout.fillWidth: true
                    text: "Version " + (App.updateInfo.version ?? "")
                    color: Theme.accent
                    font.pixelSize: 13
                    font.bold: true
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    text: App.updateInfo.summary ?? ""
                    color: Theme.textSecondary
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    Column {
                        width: updatePopup.width - 36
                        spacing: 8
                        Repeater {
                            model: App.updateInfo.changelog ?? []
                            delegate: RowLayout {
                                width: parent.width
                                spacing: 8
                                Text {
                                    text: "•"
                                    color: Theme.accent
                                    font.pixelSize: 14
                                    Layout.alignment: Qt.AlignTop
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData
                                    color: Theme.textPrimary
                                    font.pixelSize: 12
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 64
                color: Theme.popupHeaderBg
                radius: 10
                Rectangle { anchors.top: parent.top; width: parent.width; height: 10; color: Theme.popupHeaderBg }
                Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.popupHeaderBorder }
                RowLayout {
                    anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                    spacing: 10
                    LcButton {
                        text: "Postponer hasta la proxima version"
                        secondary: true
                        onClicked: {
                            App.handleUpdateDecision("skipVersion")
                            updatePopup.close()
                        }
                    }
                    LcButton {
                        text: "Postponer al proximo inicio"
                        secondary: true
                        onClicked: {
                            App.handleUpdateDecision("nextStart")
                            updatePopup.close()
                        }
                    }
                    LcButton {
                        text: "Actualizar ahora"
                        onClicked: {
                            App.handleUpdateDecision("updateNow")
                            updatePopup.close()
                        }
                    }
                }
            }
        }
    }

    Popup {
        id: installLogPopup
        parent: Overlay.overlay
        modal: true
        clip: true
        width: Math.min(window.width - 80, 900)
        height: Math.min(window.height - 80, 520)
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        padding: 14

        background: Rectangle {
            color: Theme.popupBg
            radius: 10
            border.width: 1
            border.color: Theme.popupBorderColor
        }

        contentItem: ColumnLayout {
            width: installLogPopup.availableWidth
            height: installLogPopup.availableHeight
            spacing: 10

            Text {
                text: (App.langV, App.l("setup.installLog"))
                color: Theme.textPrimary
                font.pixelSize: 16
                font.bold: true
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
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
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8
                LcButton {
                    text: (App.langV, App.l("setup.copyLog"))
                    secondary: true
                    onClicked: App.copyToClipboard(App.officialBinaryInstallLog)
                }
                LcButton { text: (App.langV, App.l("common.close")); onClicked: installLogPopup.close() }
            }
        }
    }

    Component.onCompleted: {
        const savedX = Number(App.readSetting("window/x", 100))
        const savedY = Number(App.readSetting("window/y", 100))
        const savedW = Number(App.readSetting("window/width", 1200))
        const savedH = Number(App.readSetting("window/height", 760))
        const savedMaximized = Boolean(App.readSetting("window/maximized", false))

        const restoredW = Math.max(minimumWidth, savedW)
        const restoredH = Math.max(minimumHeight, savedH)
        width = restoredW
        height = restoredH
        x = savedX
        y = savedY
        if (savedMaximized)
            showMaximized()
        else
            visible = true
        restoringWindowState = false

        // El escaneo pesado ya corrió en main.cpp bajo el splash → counts listos.
        if (App.needsSetup) setupPopup.open()
        maybeCreateInitialProfile()
        App.checkForUpdates()
    }

    onClosing: function(close) {
        saveWindowState()
        if (window.minimizeToTray && !window.forceQuit) {
            close.accepted = false
            window.hide()
        }
    }

    // Ícono en la bandeja de notificación. Visible sólo con el toggle activo.
    // Click izquierdo o doble click restaura; botón derecho da menú Abrir/Salir.
    Platform.SystemTrayIcon {
        id: trayIcon
        visible: window.minimizeToTray
        icon.source: AppIconSource
        tooltip: "UNLZ_Llamacode"
        onActivated: function(reason) {
            if (reason === Platform.SystemTrayIcon.Trigger
                    || reason === Platform.SystemTrayIcon.DoubleClick)
                window.showFromTray()
        }
        menu: Platform.Menu {
            Platform.MenuItem {
                text: (App.langV, App.l("tray.open"))
                onTriggered: window.showFromTray()
            }
            Platform.MenuItem { separator: true }
            Platform.MenuItem {
                text: (App.langV, App.l("tray.quit"))
                onTriggered: { window.forceQuit = true; Qt.quit() }
            }
        }
    }

    onXChanged: saveWindowState()
    onYChanged: saveWindowState()
    onWidthChanged: saveWindowState()
    onHeightChanged: saveWindowState()
    onVisibilityChanged: saveWindowState()

    Connections {
        target: App
        function onServerError(message) { errorToast.show(message) }
        function onSetupStateChanged() {
            maybeCreateInitialProfile()
            if (App.needsSetup) setupPopup.open()
            else setupPopup.close()
        }
        function onModelDownloadChanged() { maybeCreateInitialProfile() }
        function onOfficialBinaryInstallFinished(success, message, binaryPath) {
            errorToast.show(message)
            if (success && binaryPath.length > 0) {
                stack.currentIndex = App.hasAnyModel ? 0 : 2
            }
            maybeCreateInitialProfile()
        }
        function onUpdateCheckChanged() {
            if (App.updateAvailable)
                updatePopup.open()
        }
    }
    Connections {
        target: App.binaryRegistry
        function onCapabilitiesDetected(id, success, error) {
            if (!success) errorToast.show("Capability detection failed: " + error)
        }
    }
    Connections {
        target: App.rootRegistry
        function onScanFinished(rootId, count) {
            maybeCreateInitialProfile()
        }
    }
}
