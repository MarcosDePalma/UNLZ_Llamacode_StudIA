import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0

// Sección Tuner: mezcla de auto-tuning y benchmarking sobre un perfil.
// Mide el perfil tal cual está (baseline), busca una mejor config de
// -ngl/-b/-ub/flash-attn/cache-type con el gate de calidad, y deja el resultado
// como un perfil NUEVO "Opti - <perfil>" sin tocar el original.
//
// El objetivo es ajustable entre prefill (PP) y generación (TG): -b/-ub afectan
// sobre todo al prefill, así que tunearlos mirando sólo TG mide el efecto
// secundario. Un agente con system prompt largo quiere PP; un chat corto, TG.
Item {
    id: root

    property string selectedLaunchId: ""
    property string tuneMode: "auto"          // auto | cpu
    // Peso del prefill en el objetivo [0,1].
    property real ppWeight: 0.5
    property bool measureBaseline: true

    readonly property var result: App.autoTuneResult
    readonly property bool hasResult: Object.keys(result || {}).length > 0
    readonly property bool canTune:
        selectedLaunchId.length > 0 && !App.serverRunning && !App.autoTuneRunning

    function fmt(v, digits) {
        const n = Number(v)
        if (!isFinite(n) || n <= 0) return "n/d"
        return n.toFixed(digits === undefined ? 1 : digits)
    }
    function fmtAcceptance(v) {
        const n = Number(v)
        return isFinite(n) && n >= 0 ? n.toFixed(1) + "%" : "n/d"
    }
    function acceptanceDelta(after, before) {
        const a = Number(after), b = Number(before)
        if (!isFinite(a) || !isFinite(b) || a < 0 || b < 0) return "—"
        const d = a - b
        return (d > 0 ? "+" : "") + d.toFixed(1) + " pp"
    }
    function gainLabel(pct) {
        const n = Number(pct)
        if (!isFinite(n) || Math.abs(n) < 0.05) return "="
        return (n > 0 ? "+" : "") + n.toFixed(1) + "%"
    }
    function gainColor(pct) {
        const n = Number(pct)
        if (!isFinite(n) || Math.abs(n) < 0.05) return Theme.textMuted
        return n > 0 ? Theme.successText : Theme.errorText
    }
    function objectiveLabel() {
        if (ppWeight >= 0.95) return "Sólo prefill (PP)"
        if (ppWeight <= 0.05) return "Sólo generación (TG)"
        return Math.round(ppWeight * 100) + "% prefill / "
               + Math.round((1 - ppWeight) * 100) + "% generación"
    }

    function start() {
        if (!canTune) return
        App.startAutoTune(selectedLaunchId,
                          trialsSpin.value,
                          qualitySpin.value / 100.0,
                          predictSpin.value,
                          tuneMode,
                          ppWeight,
                          prefillSpin.value,
                          measureBaseline)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            Layout.fillWidth: true
            title: "Tuner"
            subtitle: App.autoTuneRunning
                ? App.autoTuneStatus
                : "Mide el perfil actual, busca una config mejor y la guarda como perfil «Opti - …»"
            actionLabel: App.autoTuneRunning ? "Cancelar" : ""
            onActionClicked: App.cancelAutoTune()
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ── Panel de configuración ────────────────────────────────────────
            Rectangle {
                Layout.preferredWidth: 360
                Layout.fillHeight: true
                color: Theme.surfaceBg

                ScrollView {
                    anchors.fill: parent
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    contentWidth: availableWidth

                    ColumnLayout {
                        width: parent.width
                        spacing: 14

                        Item { Layout.preferredHeight: 2 }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.leftMargin: 12
                            Layout.rightMargin: 12
                            radius: 8
                            color: Theme.inputBg
                            border.color: Theme.borderColor
                            implicitHeight: hardwareDiagCol.implicitHeight + 20

                            ColumnLayout {
                                id: hardwareDiagCol
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                                spacing: 5

                                Text {
                                    text: "Diagnóstico multi-GPU"
                                    color: Theme.textPrimary
                                    font { pixelSize: 13; bold: true }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: App.hardwareSummary.summary || "Hardware no analizado"
                                    color: Theme.textSecondary
                                    font.pixelSize: 11
                                    wrapMode: Text.WordWrap
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: {
                                        const mode = App.hardwareSummary.recommendedSplitMode || "layer"
                                        const rec = App.hardwareSummary.performanceRecommendation || ({})
                                        return "Sugerencia: split-mode " + mode + " · KV "
                                               + (rec.kvCache || "q8_0")
                                    }
                                    color: Theme.accent
                                    font.pixelSize: 11
                                    wrapMode: Text.WordWrap
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: (App.hardwareSummary.performanceRecommendation || ({})).reason ||
                                          "La recomendación se ajustará cuando termine el probe de hardware."
                                    color: Theme.textMuted
                                    font.pixelSize: 10
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }

                        Text {
                            Layout.leftMargin: 16
                            text: "PERFIL A OPTIMIZAR"
                            color: Theme.textSecondary
                            font.pixelSize: 10
                            font.bold: true
                        }

                        LcComboBox {
                            id: profileCombo
                            Layout.fillWidth: true
                            Layout.leftMargin: 16
                            Layout.rightMargin: 16
                            enabled: !App.autoTuneRunning
                            property var launchMenu: App.launchMenu()
                            function refreshMenu() {
                                const sel = profileCombo.currentValue
                                profileCombo.launchMenu = App.launchMenu()
                                const i = profileCombo.indexOfValue(sel)
                                if (i >= 0) profileCombo.currentIndex = i
                            }
                            Connections {
                                target: App.profileManager
                                function onLaunchesChanged() { profileCombo.refreshMenu() }
                            }
                            model: launchMenu
                            textRole: "displayName"
                            valueRole: "id"
                            onCurrentValueChanged: root.selectedLaunchId = currentValue || ""
                            Component.onCompleted: root.selectedLaunchId = currentValue || ""
                            background: Rectangle { color: Theme.inputBg; radius: 6; border.color: Theme.borderColor }
                            contentItem: Text {
                                text: profileCombo.displayText
                                color: Theme.textPrimary
                                font.pixelSize: 13
                                leftPadding: 10
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }
                            delegate: ItemDelegate {
                                width: profileCombo.width
                                highlighted: profileCombo.highlightedIndex === index
                                contentItem: Text {
                                    text: modelData.displayName || ""
                                    color: Theme.theme === "oled" ? "white" : Theme.textPrimary
                                    font.pixelSize: 13; leftPadding: 6; elide: Text.ElideRight
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle { color: highlighted ? Theme.borderColor : Theme.inputBg }
                            }
                        }

                        Text {
                            Layout.leftMargin: 16
                            text: "MODO"
                            color: Theme.textSecondary
                            font.pixelSize: 10
                            font.bold: true
                        }

                        Flow {
                            Layout.fillWidth: true
                            Layout.leftMargin: 16
                            Layout.rightMargin: 16
                            spacing: 6

                            Repeater {
                                model: [
                                    { key: "auto", label: "GPU / auto" },
                                    { key: "cpu", label: "CPU-only" }
                                ]
                                delegate: Rectangle {
                                    height: 30
                                    radius: 7
                                    color: root.tuneMode === modelData.key ? Theme.highlight : "transparent"
                                    border.color: root.tuneMode === modelData.key ? Theme.accent : Theme.borderColor
                                    implicitWidth: modeChip.implicitWidth + 18
                                    opacity: App.autoTuneRunning ? 0.5 : 1.0
                                    Text {
                                        id: modeChip
                                        anchors.centerIn: parent
                                        text: modelData.label
                                        color: root.tuneMode === modelData.key ? Theme.accent : Theme.textSecondary
                                        font.pixelSize: 12
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        enabled: !App.autoTuneRunning
                                        onClicked: root.tuneMode = modelData.key
                                    }
                                }
                            }
                        }

                        Text {
                            Layout.leftMargin: 16
                            text: "OBJETIVO"
                            color: Theme.textSecondary
                            font.pixelSize: 10
                            font.bold: true
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 16
                            Layout.rightMargin: 16
                            spacing: 4

                            Text {
                                text: root.objectiveLabel()
                                color: Theme.textPrimary
                                font.pixelSize: 12
                            }
                            Slider {
                                id: ppSlider
                                Layout.fillWidth: true
                                from: 0; to: 1; stepSize: 0.05
                                value: root.ppWeight
                                enabled: !App.autoTuneRunning
                                onValueChanged: root.ppWeight = value
                            }
                            Text {
                                Layout.fillWidth: true
                                text: "-b/-ub mueven sobre todo el prefill. Con el objetivo en TG puro "
                                      + "el tuner los elige por su efecto secundario."
                                color: Theme.textMuted
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                            }
                        }

                        Text {
                            Layout.leftMargin: 16
                            text: "MEDICIÓN"
                            color: Theme.textSecondary
                            font.pixelSize: 10
                            font.bold: true
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 16
                            Layout.rightMargin: 16
                            columns: 2
                            columnSpacing: 10
                            rowSpacing: 8

                            Text {
                                text: "Prompt de prefill (tokens)"
                                color: Theme.textSecondary; font.pixelSize: 12
                                Layout.fillWidth: true
                            }
                            SpinBox {
                                id: prefillSpin
                                from: 0; to: 32768; stepSize: 512
                                value: 4096
                                editable: true
                                enabled: !App.autoTuneRunning
                                Layout.preferredWidth: 120
                            }

                            Text {
                                text: "Tokens a generar"
                                color: Theme.textSecondary; font.pixelSize: 12
                                Layout.fillWidth: true
                            }
                            SpinBox {
                                id: predictSpin
                                from: 16; to: 2048; stepSize: 32
                                value: 256
                                editable: true
                                enabled: !App.autoTuneRunning
                                Layout.preferredWidth: 120
                            }

                            Text {
                                text: "Trials"
                                color: Theme.textSecondary; font.pixelSize: 12
                                Layout.fillWidth: true
                            }
                            SpinBox {
                                id: trialsSpin
                                from: 2; to: 100; stepSize: 2
                                value: 24
                                editable: true
                                enabled: !App.autoTuneRunning
                                Layout.preferredWidth: 120
                            }

                            Text {
                                text: "Calidad mínima (%)"
                                color: Theme.textSecondary; font.pixelSize: 12
                                Layout.fillWidth: true
                            }
                            SpinBox {
                                id: qualitySpin
                                from: 0; to: 100; stepSize: 5
                                value: 60
                                editable: true
                                enabled: !App.autoTuneRunning
                                Layout.preferredWidth: 120
                            }
                        }

                        LcCheckBox {
                            Layout.leftMargin: 16
                            Layout.rightMargin: 16
                            text: "Medir el perfil actual como baseline"
                            checked: root.measureBaseline
                            enabled: !App.autoTuneRunning
                            onToggled: root.measureBaseline = checked
                        }

                        Text {
                            Layout.fillWidth: true
                            Layout.leftMargin: 16
                            Layout.rightMargin: 16
                            text: "Sin baseline no hay con qué comparar la mejora: cuesta un arranque "
                                  + "de servidor más."
                            color: Theme.textMuted
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 16
                            Layout.rightMargin: 16
                            spacing: 8

                            LcButton {
                                text: App.autoTuneRunning ? "Optimizando…" : "Optimizar"
                                enabled: root.canTune
                                onClicked: root.start()
                            }
                            LcButton {
                                text: "Cancelar"
                                secondary: true
                                visible: App.autoTuneRunning
                                onClicked: App.cancelAutoTune()
                            }
                            LcButton {
                                text: "Limpiar"
                                secondary: true
                                visible: !App.autoTuneRunning && App.autoTuneTrials.length > 0
                                onClicked: App.clearAutoTuneResults()
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            Layout.leftMargin: 16
                            Layout.rightMargin: 16
                            visible: App.serverRunning
                            text: "Detené el servidor: el tuner necesita levantar el suyo por cada configuración."
                            color: Theme.warnText
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }

                        Item { Layout.preferredHeight: 8 }
                    }
                }
            }

            Rectangle { width: 1; Layout.fillHeight: true; color: Theme.divider }

            // ── Resultados ────────────────────────────────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                // Estado / progreso
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: statusCol.implicitHeight + 20
                    visible: App.autoTuneRunning || App.autoTuneStatus.length > 0
                    color: Theme.surfaceBg

                    ColumnLayout {
                        id: statusCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                        spacing: 6

                        Text {
                            Layout.fillWidth: true
                            text: (App.autoTuneRunning ? "⏳ " : "") + App.autoTuneStatus
                            color: Theme.textPrimary
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }
                        ProgressBar {
                            Layout.fillWidth: true
                            visible: App.autoTuneRunning
                            from: 0; to: 100
                            value: App.autoTuneProgress
                        }
                    }
                }

                // Resumen A/B
                Rectangle {
                    Layout.fillWidth: true
                    Layout.margins: 12
                    Layout.preferredHeight: resultCol.implicitHeight + 24
                    visible: root.hasResult
                    radius: 8
                    color: Theme.inputBg
                    border.color: Theme.borderColor

                    ColumnLayout {
                        id: resultCol
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
                        spacing: 8

                        Text {
                            text: "Resultado"
                            color: Theme.textPrimary
                            font { pixelSize: 13; bold: true }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 4
                            columnSpacing: 18
                            rowSpacing: 6

                            Text { text: ""; color: Theme.textMuted; font.pixelSize: 11 }
                            Text { text: "Antes"; color: Theme.textSecondary; font { pixelSize: 11; bold: true } }
                            Text { text: "Después"; color: Theme.textSecondary; font { pixelSize: 11; bold: true } }
                            Text { text: "Mejora"; color: Theme.textSecondary; font { pixelSize: 11; bold: true } }

                            Text { text: "Prefill (PP tok/s)"; color: Theme.textSecondary; font.pixelSize: 12 }
                            Text {
                                text: root.fmt(root.result.basePromptTps)
                                color: Theme.textPrimary; font.pixelSize: 12
                            }
                            Text {
                                text: root.fmt(root.result.promptTps)
                                color: Theme.textPrimary; font.pixelSize: 12
                            }
                            Text {
                                text: (root.result.hasBaseline ?? false)
                                      ? root.gainLabel(root.result.promptGainPct) : "—"
                                color: (root.result.hasBaseline ?? false)
                                       ? root.gainColor(root.result.promptGainPct) : Theme.textMuted
                                font { pixelSize: 12; bold: true }
                            }

                            Text { text: "Generación (TG tok/s)"; color: Theme.textSecondary; font.pixelSize: 12 }
                            Text {
                                text: root.fmt(root.result.baseGenTps)
                                color: Theme.textPrimary; font.pixelSize: 12
                            }
                            Text {
                                text: root.fmt(root.result.genTps)
                                color: Theme.textPrimary; font.pixelSize: 12
                            }
                            Text {
                                text: (root.result.hasBaseline ?? false)
                                      ? root.gainLabel(root.result.genGainPct) : "—"
                                color: (root.result.hasBaseline ?? false)
                                       ? root.gainColor(root.result.genGainPct) : Theme.textMuted
                                font { pixelSize: 12; bold: true }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: !(root.result.hasBaseline ?? false)
                            text: "Sin baseline medido: no hay comparación contra el perfil original."
                            color: Theme.textMuted
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: (root.result.ok ?? false) !== true
                            text: "No se creó un perfil nuevo: el resultado no superó el gate de promoción."
                            color: Theme.warnText
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 4
                            columnSpacing: 18
                            rowSpacing: 6

                            Text { text: "Aceptación draft (%)"; color: Theme.textSecondary; font.pixelSize: 12 }
                            Text { text: root.fmtAcceptance(root.result.baseDraftAcceptancePct); color: Theme.textPrimary; font.pixelSize: 12 }
                            Text { text: root.fmtAcceptance(root.result.draftAcceptancePct); color: Theme.textPrimary; font.pixelSize: 12 }
                            Text { text: root.acceptanceDelta(root.result.draftAcceptancePct, root.result.baseDraftAcceptancePct); color: Theme.textMuted; font.pixelSize: 12 }
                        }

                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

                        Text {
                            Layout.fillWidth: true
                            text: "Flags: " + (root.result.bestArgs || "—")
                            color: Theme.textSecondary
                            font { pixelSize: 12; family: "Consolas" }
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            visible: (root.result.newProfileName || "").length > 0

                            Text {
                                Layout.fillWidth: true
                                text: "Perfil creado: " + (root.result.newProfileName || "")
                                color: Theme.successText
                                font { pixelSize: 12; bold: true }
                                elide: Text.ElideRight
                            }
                            LcButton {
                                text: "Usar este perfil"
                                secondary: true
                                enabled: !App.serverRunning && !App.autoTuneRunning
                                onClicked: App.startServer(root.result.newProfileId || "")
                            }
                        }
                    }
                }

                // Tabla de trials
                Text {
                    Layout.leftMargin: 16
                    Layout.topMargin: 4
                    text: "TRIALS"
                    color: Theme.textSecondary
                    font { pixelSize: 10; bold: true }
                    visible: App.autoTuneTrials.length > 0
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: 12
                    radius: 8
                    color: Theme.inputBg
                    border.color: Theme.borderColor
                    clip: true

                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 48
                        visible: App.autoTuneTrials.length === 0
                        horizontalAlignment: Text.AlignHCenter
                        text: "Elegí un perfil y tocá Optimizar.\n\n"
                              + "Cada trial levanta el servidor con una configuración distinta, mide "
                              + "prefill y generación, y descarta las que degradan la calidad. "
                              + "Al terminar se crea un perfil «Opti - …» con la mejor config; "
                              + "el original queda intacto."
                        color: Theme.textMuted
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 1
                        spacing: 0
                        visible: App.autoTuneTrials.length > 0

                        // Encabezado
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 28
                            color: Theme.surfaceBg

                            RowLayout {
                                anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                                spacing: 8
                                Text { text: "#"; Layout.preferredWidth: 34; color: Theme.textSecondary; font { pixelSize: 11; bold: true } }
                                Text { text: "PP"; Layout.preferredWidth: 62; color: Theme.textSecondary; font { pixelSize: 11; bold: true } }
                                Text { text: "TG"; Layout.preferredWidth: 62; color: Theme.textSecondary; font { pixelSize: 11; bold: true } }
                                Text { text: "Spec"; Layout.preferredWidth: 58; color: Theme.textSecondary; font { pixelSize: 11; bold: true } }
                                Text { text: "Score"; Layout.preferredWidth: 62; color: Theme.textSecondary; font { pixelSize: 11; bold: true } }
                                Text { text: "Cal."; Layout.preferredWidth: 42; color: Theme.textSecondary; font { pixelSize: 11; bold: true } }
                                Text { text: "Config"; Layout.fillWidth: true; color: Theme.textSecondary; font { pixelSize: 11; bold: true } }
                            }
                        }

                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: App.autoTuneTrials
                            ScrollBar.vertical: LcScrollBar {}

                            delegate: Rectangle {
                                width: ListView.view.width
                                height: 30
                                color: (modelData.baseline ?? false)
                                       ? Theme.highlight
                                       : (index % 2 === 0 ? "transparent" : Theme.surfaceBg)

                                RowLayout {
                                    anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                                    spacing: 8

                                    Text {
                                        Layout.preferredWidth: 34
                                        text: (modelData.baseline ?? false) ? "base" : (modelData.index ?? 0)
                                        color: (modelData.baseline ?? false) ? Theme.accent : Theme.textMuted
                                        font.pixelSize: 11
                                    }
                                    Text {
                                        Layout.preferredWidth: 62
                                        text: root.fmt(modelData.promptTps)
                                        color: Theme.textPrimary; font.pixelSize: 11
                                    }
                                    Text {
                                        Layout.preferredWidth: 62
                                        text: root.fmt(modelData.genTps)
                                        color: Theme.textPrimary; font.pixelSize: 11
                                    }
                                    Text {
                                        Layout.preferredWidth: 58
                                        text: root.fmtAcceptance(modelData.draftAcceptancePct)
                                        color: (modelData.draftAcceptancePct ?? -1) >= 80 ? Theme.successText
                                             : (modelData.draftAcceptancePct ?? -1) >= 0 ? Theme.warnText
                                             : Theme.textMuted
                                        font.pixelSize: 11
                                    }
                                    Text {
                                        Layout.preferredWidth: 62
                                        text: root.fmt(modelData.score)
                                        color: Theme.textSecondary; font.pixelSize: 11
                                    }
                                    Text {
                                        Layout.preferredWidth: 42
                                        text: root.fmt(modelData.quality, 2)
                                        color: (modelData.quality ?? 0) >= 0.8 ? Theme.successText
                                             : (modelData.quality ?? 0) >= 0.5 ? Theme.warnText
                                             : Theme.errorText
                                        font.pixelSize: 11
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.summary || ""
                                        color: Theme.textMuted
                                        font { pixelSize: 11; family: "Consolas" }
                                        elide: Text.ElideRight
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
