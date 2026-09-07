import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0
import "../components"

// Modo Charla (voz-a-voz): hablar con la IA y escuchar la respuesta.
// STT→backend de chat→TTS. La config (local/cloud) vive en App.voiceConfig().
Item {
    id: page

    // La config de voz vive en el LaunchProfile activo (el que lanzó el server).
    property string pid: App.activeLaunchId
    property var cfg: ({})
    property bool testing: false
    property int installPct: -1
    property string installMsg: ""       // instalación de modelo STT (whisper)
    property int voiceInstallPct: -1
    property string voiceInstallMsg: ""  // instalación de voz piper (TTS)
    property string binMsgWhisper: ""
    property string binMsgPiper: ""
    property string whisperPath: App.voiceWhisperServerPath()
    property string piperPath: App.voicePiperPath()

    // Las descargas de STT (modelo whisper) y TTS (voz piper) comparten señales;
    // rutear el feedback a la sección correcta según el id (antes todo caía en
    // "Estado" del STT y parecía que el botón de piper instalaba whisper).
    function isTtsVoiceId(id) {
        var o = App.voiceTtsCatalog()
        for (var i = 0; i < o.length; ++i) if (o[i].id === id) return true
        return false
    }
    Connections {
        target: App
        function onVoiceInstallProgress(engineId, pct, status) {
            if (page.isTtsVoiceId(engineId)) { page.voiceInstallPct = pct; page.voiceInstallMsg = status }
            else { page.installPct = pct; page.installMsg = status }
        }
        function onVoiceInstallFinished(engineId, ok, message) {
            if (page.isTtsVoiceId(engineId)) {
                page.voiceInstallPct = -1
                page.voiceInstallMsg = ok ? "Instalada ✓" : ("Error: " + message)
            } else {
                page.installPct = -1
                page.installMsg = ok ? "Instalado ✓" : ("Error: " + message)
            }
            sttEngineCombo.refresh()
            ttsVoiceCombo.refresh()
        }
        function onVoiceBinaryInstalled(kind, ok, message) {
            page.whisperPath = App.voiceWhisperServerPath()
            page.piperPath = App.voicePiperPath()
            var msg = ok ? "Binario instalado ✓" : ("Error binario: " + message)
            if (kind === "piper") page.binMsgPiper = msg
            else page.binMsgWhisper = msg
        }
    }
    function reload() {
        cfg = pid.length ? App.voiceConfig(pid) : ({})
        // Voz por defecto si el perfil guardó vacío (el runtime usa la default del
        // idioma igual; sin esto la UI mostraba "No instalada" sin botón).
        if (!cfg.ttsManagedVoice) cfg.ttsManagedVoice = "es_ES-davefx-medium"
    }
    function startOrPromptModelDownload() {
        const engineId = page.cfg.sttManagedEngine || ""
        const voiceId = page.cfg.ttsManagedVoice || "es_ES-davefx-medium"
        if (engineId.length > 0
                && (!App.voiceModelInstalled(engineId)
                    || !App.voiceWhisperServerAvailable()
                    || !App.voiceTtsVoiceInstalled(voiceId)
                    || !App.voicePiperAvailable())) {
            missingSttModelDialog.engineId = engineId
            missingSttModelDialog.open()
            return
        }
        // Perfil activo con ajustes recomendados para voz: ofrecer aplicarlos
        // (relanza el server con overrides temporales). Con auto-tune activado,
        // App.startCharla() los aplica solo sin preguntar.
        var recs = App.charlaTuneRecommendations()
        if (recs.length > 0 && !App.charlaAutoTune()) {
            charlaTuneDialog.recs = recs
            charlaTuneDialog.open()
            return
        }
        App.startCharla()
    }
    // Persiste y REASIGNA cfg (copia) para que QML reevalúe los bindings `visible`
    // que dependen de cfg (mutar un campo no notifica el cambio del var property).
    function save() { if (pid.length) App.setVoiceConfig(pid, cfg); cfg = Object.assign({}, cfg) }
    onPidChanged: reload()
    Component.onCompleted: reload()

    readonly property string st: App.voiceState
    readonly property var stateColor: ({
        "idle": Theme.textMuted, "listening": Theme.accent,
        "transcribing": "#e0a93b", "thinking": "#9b6dd6",
        "speaking": "#3bbf6e", "error": Theme.btnDangerBg
    })
    readonly property var stateLabel: ({
        "idle": "Listo", "listening": "Escuchando…", "transcribing": "Transcribiendo…",
        "thinking": "Pensando…", "speaking": "Hablando…", "error": "Error"
    })

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader { title: "🎙  Ingi Charla"; subtitle: "Ingi, tu ingeniero asistente: hablá y él usa tu computadora por vos (clic, teclado, instalar programas, mirar tus pantallas)" }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ── Panel izquierdo: orbe + controles ──
            ColumnLayout {
                Layout.preferredWidth: 360
                Layout.fillHeight: true
                Layout.margins: 24
                spacing: 20

                Item { Layout.fillHeight: true }

                // Orbe de estado con anillo de nivel.
                Item {
                    Layout.alignment: Qt.AlignHCenter
                    width: 220; height: 220

                    Rectangle {            // anillo de nivel de micrófono
                        anchors.centerIn: parent
                        width: 160 + App.voiceLevel * 600
                        height: width
                        radius: width / 2
                        color: "transparent"
                        border.color: page.stateColor[page.st] || Theme.textMuted
                        border.width: 2
                        opacity: page.st === "listening" || page.st === "speaking" ? 0.6 : 0.15
                        Behavior on width { NumberAnimation { duration: 90 } }
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        width: 150; height: 150; radius: 75
                        color: page.stateColor[page.st] || Theme.textMuted
                        opacity: 0.9
                        Text {
                            anchors.centerIn: parent
                            text: page.st === "speaking" ? "🔊" : "🎙"
                            font.pixelSize: 56
                        }
                    }
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: page.stateLabel[page.st] || page.st
                    color: Theme.textPrimary
                    font { pixelSize: 18; bold: true }
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    visible: App.voiceError.length > 0
                    text: App.voiceError
                    color: Theme.btnDangerBg
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    Layout.maximumWidth: 320
                    horizontalAlignment: Text.AlignHCenter
                }

                // Transcripción parcial en vivo del turno en curso.
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.maximumWidth: 320
                    visible: App.voicePartial.length > 0
                    text: "“" + App.voicePartial + "”"
                    color: Theme.textSecondary
                    font { pixelSize: 13; italic: true }
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                }

                // Medidor de nivel de micrófono (siempre visible).
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text { text: "Nivel de entrada"; color: Theme.textMuted; font.pixelSize: 11 }
                    Rectangle {
                        Layout.fillWidth: true
                        height: 12; radius: 6
                        color: Theme.inputBg
                        border.color: Theme.borderColor
                        Rectangle {
                            anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 2 }
                            width: Math.max(0, Math.min(1, App.voiceLevel * 6)) * (parent.width - 4)
                            radius: 5
                            color: App.voiceLevel > 0.03 ? "#3bbf6e" : Theme.accent
                            Behavior on width { NumberAnimation { duration: 70 } }
                        }
                    }
                }

                // Selección de micrófono.
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text { text: "Micrófono"; color: Theme.textMuted; font.pixelSize: 11 }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        LcComboBox {
                            id: micCombo
                            Layout.fillWidth: true
                            property var devs: App.audioInputDevices()
                            textRole: "name"
                            model: devs
                            Component.onCompleted: {
                                var cur = App.voiceInputDevice()
                                for (var i = 0; i < devs.length; ++i)
                                    if (devs[i].id === cur) { currentIndex = i; break }
                            }
                            onActivated: if (devs[currentIndex]) App.setVoiceInputDevice(devs[currentIndex].id)
                        }
                        LcButton {
                            text: "↻"
                            secondary: true
                            onClicked: { micCombo.devs = App.audioInputDevices(); micCombo.model = micCombo.devs }
                        }
                    }
                }

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 12
                    LcButton {
                        text: (App.voiceActive && !page.testing) ? "Detener" : "Iniciar charla"
                        danger: App.voiceActive && !page.testing
                        enabled: !page.testing
                        onClicked: {
                            if (App.voiceActive) App.stopCharla()
                            else page.startOrPromptModelDownload()
                        }
                    }
                    LcButton {
                        text: page.testing ? "Detener prueba" : "Probar micrófono"
                        secondary: true
                        enabled: !App.voiceActive || page.testing
                        onClicked: {
                            if (page.testing) { App.stopMicTest(); page.testing = false }
                            else { App.startMicTest(); page.testing = true }
                        }
                    }
                }
                LcButton {
                    Layout.fillWidth: true
                    text: App.dictationActive ? "Detener dictado y copiar" : "Iniciar dictado literal"
                    secondary: true
                    enabled: !App.voiceActive || App.dictationActive
                    onClicked: App.toggleDictation()
                }
                Text {
                    Layout.fillWidth: true
                    visible: App.dictationActive || App.dictationText.length > 0
                    text: App.dictationActive
                          ? "Hablá y volvé a pulsar para terminar. El resultado queda en el portapapeles."
                          : "Copiado: “" + App.dictationText + "”"
                    color: Theme.textMuted; font.pixelSize: 11; wrapMode: Text.WordWrap
                }
                LcButton {
                    Layout.alignment: Qt.AlignHCenter
                    text: "Hablar ahora"
                    secondary: true
                    visible: App.voiceActive && !page.testing
                    onClicked: App.charlaListen()
                }

                Item { Layout.fillHeight: true }
            }

            Rectangle { width: 1; Layout.fillHeight: true; color: Theme.divider }

            // ── Panel derecho: configuración ──
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true

                ColumnLayout {
                    width: parent.width
                    spacing: 14

                    Item { height: 8; width: 1 }

                    Text { text: "Motor STT gestionado (la app descarga y lanza)"; color: Theme.textPrimary; Layout.leftMargin: 24; font { pixelSize: 15; bold: true } }
                    GridLayout {
                        columns: 2; columnSpacing: 12; rowSpacing: 8
                        Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.fillWidth: true

                        Text { text: "Motor"; color: Theme.textSecondary }
                        LcComboBox {
                            id: sttEngineCombo
                            Layout.fillWidth: true
                            property var opts: [{ id: "", name: "Manual (endpoint propio, abajo)" }].concat(App.voiceSttCatalog())
                            function refresh() { opts = [{ id: "", name: "Manual (endpoint propio, abajo)" }].concat(App.voiceSttCatalog()) }
                            textRole: "name"
                            model: opts
                            Component.onCompleted: {
                                var cur = page.cfg.sttManagedEngine || ""
                                for (var i = 0; i < opts.length; ++i) if (opts[i].id === cur) { currentIndex = i; break }
                            }
                            onActivated: { page.cfg.sttManagedEngine = opts[currentIndex].id; page.save() }
                        }

                        Text { text: "Estado"; color: Theme.textSecondary; visible: (page.cfg.sttManagedEngine || "") !== "" }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            visible: (page.cfg.sttManagedEngine || "") !== ""
                            Text {
                                Layout.fillWidth: true
                                color: Theme.textMuted; font.pixelSize: 12
                                text: page.installPct >= 0 ? page.installMsg
                                      : (App.voiceModelInstalled(page.cfg.sttManagedEngine || "") ? "Instalado ✓"
                                         : (page.installMsg.length ? page.installMsg : "No instalado"))
                            }
                            LcButton {
                                text: page.installPct >= 0 ? "Cancelar" : "Instalar modelo"
                                secondary: true
                                visible: page.installPct >= 0 || !App.voiceModelInstalled(page.cfg.sttManagedEngine || "")
                                onClicked: {
                                    if (page.installPct >= 0) App.cancelVoiceModelInstall()
                                    else App.installVoiceModel(page.cfg.sttManagedEngine)
                                }
                            }
                        }

                        Text { text: "Binario whisper-server"; color: Theme.textSecondary; visible: (page.cfg.sttManagedEngine || "") !== "" }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 6
                            visible: (page.cfg.sttManagedEngine || "") !== ""
                            LcTextField {
                                Layout.fillWidth: true
                                placeholderText: "ruta a whisper-server (vacío = PATH)"
                                text: page.whisperPath
                                onEditingFinished: { App.setVoiceWhisperServerPath(text); page.whisperPath = text }
                            }
                            LcButton { text: "Descargar"; secondary: true; onClicked: App.installVoiceBinary("whisper-server", "") }
                            LcButton { text: "…"; secondary: true; onClicked: { App.pickVoiceWhisperServer(); page.whisperPath = App.voiceWhisperServerPath() } }
                        }
                        Item { visible: page.binMsgWhisper.length > 0; width: 1; height: 1 }
                        Text {
                            visible: page.binMsgWhisper.length > 0
                            text: page.binMsgWhisper; color: Theme.textMuted; font.pixelSize: 12
                        }
                    }

                    Text {
                        text: "Reconocimiento de voz (STT) — endpoint manual"
                        color: Theme.textPrimary; Layout.leftMargin: 24; font { pixelSize: 15; bold: true }
                        visible: (page.cfg.sttManagedEngine || "") === ""
                    }
                    GridLayout {
                        columns: 2; columnSpacing: 12; rowSpacing: 8
                        Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.fillWidth: true
                        visible: (page.cfg.sttManagedEngine || "") === ""

                        Text { text: "Proveedor"; color: Theme.textSecondary }
                        LcComboBox {
                            Layout.fillWidth: true
                            model: ["local", "cloud"]
                            currentIndex: (page.cfg.sttProvider === "cloud") ? 1 : 0
                            onActivated: { page.cfg.sttProvider = model[currentIndex]; page.save() }
                        }
                        Text { text: "Endpoint (baseUrl)"; color: Theme.textSecondary }
                        LcTextField {
                            Layout.fillWidth: true
                            text: page.cfg.sttBaseUrl || ""
                            onEditingFinished: { page.cfg.sttBaseUrl = text; page.save() }
                        }
                        Text { text: "Modelo"; color: Theme.textSecondary }
                        LcTextField {
                            Layout.fillWidth: true
                            text: page.cfg.sttModel || ""
                            onEditingFinished: { page.cfg.sttModel = text; page.save() }
                        }
                        Text { text: "Idioma"; color: Theme.textSecondary }
                        LcTextField {
                            Layout.fillWidth: true
                            text: page.cfg.sttLanguage || "auto"
                            onEditingFinished: { page.cfg.sttLanguage = text; page.save() }
                        }
                        Text { text: "API key ref"; color: Theme.textSecondary; visible: page.cfg.sttProvider === "cloud" }
                        LcTextField {
                            Layout.fillWidth: true
                            visible: page.cfg.sttProvider === "cloud"
                            placeholderText: "ej: voice/openai (env var o store)"
                            text: page.cfg.sttKeyRef || ""
                            onEditingFinished: { page.cfg.sttKeyRef = text; page.save() }
                        }
                    }

                    Text { text: "Motor TTS (automático o manual)"; color: Theme.textPrimary; Layout.leftMargin: 24; font { pixelSize: 15; bold: true } }
                    Rectangle {
                        Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.fillWidth: true
                        implicitHeight: capabilityText.implicitHeight + 18
                        radius: 6; color: Theme.inputBg; border.color: Theme.borderColor
                        property var cap: App.charlaAgentCapability()
                        Text {
                            id: capabilityText
                            anchors.fill: parent; anchors.margins: 9
                            wrapMode: Text.WordWrap; color: Theme.textSecondary; font.pixelSize: 12
                            text: "Capacidad del agente: " + (parent.cap.level || "desconocida")
                                  + " — " + (parent.cap.reason || "")
                                  + (parent.cap.requireSupervisor ? " · Se escalarán tareas complejas al supervisor." : "")
                        }
                    }
                    GridLayout {
                        columns: 2; columnSpacing: 12; rowSpacing: 8
                        Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.fillWidth: true

                        Text { text: "Modo"; color: Theme.textSecondary }
                        LcComboBox {
                            Layout.fillWidth: true
                            model: ["auto", "piper", "kokoro", "qwen3", "inflect", "http"]
                            currentIndex: Math.max(0, model.indexOf(page.cfg.ttsMode || "auto"))
                            onActivated: { page.cfg.ttsMode = model[currentIndex]; page.save() }
                        }

                        Text { text: "Recomendación"; color: Theme.textSecondary; visible: page.cfg.ttsMode === "auto" }
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textMuted
                            visible: page.cfg.ttsMode === "auto"
                            property var rec: App.recommendedVoiceTts(App.activeLaunchId)
                            text: (rec.mode || "-") + " — " + (rec.reason || "")
                        }

                        Text { text: "Fallback"; color: Theme.textSecondary }
                        LcComboBox {
                            Layout.fillWidth: true
                            model: ["piper", "http", "none"]
                            currentIndex: Math.max(0, model.indexOf(page.cfg.ttsFallbackMode || "piper"))
                            onActivated: { page.cfg.ttsFallbackMode = model[currentIndex]; page.save() }
                        }

                        Text { text: "Voz piper"; color: Theme.textSecondary; visible: page.cfg.ttsMode === "piper" || page.cfg.ttsMode === "auto" }
                        LcComboBox {
                            id: ttsVoiceCombo
                            Layout.fillWidth: true
                            visible: page.cfg.ttsMode === "piper" || page.cfg.ttsMode === "auto"
                            property var opts: App.voiceTtsCatalog()
                            function refresh() { opts = App.voiceTtsCatalog() }
                            textRole: "name"
                            model: opts
                            Component.onCompleted: {
                                var cur = page.cfg.ttsManagedVoice || ""
                                for (var i = 0; i < opts.length; ++i) if (opts[i].id === cur) { currentIndex = i; break }
                            }
                            onActivated: { page.cfg.ttsManagedVoice = opts[currentIndex].id; page.save() }
                        }

                        Text { text: "Estado voz"; color: Theme.textSecondary; visible: page.cfg.ttsMode === "piper" || page.cfg.ttsMode === "auto" }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            visible: page.cfg.ttsMode === "piper" || page.cfg.ttsMode === "auto"
                            Text {
                                Layout.fillWidth: true; color: Theme.textMuted; font.pixelSize: 12
                                text: page.voiceInstallPct >= 0 ? page.voiceInstallMsg
                                      : (App.voiceTtsVoiceInstalled(page.cfg.ttsManagedVoice || "") ? "Instalada ✓"
                                         : (page.voiceInstallMsg.length ? page.voiceInstallMsg : "No instalada"))
                            }
                            LcButton {
                                text: "Instalar voz"
                                secondary: true
                                visible: page.voiceInstallPct < 0
                                         && (page.cfg.ttsManagedVoice || "") !== ""
                                         && !App.voiceTtsVoiceInstalled(page.cfg.ttsManagedVoice || "")
                                onClicked: App.installVoiceTts(page.cfg.ttsManagedVoice)
                            }
                        }

                        Text { text: "Binario piper"; color: Theme.textSecondary; visible: page.cfg.ttsMode === "piper" || page.cfg.ttsMode === "auto" }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 6
                            visible: page.cfg.ttsMode === "piper" || page.cfg.ttsMode === "auto"
                            LcTextField {
                                Layout.fillWidth: true
                                placeholderText: "ruta a piper (vacío = PATH)"
                                text: page.piperPath
                                onEditingFinished: { App.setVoicePiperPath(text); page.piperPath = text }
                            }
                            LcButton { text: "Descargar"; secondary: true; onClicked: App.installVoiceBinary("piper", "") }
                            LcButton { text: "…"; secondary: true; onClicked: { App.pickVoicePiper(); page.piperPath = App.voicePiperPath() } }
                        }
                        Item { visible: page.binMsgPiper.length > 0; width: 1; height: 1 }
                        Text {
                            visible: page.binMsgPiper.length > 0
                            text: page.binMsgPiper; color: Theme.textMuted; font.pixelSize: 12
                        }
                    }

                    Text {
                        text: "Qwen3-TTS local (GGUF, clonación opcional)"
                        color: Theme.textPrimary
                        Layout.leftMargin: 24
                        font.pixelSize: 15
                        font.bold: true
                        visible: page.cfg.ttsMode === "qwen3" || page.cfg.ttsMode === "auto"
                    }
                    GridLayout {
                        columns: 2; columnSpacing: 12; rowSpacing: 8
                        Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.fillWidth: true
                        visible: page.cfg.ttsMode === "qwen3" || page.cfg.ttsMode === "auto"
                        Text { text: "Binario"; color: Theme.textSecondary }
                        LcTextField { Layout.fillWidth: true; placeholderText: "qwen3-tts-cli.exe o PATH"; text: page.cfg.qwenBinaryPath || ""; onEditingFinished: { page.cfg.qwenBinaryPath = text; page.save() } }
                        Text { text: "Carpeta modelos"; color: Theme.textSecondary }
                        LcTextField { Layout.fillWidth: true; placeholderText: "carpeta con GGUFs Qwen3-TTS"; text: page.cfg.qwenModelDir || ""; onEditingFinished: { page.cfg.qwenModelDir = text; page.save() } }
                        Text { text: "Modelo"; color: Theme.textSecondary }
                        LcComboBox { Layout.fillWidth: true; model: ["qwen-talker-0.6b-base-Q8_0.gguf", "qwen-talker-1.7b-base-Q8_0.gguf", "qwen-talker-1.7b-customvoice-Q8_0.gguf"]; currentIndex: Math.max(0, model.indexOf(page.cfg.qwenModelName || model[0])); onActivated: { page.cfg.qwenModelName = model[currentIndex]; page.save() } }
                        Text { text: "Embedding de voz"; color: Theme.textSecondary }
                        LcTextField { Layout.fillWidth: true; placeholderText: "speaker.json (opcional)"; text: page.cfg.qwenSpeakerEmbedding || ""; onEditingFinished: { page.cfg.qwenSpeakerEmbedding = text; page.save() } }
                        Text { text: "WAV referencia"; color: Theme.textSecondary }
                        LcTextField { Layout.fillWidth: true; placeholderText: "voz.wav (alternativa al embedding)"; text: page.cfg.qwenReferenceWav || ""; onEditingFinished: { page.cfg.qwenReferenceWav = text; page.save() } }
                        Text { text: "Transcripción referencia"; color: Theme.textSecondary }
                        LcTextField { Layout.fillWidth: true; text: page.cfg.qwenReferenceText || ""; onEditingFinished: { page.cfg.qwenReferenceText = text; page.save() } }
                        Text { text: "Instrucción de estilo"; color: Theme.textSecondary }
                        LcTextField { Layout.fillWidth: true; placeholderText: "tono calmo, docente..."; text: page.cfg.qwenInstruction || ""; onEditingFinished: { page.cfg.qwenInstruction = text; page.save() } }
                    }

                    Text {
                        text: "Inflect v2 ONNX (experimental · sólo inglés · voz masculina fija)"
                        color: Theme.textPrimary
                        Layout.leftMargin: 24
                        font { pixelSize: 15; bold: true }
                        visible: page.cfg.ttsMode === "inflect"
                    }
                    GridLayout {
                        columns: 2; columnSpacing: 12; rowSpacing: 8
                        Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.fillWidth: true
                        visible: page.cfg.ttsMode === "inflect"

                        Text { text: "Requisito"; color: Theme.textSecondary }
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.WordWrap
                            color: (page.cfg.sttLanguage || "").toLowerCase().indexOf("en") === 0
                                   ? Theme.textMuted : Theme.errorText
                            text: "LlamaCode debe estar en inglés (STT “en”). No se selecciona automáticamente."
                        }
                        Text { text: "Python"; color: Theme.textSecondary }
                        LcTextField {
                            Layout.fillWidth: true
                            placeholderText: "python o ruta a python.exe"
                            text: page.cfg.inflectPythonPath || "python"
                            onEditingFinished: { page.cfg.inflectPythonPath = text; page.save() }
                        }
                        Text { text: "Carpeta modelo"; color: Theme.textSecondary }
                        LcTextField {
                            Layout.fillWidth: true
                            placeholderText: "Inflect-Nano-v2-ONNX o Inflect-Micro-v2-ONNX"
                            text: page.cfg.inflectModelDir || ""
                            onEditingFinished: { page.cfg.inflectModelDir = text; page.save() }
                        }
                        Text { text: "Proveedor ONNX"; color: Theme.textSecondary }
                        LcComboBox {
                            Layout.fillWidth: true
                            model: ["cpu", "directml", "cuda"]
                            currentIndex: Math.max(0, model.indexOf(page.cfg.inflectProvider || "cpu"))
                            onActivated: { page.cfg.inflectProvider = model[currentIndex]; page.save() }
                        }
                    }

                    Text {
                        text: "Síntesis de voz (TTS) — endpoint HTTP"
                        color: Theme.textPrimary; Layout.leftMargin: 24; font { pixelSize: 15; bold: true }
                        visible: page.cfg.ttsMode === "http" || page.cfg.ttsMode === "kokoro"
                    }
                    GridLayout {
                        columns: 2; columnSpacing: 12; rowSpacing: 8
                        Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.fillWidth: true
                        visible: page.cfg.ttsMode === "http" || page.cfg.ttsMode === "kokoro"

                        Text { text: "Proveedor"; color: Theme.textSecondary }
                        LcComboBox {
                            Layout.fillWidth: true
                            model: ["local", "cloud"]
                            currentIndex: (page.cfg.ttsProvider === "cloud") ? 1 : 0
                            onActivated: { page.cfg.ttsProvider = model[currentIndex]; page.save() }
                        }
                        Text { text: "Streaming PCM"; color: Theme.textSecondary }
                        LcCheckBox {
                            text: "Reproducir mientras llega"
                            checked: page.cfg.ttsStreamAudio === true
                            onToggled: {
                                page.cfg.ttsStreamAudio = checked
                                if (checked) page.cfg.ttsFormat = "pcm"
                                page.save()
                            }
                        }
                        Text { text: "PCM (Hz / canales)"; color: Theme.textSecondary; visible: page.cfg.ttsStreamAudio === true }
                        RowLayout {
                            Layout.fillWidth: true; visible: page.cfg.ttsStreamAudio === true
                            LcTextField {
                                Layout.fillWidth: true; text: String(page.cfg.ttsPcmSampleRate || 24000)
                                onEditingFinished: { page.cfg.ttsPcmSampleRate = parseInt(text) || 24000; page.save() }
                            }
                            LcComboBox {
                                model: ["1", "2"]
                                currentIndex: Math.max(0, model.indexOf(String(page.cfg.ttsPcmChannels || 1)))
                                onActivated: { page.cfg.ttsPcmChannels = parseInt(model[currentIndex]); page.save() }
                            }
                        }
                        Text { text: "Endpoint (baseUrl)"; color: Theme.textSecondary }
                        LcTextField {
                            Layout.fillWidth: true
                            text: page.cfg.ttsBaseUrl || ""
                            onEditingFinished: { page.cfg.ttsBaseUrl = text; page.save() }
                        }
                        Text { text: "Modelo"; color: Theme.textSecondary }
                        LcTextField {
                            Layout.fillWidth: true
                            text: page.cfg.ttsModel || ""
                            onEditingFinished: { page.cfg.ttsModel = text; page.save() }
                        }
                        Text { text: "Voz"; color: Theme.textSecondary }
                        LcTextField {
                            Layout.fillWidth: true
                            text: page.cfg.ttsVoice || ""
                            onEditingFinished: { page.cfg.ttsVoice = text; page.save() }
                        }
                        Text { text: "Formato"; color: Theme.textSecondary }
                        LcComboBox {
                            Layout.fillWidth: true
                            model: ["wav", "mp3", "pcm"]
                            currentIndex: Math.max(0, model.indexOf(page.cfg.ttsFormat || "wav"))
                            onActivated: { page.cfg.ttsFormat = model[currentIndex]; page.save() }
                        }
                        Text { text: "API key ref"; color: Theme.textSecondary; visible: page.cfg.ttsProvider === "cloud" }
                        LcTextField {
                            Layout.fillWidth: true
                            visible: page.cfg.ttsProvider === "cloud"
                            text: page.cfg.ttsKeyRef || ""
                            onEditingFinished: { page.cfg.ttsKeyRef = text; page.save() }
                        }
                    }

                    Rectangle {
                        Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.fillWidth: true
                        implicitHeight: latencyText.implicitHeight + 18
                        radius: 6; color: Theme.inputBg; border.color: Theme.borderColor
                        property var stats: App.voiceLatencyStats
                        Text {
                            id: latencyText; anchors.fill: parent; anchors.margins: 9
                            wrapMode: Text.WordWrap; color: Theme.textSecondary; font.pixelSize: 12
                            text: parent.stats.count > 0
                                  ? "Latencia real (" + parent.stats.count + " turnos): p50 "
                                    + parent.stats.p50Ms + " ms · p90 " + parent.stats.p90Ms
                                    + " ms · p95 " + parent.stats.p95Ms + " ms"
                                  : "Latencia real: todavía no hay turnos medidos."
                        }
                    }

                    Text { text: "Detección de habla (VAD)"; color: Theme.textPrimary; Layout.leftMargin: 24; font { pixelSize: 15; bold: true } }
                    GridLayout {
                        columns: 2; columnSpacing: 12; rowSpacing: 8
                        Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.fillWidth: true

                        Text { text: "Silencio fin de turno (ms)"; color: Theme.textSecondary }
                        LcTextField {
                            Layout.fillWidth: true
                            text: String(page.cfg.vadSilenceMs || 800)
                            onEditingFinished: { page.cfg.vadSilenceMs = parseInt(text) || 800; page.save() }
                        }
                        Text { text: "Segmento incremental (ms)"; color: Theme.textSecondary }
                        LcTextField {
                            Layout.fillWidth: true
                            text: String(page.cfg.vadSegmentMs || 350)
                            onEditingFinished: { page.cfg.vadSegmentMs = parseInt(text) || 350; page.save() }
                        }
                        Text { text: "Auto-escuchar"; color: Theme.textSecondary }
                        LcSwitch {
                            checked: page.cfg.autoListen !== false
                            onToggled: { page.cfg.autoListen = checked; page.save() }
                        }
                        Text { text: "Barge-in (interrumpir)"; color: Theme.textSecondary }
                        LcSwitch {
                            checked: page.cfg.bargeIn !== false
                            onToggled: { page.cfg.bargeIn = checked; page.save() }
                        }
                    }

                    Text { text: "Cursor por voz (accesibilidad)"; color: Theme.textPrimary; Layout.leftMargin: 24; font { pixelSize: 15; bold: true } }
                    ColumnLayout {
                        id: cursorOcrSection
                        Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.fillWidth: true
                        spacing: 8

                        // Estado del OCR de Windows. Se consulta al abrir la página y
                        // al togglear (no es binding: levantar el motor cuesta).
                        property var ocr: ({ available: false, detail: "" })
                        function refreshOcr() { ocr = App.ocrStatus() }
                        Component.onCompleted: refreshOcr()

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            Text { text: "Mover el cursor por voz"; color: Theme.textSecondary }
                            Item { Layout.fillWidth: true }
                            LcSwitch {
                                // Default OFF: activarlo hace que Charla capture la
                                // pantalla para leerla. Es decisión del usuario.
                                checked: page.cfg.cursorOcr === true
                                onToggled: {
                                    page.cfg.cursorOcr = checked
                                    page.save()
                                    // Re-chequear al prender: si falta el paquete de
                                    // idioma hay que decirlo ACÁ, no cuando el usuario
                                    // hable y no pase nada.
                                    if (checked) cursorOcrSection.refreshOcr()
                                }
                            }
                        }
                        // Sin OCR el toggle no hace nada: avisarlo con la instrucción
                        // para resolverlo. El mensaje se re-chequea solo (el motor
                        // reintenta), así que instalar el paquete y volver acá alcanza.
                        Text {
                            Layout.fillWidth: true
                            visible: page.cfg.cursorOcr === true
                            wrapMode: Text.WordWrap
                            font.pixelSize: 12
                            color: cursorOcrSection.ocr.available ? Theme.textSecondary : Theme.errorText
                            text: cursorOcrSection.ocr.detail || ""
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Theme.textSecondary
                            font.pixelSize: 12
                            text: "Decí \"clic en Guardar\", \"doble clic en Documentos\", \"clic derecho en " +
                                  "Escritorio\" o \"mové el cursor a Aceptar\" y el cursor va al texto que se ve " +
                                  "en pantalla. Sólo actúa si la frase EMPIEZA con la orden; el resto sigue " +
                                  "siendo conversación normal."
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Theme.textSecondary
                            font.pixelSize: 12
                            text: "Con esto activo, cada orden captura la pantalla y la lee con el OCR de " +
                                  "Windows. La captura se procesa en memoria y se descarta: no se guarda ni " +
                                  "se envía a ningún lado."
                        }
                    }

                    Item { height: 16; width: 1 }
                }
            }
        }
    }

    LcDialog {
        id: charlaTuneDialog
        property var recs: []
        title: "Mejoras para modo de voz"
        width: Math.min(620, page.width - 48)
        height: Math.min(190 + recs.length * 26 + 70, page.height - 48)

        contentItem: ColumnLayout {
            width: charlaTuneDialog.availableWidth
            spacing: 10
            Text {
                Layout.fillWidth: true
                text: "¿Desea aplicar las mejoras para un modo de voz? (no hacerlo puede generar mucho delay)\n\nSe relanza el perfil activo con estos ajustes SOLO para esta sesión (el perfil guardado no se modifica):"
                color: Theme.textPrimary; font.pixelSize: 14; wrapMode: Text.WordWrap
            }
            Repeater {
                model: charlaTuneDialog.recs
                delegate: Text {
                    Layout.fillWidth: true
                    text: "• " + modelData.flag + ": "
                          + (modelData.current.length ? modelData.current : "(sin fijar)")
                          + " → " + modelData.recommended + " — " + modelData.reason
                    color: Theme.textSecondary; font.pixelSize: 12; wrapMode: Text.WordWrap
                }
            }
            CheckBox {
                id: tuneAlwaysCb
                text: "Siempre que se entre a ingi-charla, recargar el perfil con las mejoras para charla"
                checked: false
                contentItem: Text {
                    text: tuneAlwaysCb.text; color: Theme.textPrimary; font.pixelSize: 12
                    wrapMode: Text.WordWrap; leftPadding: tuneAlwaysCb.indicator.width + 6
                    verticalAlignment: Text.AlignVCenter
                }
            }
            Item { Layout.fillHeight: true }
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
                    text: "Continuar sin cambios"
                    secondary: true
                    onClicked: { charlaTuneDialog.close(); App.startCharla() }
                }
                LcButton {
                    text: "Aplicar y relanzar"
                    onClicked: {
                        App.setCharlaAutoTune(tuneAlwaysCb.checked)
                        charlaTuneDialog.close()
                        App.applyCharlaTuneAndStartCharla()
                    }
                }
            }
        }
    }

    LcDialog {
        id: missingSttModelDialog
        property string engineId: ""
        title: "Modelo de voz requerido"
        width: Math.min(520, page.width - 48)
        height: 220

        contentItem: Text {
            width: missingSttModelDialog.availableWidth
            height: missingSttModelDialog.availableHeight
            text: "Faltan componentes locales para conversar por voz.\n\n¿Desea descargar y configurar automáticamente STT, whisper-server, Piper y una voz en español?"
            color: Theme.textPrimary
            font.pixelSize: 14
            wrapMode: Text.WordWrap
            verticalAlignment: Text.AlignVCenter
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
                    text: "Más tarde"
                    secondary: true
                    onClicked: missingSttModelDialog.close()
                }
                LcButton {
                    text: "Descargar"
                    onClicked: {
                        const engineId = missingSttModelDialog.engineId
                        missingSttModelDialog.close()
                        App.installVoicePrerequisites(engineId)
                    }
                }
            }
        }
    }
}
