import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0

Item {
    id: root
    property bool logVisible: false
    property real logHeight: 220
    property real minLogHeight: 120
    property bool _restored: false   // evita pisar la setting durante la carga inicial
    property bool _syncingActiveLaunch: false
    property string pendingPortLaunchId: ""
    property string pendingPortHost: ""
    property int pendingPortCurrent: 0
    property int pendingPortSuggested: 0
    property bool pendingPortStartAgent: false

    function startProfile(launchId, withAgent) {
        if (withAgent)
            App.startServerAndAgent(launchId)
        else
            App.startServer(launchId)
    }

    function syncToActiveLaunch() {
        const id = App.activeLaunchId
        if (!id || id.length === 0) return
        root._syncingActiveLaunch = true
        launchCombo.selectLaunchProfile(id)
        root._syncingActiveLaunch = false
    }

    onVisibleChanged: {
        if (visible && App.serverRunning)
            syncToActiveLaunch()
    }

    Connections {
        target: App
        function onServerPortCollision(launchProfileId, host, requestedPort, suggestedPort, startAgent) {
            root.pendingPortLaunchId = launchProfileId
            root.pendingPortHost = host
            root.pendingPortCurrent = requestedPort
            root.pendingPortSuggested = suggestedPort
            root.pendingPortStartAgent = startAgent
            portCollisionDialog.open()
        }
        function onLanProfileReady(launchProfileId, error) {
            lanDialog.busy = false
            lanDialog.message = error.length > 0
                ? error
                : "Servidor remoto iniciado y agente conectado."
            if (error.length === 0)
                lanDialog.close()
        }
    }

    LcDialog {
        id: lanDialog
        modal: true
        width: 560
        height: 330
        x: Math.round((root.width - width) / 2)
        y: Math.round((root.height - height) / 2)
        title: "Usar un servidor LAN"
        standardButtons: Dialog.NoButton
        footer: null
        property bool busy: false
        property string message: ""
        property var selectedServer: (serverCombo.currentIndex >= 0
                                      && serverCombo.currentIndex < App.lanServers.length)
                                     ? App.lanServers[serverCombo.currentIndex] : null
        property var remoteProfiles: selectedServer ? (selectedServer.profiles || []) : []
        background: Rectangle { color: Theme.surfaceBg; radius: 8; border.color: Theme.borderColor }

        onOpened: {
            message = ""
            App.discoverLanServers()
        }

        contentItem: ColumnLayout {
            spacing: 12
            Text {
                Layout.fillWidth: true
                text: "LlamaCode busca automáticamente equipos que estén compartiendo su gateway en esta red."
                color: Theme.textSecondary
                wrapMode: Text.WordWrap
                font.pixelSize: 12
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "Servidor"; color: Theme.textMuted; Layout.preferredWidth: 70 }
                LcComboBox {
                    id: serverCombo
                    Layout.fillWidth: true
                    model: App.lanServers
                    textRole: "name"
                    onCurrentIndexChanged: profileCombo.currentIndex = 0
                }
                LcButton {
                    text: App.lanDiscoveryActive ? "Buscando…" : "Buscar otra vez"
                    secondary: true
                    enabled: !App.lanDiscoveryActive && !lanDialog.busy
                    onClicked: App.discoverLanServers()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "Perfil"; color: Theme.textMuted; Layout.preferredWidth: 70 }
                LcComboBox {
                    id: profileCombo
                    Layout.fillWidth: true
                    model: lanDialog.remoteProfiles
                    textRole: "name"
                    valueRole: "id"
                }
            }
            Text {
                Layout.fillWidth: true
                visible: !App.lanDiscoveryActive && App.lanServers.length === 0
                text: "No encontré servidores. En la otra PC activá Configuración → Gateway → Compartir server en LAN y revisá el firewall de red privada."
                color: Theme.textMuted
                wrapMode: Text.WordWrap
                font.pixelSize: 11
            }
            Text {
                Layout.fillWidth: true
                visible: lanDialog.message.length > 0
                text: lanDialog.message
                color: Theme.errorText
                wrapMode: Text.WordWrap
                font.pixelSize: 11
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8
                LcButton {
                    text: "Cancelar"
                    secondary: true
                    enabled: !lanDialog.busy
                    onClicked: lanDialog.close()
                }
                LcButton {
                    text: lanDialog.busy ? "Iniciando servidor remoto…" : "Usar perfil remoto"
                    enabled: !lanDialog.busy && lanDialog.selectedServer
                             && profileCombo.currentIndex >= 0
                    onClicked: {
                        const profile = lanDialog.remoteProfiles[profileCombo.currentIndex]
                        if (!profile) return
                        lanDialog.busy = true
                        lanDialog.message = ""
                        App.useLanServer(lanDialog.selectedServer.url,
                                         lanDialog.selectedServer.apiKey || "",
                                         profile.id || "", profile.name || profile.id,
                                         profile.context || 4096)
                    }
                }
            }
        }
    }

    // Perfil de sistema seleccionado sin modelo/binario: ofrecer instalar deps.
    LcDialog {
        id: depsDialog
        property string launchId: ""
        property string profileName: ""
        modal: true
        width: 460
        height: 200
        x: Math.round((root.width - width) / 2)
        y: Math.round((root.height - height) / 2)
        title: "Faltan dependencias"
        standardButtons: Dialog.NoButton
        footer: null
        background: Rectangle { color: Theme.surfaceBg; radius: 8; border.color: Theme.borderColor }
        contentItem: ColumnLayout {
            spacing: 12
            Text {
                Layout.fillWidth: true
                Layout.preferredWidth: 420
                text: "Al perfil «" + depsDialog.profileName + "» le faltan modelos o binarios.\n¿Descargar e instalar las dependencias?"
                color: Theme.textPrimary
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 10
                LcButton {
                    text: "Instalar dependencias"
                    onClicked: { App.acceptSystemProfile(depsDialog.launchId); depsDialog.close() }
                }
                LcButton {
                    text: "Cerrar"
                    secondary: true
                    onClicked: depsDialog.close()
                }
            }
        }
    }

    function startWithPortCheck(withAgent) {
        const launchId = launchCombo.currentValue ?? ""
        if (!launchId || launchId.length === 0)
            return
        const vf = App.launchVramFitStatus(launchId)
        if (vf.warning === true) {
            vramWarningDialog.launchId = launchId
            vramWarningDialog.withAgent = withAgent
            vramWarningDialog.message = vf.message || ""
            vramWarningDialog.freeGb = vf.freeGb ?? 0
            vramWarningDialog.requiredGb = vf.requiredGb ?? 0
            vramWarningDialog.open()
            return
        }
        startAfterVramCheck(launchId, withAgent)
    }

    function startAfterVramCheck(launchId, withAgent) {
        const st = App.launchPortStatus(launchId)
        if (st.blocked === true && (st.suggestedPort ?? 0) > 0) {
            portConflictDialog.launchId = launchId
            portConflictDialog.withAgent = withAgent
            portConflictDialog.currentPort = st.port ?? 8080
            portConflictDialog.suggestedPort = st.suggestedPort
            portConflictDialog.host = st.host ?? "127.0.0.1"
            portConflictDialog.open()
            return
        }
        startProfile(launchId, withAgent)
    }

    Dialog {
        id: vramWarningDialog
        modal: true
        width: 500
        height: 300
        x: Math.round((root.width - width) / 2)
        y: Math.round((root.height - height) / 2)
        closePolicy: Popup.CloseOnEscape
        leftPadding: 22
        rightPadding: 22
        topPadding: 20
        bottomPadding: 16

        property string launchId: ""
        property bool withAgent: true
        property string message: ""
        property real freeGb: 0
        property real requiredGb: 0

        background: Rectangle {
            color: Theme.surfaceBg
            radius: 8
            border.color: Theme.errorText
        }

        contentItem: ColumnLayout {
            spacing: 12
            Text {
                text: "VRAM insuficiente para este perfil"
                color: Theme.textPrimary
                font { pixelSize: 16; bold: true }
            }
            Text {
                Layout.fillWidth: true
                text: vramWarningDialog.message
                color: Theme.textSecondary
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                text: "Recomendación: cerrá apps que usen GPU o elegí un perfil con menor contexto/modelo. Continuar puede hacer que el servidor cargue en memoria compartida y decodifique a pocos tokens por segundo."
                color: Theme.textMuted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }

        footer: Item {
            implicitHeight: 58
            RowLayout {
                anchors {
                    right: parent.right; bottom: parent.bottom
                    rightMargin: 22; bottomMargin: 16
                }
                spacing: 8
                LcButton {
                    text: "Cancelar"
                    secondary: true
                    onClicked: vramWarningDialog.close()
                }
                LcButton {
                    text: "Continuar igual"
                    onClicked: {
                        vramWarningDialog.close()
                        root.startAfterVramCheck(vramWarningDialog.launchId, vramWarningDialog.withAgent)
                    }
                }
            }
        }
    }

    Dialog {
        id: portConflictDialog
        modal: true
        width: 430
        height: 250
        x: Math.round((root.width - width) / 2)
        y: Math.round((root.height - height) / 2)
        closePolicy: Popup.CloseOnEscape

        property string launchId: ""
        property bool withAgent: true
        property int currentPort: 8080
        property int suggestedPort: 8081
        property string host: "127.0.0.1"

        background: Rectangle {
            color: Theme.surfaceBg
            radius: 8
            border.color: Theme.borderColor
        }

        contentItem: ColumnLayout {
            spacing: 12
            Text {
                text: "Puerto ocupado"
                color: Theme.textPrimary
                font { pixelSize: 16; bold: true }
            }
            Text {
                Layout.fillWidth: true
                text: "El puerto " + portConflictDialog.currentPort + " ya está en uso en " + portConflictDialog.host
                      + ". ¿Desea cambiar al puerto " + portConflictDialog.suggestedPort + "?"
                color: Theme.textSecondary
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                text: "Se actualizará el backend de este perfil y luego se iniciará "
                      + (portConflictDialog.withAgent ? "el servidor + agente." : "el servidor.")
                color: Theme.textMuted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }

        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            LcButton {
                text: "Cancelar"
                secondary: true
                onClicked: portConflictDialog.close()
            }
            LcButton {
                text: "Cambiar puerto"
                onClicked: {
                    if (App.setLaunchBackendPort(portConflictDialog.launchId, portConflictDialog.suggestedPort)) {
                        portConflictDialog.close()
                        root.startProfile(portConflictDialog.launchId, portConflictDialog.withAgent)
                    }
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            Layout.fillWidth: true
            title: (App.langV, App.l("launch.title"))
            subtitle: {
                const _lang = App.langV
                return App.serverRunning ? App.l("launch.running") : App.l("launch.stopped")
            }
            // Re-abrir el asistente inicial (si se canceló por error el setup).
            actionLabel: "Repetir asistente inicial"
            onActionClicked: App.requestShowSetup()
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.topMargin: 20
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                spacing: 16

                Text { text: (App.langV, App.l("launch.profile")); color: Theme.textMuted; font.pixelSize: 12 }

                LcButton {
                    text: "Usar un servidor LAN"
                    secondary: true
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    onClicked: lanDialog.open()
                }

                LcComboBox {
                    id: launchCombo
                    Layout.fillWidth: true
                    // Menú filtrado por hardware: oculta perfiles de sistema de más
                    // VRAM que el equipo; marca "ready" (modelo+binario presentes).
                    property var launchMenu: App.launchMenu()
                    function refreshMenu() {
                        const sel = launchCombo.currentValue
                        launchCombo.launchMenu = App.launchMenu()
                        const i = launchCombo.indexOfValue(sel)
                        if (i >= 0) launchCombo.currentIndex = i
                    }
                    function selectLaunchProfile(id) {
                        if (!id || id.length === 0) return
                        launchCombo.launchMenu = App.launchMenu()
                        const i = launchCombo.indexOfValue(id)
                        if (i >= 0) {
                            launchCombo.currentIndex = i
                            App.computeEffectiveProfile(id)
                        }
                    }
                    function itemById(id) {
                        for (var i = 0; i < launchMenu.length; i++)
                            if (launchMenu[i].id === id) return launchMenu[i]
                        return null
                    }
                    Connections {
                        target: App.profileManager
                        function onLaunchesChanged() { launchCombo.refreshMenu() }
                    }
                    Connections {
                        target: App
                        // tras descargar deps / escanear, recomputar ready.
                        function onSetupStateChanged() { launchCombo.refreshMenu() }
                        // startServer también lo invocan benchmarks, Tasks y Charla.
                        // Reflejar el perfil realmente activo sin convertir ese swap
                        // interno en la preferencia persistida del usuario.
                        function onActiveLaunchIdChanged() {
                            root.syncToActiveLaunch()
                        }
                        function onLaunchProfileSelected(launchProfileId) {
                            launchCombo.selectLaunchProfile(launchProfileId)
                        }
                    }
                    model: launchMenu
                    textRole: "displayName"
                    valueRole: "id"
                    background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                    contentItem: Text {
                        text: {
                            const _lang = App.langV
                            return launchCombo.displayText.length > 0 ? launchCombo.displayText : App.l("common.selectPlaceholder")
                        }
                        color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 10
                        verticalAlignment: Text.AlignVCenter
                    }
                    // Items no-ready (faltan deps) grisados.
                    delegate: ItemDelegate {
                        width: launchCombo.width
                        highlighted: launchCombo.highlightedIndex === index
                        opacity: (modelData.ready === false) ? 0.45 : 1.0
                        contentItem: Text {
                            text: (modelData.displayName || "")
                                  + (modelData.ready === false ? "  (faltan deps)" : "")
                            color: Theme.theme === "oled" ? "white" : Theme.textPrimary
                            font.pixelSize: 13; leftPadding: 6; elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle { color: highlighted ? Theme.borderColor : Theme.inputBg }
                    }
                    onCurrentValueChanged: {
                        if (!currentValue) return
                        // Recomputar SIEMPRE para que la vista previa refleje ESTE perfil
                        // (si no, queda el comando del perfil anterior = preview stale).
                        App.computeEffectiveProfile(currentValue)
                        if (root._restored && !root._syncingActiveLaunch)
                            App.writeSetting("lastLaunchId", currentValue)
                        const it = itemById(currentValue)
                        if (it && it.ready === false) {
                            // Perfil de sistema sin modelo/binario: ofrecer instalar deps.
                            depsDialog.launchId = currentValue
                            depsDialog.profileName = it.displayName || ""
                            depsDialog.open()
                        }
                    }

                    // Al abrir la app, restaurar el último perfil de lanzamiento usado.
                    Component.onCompleted: {
                        const last = App.readSetting("lastLaunchId", "")
                        if (last && last.length > 0) {
                            const i = launchCombo.indexOfValue(last)
                            if (i >= 0) launchCombo.currentIndex = i
                        }
                        root._restored = true
                        if (App.serverRunning && App.activeLaunchId.length > 0)
                            root.syncToActiveLaunch()
                        else if (launchCombo.currentValue)
                            App.computeEffectiveProfile(launchCombo.currentValue)
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: summaryCol.implicitHeight + 16
                    color: Theme.inputBg
                    radius: 8
                    border.color: Theme.borderColor
                    visible: launchCombo.currentValue !== undefined

                    Column {
                        id: summaryCol
                        anchors { fill: parent; margins: 12 }
                        spacing: 6

                        Repeater {
                            model: [
                                [App.l("launch.binary"),  App.effectiveProfile.binaryPath?.split(/[/\\]/).pop() ?? "—"],
                                [App.l("launch.valid"),   App.effectiveProfile.isValid ? App.l("launch.yes") : App.l("launch.no")],
                            ]
                            delegate: Row {
                                spacing: 8
                                Text { text: modelData[0] + ":"; color: Theme.textMuted; font.pixelSize: 12; width: 60 }
                                Text {
                                    text: modelData[1]
                                    color: modelData[0] === App.l("launch.valid")
                                           ? (modelData[1] === App.l("launch.yes") ? Theme.successText : Theme.errorText)
                                           : Theme.textSecondary
                                    font.pixelSize: 12
                                }
                            }
                        }
                    }
                }

                Text { text: (App.langV, App.l("launch.cmdPreview")); color: Theme.textMuted; font.pixelSize: 12 }

                CommandPreview {
                    Layout.fillWidth: true
                    height: 120
                    commandLine: App.effectiveProfile.commandLine ?? ""
                    warnings: App.effectiveProfile.warnings ?? []
                    errors: App.effectiveProfile.blockingErrors ?? []
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    LcButton {
                        text: {
                            const _lang = App.langV
                            if (App.serverStopping) return "Deteniendo..."
                            if (App.serverRunning) return App.l("launch.stopServer")
                            if (launchCombo.count === 0) return "Crear un perfil primero"
                            return App.l("launch.startServerAndAgent")
                        }
                        danger: App.serverRunning && !App.serverStopping
                        secondary: App.serverStopping
                        Layout.fillWidth: true
                        enabled: !App.serverStopping && launchCombo.count > 0 && (launchCombo.currentValue !== undefined || App.serverRunning)
                        onClicked: {
                            if (App.serverRunning) App.stopServer()
                            else root.startWithPortCheck(true)
                        }
                    }

                    LcButton {
                        text: (App.langV, App.l("launch.preview"))
                        secondary: true
                        enabled: launchCombo.currentValue !== undefined
                        onClicked: App.computeEffectiveProfile(launchCombo.currentValue)
                    }

                    LcButton {
                        text: App.installingOfficialBinary ? "Actualizando binario…" : "Actualizar binario requerido"
                        secondary: true
                        visible: launchCombo.currentValue !== undefined
                                 && App.effectiveProfile !== undefined
                                 && (App.effectiveProfile.blockingErrors ?? []).length > 0
                        enabled: visible && !App.installingOfficialBinary && !App.serverRunning && !App.serverStopping
                        onClicked: App.installRequiredBinaryForProfile(launchCombo.currentValue)
                    }
                }

                // --- Watchdog state + live VRAM meter -----------------------
                Rectangle {
                    Layout.fillWidth: true
                    visible: App.serverState === "restarting" || App.serverState === "failed"
                             || (App.serverStats.gpus !== undefined && App.serverStats.gpus.length > 0)
                    color: Theme.inputBg
                    radius: 8
                    border.color: App.serverState === "failed" ? Theme.errorText
                                  : App.serverState === "restarting" ? "#d08a2a"
                                  : Theme.borderColor
                    implicitHeight: statsCol.implicitHeight + 20

                    ColumnLayout {
                        id: statsCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                        spacing: 8

                        // Watchdog state badge
                        RowLayout {
                            Layout.fillWidth: true
                            visible: App.serverState === "restarting" || App.serverState === "failed"
                            spacing: 8
                            Rectangle {
                                width: 10; height: 10; radius: 5
                                color: App.serverState === "failed" ? Theme.errorText : "#d08a2a"
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                color: App.serverState === "failed" ? Theme.errorText : Theme.textSecondary
                                font.pixelSize: 12
                                text: App.serverState === "failed"
                                      ? "Watchdog: server crasheó repetidamente. Auto-reinicio detenido."
                                      : "Watchdog: reiniciando server tras crash…"
                            }
                        }

                        // Per-GPU VRAM bars
                        Repeater {
                            model: App.serverStats.gpus ?? []
                            delegate: ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 2
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        Layout.fillWidth: true
                                        text: "GPU " + modelData.index + " · " + modelData.name
                                        color: Theme.textMuted; font.pixelSize: 11; elide: Text.ElideRight
                                    }
                                    Text {
                                        text: Math.round(modelData.usedMb) + " / " + Math.round(modelData.totalMb) + " MB"
                                        color: Theme.textSecondary; font.pixelSize: 11
                                    }
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    height: 8; radius: 4
                                    color: Theme.borderColor
                                    Rectangle {
                                        height: parent.height; radius: 4
                                        width: parent.width * Math.min(1, modelData.pct / 100)
                                        color: modelData.pct >= 90 ? Theme.errorText
                                               : modelData.pct >= 75 ? "#d08a2a"
                                               : Theme.successText
                                        Behavior on width { NumberAnimation { duration: 300 } }
                                    }
                                }
                                // Power draw / limit (sólo si nvidia-smi reporta telemetría)
                                RowLayout {
                                    Layout.fillWidth: true
                                    visible: (modelData.limitW ?? 0) > 0
                                    Text {
                                        Layout.fillWidth: true
                                        text: "potencia"
                                        color: Theme.textMuted; font.pixelSize: 11
                                    }
                                    Text {
                                        text: Math.round(modelData.drawW) + " / " + Math.round(modelData.limitW) + " W"
                                        color: Theme.textSecondary; font.pixelSize: 11
                                    }
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    visible: (modelData.limitW ?? 0) > 0
                                    height: 8; radius: 4
                                    color: Theme.borderColor
                                    Rectangle {
                                        height: parent.height; radius: 4
                                        width: parent.width * Math.min(1, (modelData.powerPct ?? 0) / 100)
                                        color: "#5a8ad0"
                                        Behavior on width { NumberAnimation { duration: 300 } }
                                    }
                                }
                            }
                        }
                    }
                }

                LcButton {
                    text: (App.langV, App.l("launch.startServerOnly"))
                    secondary: true
                    Layout.fillWidth: true
                    visible: !App.serverRunning && !App.serverStopping
                    enabled: launchCombo.count > 0 && launchCombo.currentValue !== undefined
                    onClicked: root.startWithPortCheck(false)
                }

                // --- Router mode (hot-swap entre varios modelos) ---------------
                Rectangle {
                    id: routerBox
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    color: Theme.inputBg
                    radius: 8
                    border.color: Theme.borderColor
                    implicitHeight: routerCol.implicitHeight + 24
                    visible: !App.serverStopping

                    // profileId -> bool (selección del pool)
                    property var pool: ({})
                    property var rmodels: []

                    Connections {
                        target: App
                        function onRouterStateChanged() { routerBox.rmodels = App.routerModelNames() }
                    }

                    ColumnLayout {
                        id: routerCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
                        spacing: 8

                        Text { text: "Router (hot-swap)"; color: Theme.textPrimary; font.pixelSize: 13; font.bold: true }
                        Text {
                            text: App.serverIsRouter
                                  ? "Router activo. Modelo activo abajo; chat/agente swappean al usarlo."
                                  : "Un solo server con varios modelos, swap en ~1s. Elegí el pool:"
                            color: Theme.textMuted; font.pixelSize: 11
                            Layout.fillWidth: true; wrapMode: Text.WordWrap
                        }

                        // Pool: lista scrollable de checkboxes sobre los launch profiles
                        ScrollView {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 220
                            visible: !App.serverRunning
                            clip: true
                            ScrollBar.vertical.policy: ScrollBar.AsNeeded

                            ColumnLayout {
                                width: parent.width
                                spacing: 2
                                Repeater {
                                    model: App.profileManager.launchProfiles
                                    delegate: LcCheckBox {
                                        id: poolCheck
                                        required property string name
                                        required property string profileId
                                        Layout.fillWidth: true
                                        text: name
                                        checked: !!routerBox.pool[profileId]
                                        onToggled: {
                                            var p = routerBox.pool
                                            p[profileId] = checked
                                            routerBox.pool = p
                                        }
                                    }
                                }
                            }
                        }

                        Text {
                            visible: !App.serverRunning
                            text: {
                                var n = 0
                                for (var k in routerBox.pool) if (routerBox.pool[k]) n++
                                return n + " perfil(es) en el pool"
                            }
                            color: Theme.textMuted; font.pixelSize: 11
                        }

                        LcButton {
                            text: App.serverRunning
                                  ? (App.serverIsRouter ? "Detener router" : "Detener server")
                                  : "Iniciar router"
                            danger: App.serverRunning
                            // Acción secundaria: el celeste se reserva para "servidor + agente".
                            secondary: !App.serverRunning
                            Layout.fillWidth: true
                            enabled: App.serverRunning || (function(){
                                for (var k in routerBox.pool) if (routerBox.pool[k]) return true
                                return false
                            })()
                            onClicked: {
                                if (App.serverRunning) { App.stopServer(); return }
                                var ids = []
                                for (var k in routerBox.pool) if (routerBox.pool[k]) ids.push(k)
                                if (ids.length > 0) App.startRouter(ids, 1)
                            }
                        }

                        // Selector de modelo activo (visible con router corriendo)
                        LcComboBox {
                            id: activeModelCombo
                            Layout.fillWidth: true
                            visible: App.serverIsRouter && routerBox.rmodels.length > 0
                            model: routerBox.rmodels
                            background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                            contentItem: Text {
                                text: activeModelCombo.displayText
                                color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 10
                                verticalAlignment: Text.AlignVCenter
                            }
                            onActivated: App.setRouterActiveModel(currentText)
                            onModelChanged: {
                                var a = App.routerActiveModel()
                                for (var j = 0; j < model.length; j++) if (model[j] === a) currentIndex = j
                            }
                        }
                    }
                }

                // OpenAI-compatible endpoint for external agents — shown while running.
                ColumnLayout {
                    id: endpointBox
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    spacing: 4
                    visible: App.serverRunning

                    readonly property string endpointUrl: App.serverBaseUrl + "/v1"

                    Text {
                        text: (App.langV, App.l("launch.endpointLabel"))
                        color: Theme.textMuted
                        font.pixelSize: 12
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Rectangle {
                            Layout.fillWidth: true
                            height: 36
                            color: Theme.inputBg
                            border.color: Theme.borderColor
                            border.width: 1
                            radius: 4

                            TextInput {
                                id: endpointField
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                verticalAlignment: TextInput.AlignVCenter
                                text: endpointBox.endpointUrl
                                color: Theme.textPrimary
                                font.family: Theme.codeFont
                                font.pixelSize: 13
                                readOnly: true
                                selectByMouse: true
                            }
                        }

                        LcButton {
                            id: endpointCopyBtn
                            property bool copied: false
                            text: copied ? (App.langV, App.l("cmd.copied")) : (App.langV, App.l("cmd.copy"))
                            secondary: true
                            onClicked: {
                                App.copyToClipboard(endpointField.text)
                                endpointCopyBtn.copied = true
                                endpointCopyTimer.start()
                            }
                            Timer {
                                id: endpointCopyTimer
                                interval: 1500
                                onTriggered: endpointCopyBtn.copied = false
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 44
            color: Theme.navBg
            border.color: Theme.borderColor
            border.width: 1

            Row {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 16
                spacing: 8

                LcButton {
                    text: {
                        const _lang = App.langV
                        return root.logVisible ? App.l("launch.hideLog") : App.l("launch.showLog")
                    }
                    secondary: true
                    onClicked: root.logVisible = !root.logVisible
                }
                LcButton {
                    text: (App.langV, App.l("launch.clear"))
                    secondary: true
                    onClicked: App.clearLog()
                    enabled: root.logVisible
                }
                LcButton {
                    text: (App.langV, App.l("launch.copyLogs"))
                    secondary: true
                    enabled: root.logVisible
                    onClicked: App.copyToClipboard(App.serverLog)
                }
            }
        }

        Rectangle {
            visible: root.logVisible
            Layout.fillWidth: true
            Layout.preferredHeight: root.logVisible ? root.logHeight : 0
            Layout.maximumHeight: root.height * 0.75
            color: Theme.logBg
            border.color: Theme.borderColor
            border.width: 1
            clip: true

            Behavior on Layout.preferredHeight {
                NumberAnimation { duration: 120; easing.type: Easing.OutQuad }
            }

            Rectangle {
                id: splitter
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 8
                color: dragArea.containsMouse ? Theme.splitterHover : Theme.splitterNormal

                MouseArea {
                    id: dragArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.SizeVerCursor
                    property real startY: 0
                    property real startHeight: 0

                    onPressed: function(mouse) {
                        startY = mouse.y
                        startHeight = root.logHeight
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed) return
                        const delta = mouse.y - startY
                        const next = startHeight - delta
                        const maxH = root.height * 0.75
                        root.logHeight = Math.max(root.minLogHeight, Math.min(maxH, next))
                    }
                }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.topMargin: 10
                anchors.margins: 8
                spacing: 6

                Text {
                    text: (App.langV, App.l("launch.serverLog"))
                    color: Theme.textMuted
                    font.pixelSize: 12
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.inputBg
                    radius: 8
                    border.color: Theme.borderColor
                    clip: true

                    ScrollView {
                        anchors.fill: parent
                        anchors.margins: 8
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                        TextArea {
                            readOnly: true
                            text: App.serverLog
                            color: Theme.textSecondary
                            font { family: Theme.codeFont; pixelSize: 12 }
                            wrapMode: TextArea.WrapAnywhere
                            background: null
                            onTextChanged: cursorPosition = text.length
                        }
                    }
                }
            }
        }
    }

    Popup {
        id: portCollisionDialog
        parent: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(520, parent.width - 48)
        height: 250
        padding: 18
        closePolicy: Popup.CloseOnEscape
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)

        background: Rectangle {
            color: Theme.popupBg
            radius: 8
            border.color: Theme.popupBorderColor
            border.width: 1
        }

        contentItem: ColumnLayout {
            width: portCollisionDialog.availableWidth
            spacing: 14

            Text {
                Layout.fillWidth: true
                text: "Puerto ocupado"
                color: Theme.textPrimary
                font.pixelSize: 16
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                text: "El perfil usa " + root.pendingPortHost + ":" + root.pendingPortCurrent
                      + ", pero ese puerto ya está en uso. Hay un puerto libre disponible: "
                      + root.pendingPortSuggested + "."
                color: Theme.textSecondary
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                text: "Si aceptás, se actualiza el puerto del BackendProfile y se vuelve a lanzar el servidor con ese mismo perfil."
                color: Theme.textMuted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                LcButton {
                    text: "Cancelar"
                    secondary: true
                    onClicked: portCollisionDialog.close()
                }
                LcButton {
                    text: "Usar puerto " + root.pendingPortSuggested
                    onClicked: {
                        const launchId = root.pendingPortLaunchId
                        const port = root.pendingPortSuggested
                        const startAgent = root.pendingPortStartAgent
                        portCollisionDialog.close()
                        App.useSuggestedServerPort(launchId, port, startAgent)
                    }
                }
            }
        }
    }
}
