import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0

// Sección "Descargas": progreso en vivo de modelos/binarios, cola con
// pausar/reanudar/cancelar/reordenar, e historial persistente de descargas
// finalizadas. Se abre automáticamente al "Instalar dependencias"
// (AppController::navigateToDownloads).
Item {
    id: root

    function stateLabel(s) {
        return s === "paused" ? "Pausada"
             : s === "queued" ? "En cola"
             : s === "done" ? "Lista"
             : s === "error" ? "Error"
             : s === "resolving" ? "Resolviendo"
             : s === "verifying" ? "Verificando"
             : "Descargando"
    }
    function stateColor(s, active) {
        return s === "error" ? Theme.errorText
             : s === "done" ? Theme.successText
             : active ? Theme.accent : Theme.textMuted
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            Layout.fillWidth: true
            title: (App.langV, App.l("nav.downloads"))
            subtitle: "Progreso de modelos y binarios, cola e historial"
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            leftPadding: 16
            rightPadding: 16
            topPadding: 16
            bottomPadding: 16
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                width: root.width - 32
                spacing: 16

                // ── Instalación de binario (en curso) ───────────────────
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: binCol.implicitHeight + 24
                    visible: App.installingOfficialBinary
                             || App.officialBinaryInstallStatus.length > 0
                    color: Theme.surfaceBg
                    radius: 8
                    border.color: App.installingOfficialBinary ? Theme.accent : Theme.borderColor

                    ColumnLayout {
                        id: binCol
                        anchors { fill: parent; margins: 12 }
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: "Instalación de binario (llama-server)"
                                color: Theme.textPrimary
                                font { pixelSize: 13; bold: true }
                            }
                            BusyIndicator {
                                running: App.installingOfficialBinary
                                visible: App.installingOfficialBinary
                                implicitWidth: 20; implicitHeight: 20
                            }
                            LcButton {
                                text: "Cancelar"
                                secondary: true
                                visible: App.installingOfficialBinary
                                onClicked: App.cancelOfficialBinaryInstall()
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: App.officialBinaryInstallStatus
                            color: App.installingOfficialBinary ? Theme.accent : Theme.textMuted
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: log.length > 0 ? 120 : 0
                            visible: log.length > 0
                            readonly property string log: App.officialBinaryInstallLog
                            color: Theme.baseBg
                            radius: 6
                            border.color: Theme.borderColor
                            Flickable {
                                anchors { fill: parent; margins: 8 }
                                clip: true
                                contentHeight: logText.implicitHeight
                                Text {
                                    id: logText
                                    width: parent.width
                                    text: App.officialBinaryInstallLog
                                    color: Theme.textMuted
                                    font { pixelSize: 10; family: "Consolas, monospace" }
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }
                }

                // ── Descargas activas / en cola ─────────────────────────
                Text {
                    text: "Activas y en cola"
                    color: Theme.textSecondary
                    font { pixelSize: 12; bold: true }
                }
                Text {
                    Layout.fillWidth: true
                    visible: App.modelDownloadQueue.length === 0
                    text: App.modelDownloadRunning
                          ? "Preparando descarga…"
                          : "No hay descargas en curso."
                    color: Theme.textDim
                    font.pixelSize: 11
                }

                Repeater {
                    model: App.modelDownloadQueue
                    delegate: Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 64
                        radius: 8
                        color: (modelData.active ?? false) ? Theme.inputBg : Theme.surfaceBg
                        border.color: (modelData.active ?? false) ? Theme.accent : Theme.borderColor

                        readonly property string st: modelData.state ?? ""
                        readonly property int progress: modelData.progress ?? 0
                        readonly property bool active: modelData.active ?? false
                        readonly property bool paused: st === "paused"
                        readonly property bool movable: !active && st !== "done"

                        RowLayout {
                            anchors { fill: parent; margins: 10 }
                            spacing: 10

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 5
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.fileName ?? ""
                                        color: Theme.textPrimary
                                        font { pixelSize: 12; bold: true }
                                        elide: Text.ElideMiddle
                                    }
                                    Text {
                                        text: (modelData.totalMb ?? 0) > 0
                                              ? (modelData.receivedMb ?? 0) + " / " + modelData.totalMb + " MB"
                                              : ((modelData.receivedMb ?? 0) > 0 ? modelData.receivedMb + " MB" : "")
                                        color: Theme.textDim
                                        font.pixelSize: 10
                                    }
                                    Text {
                                        text: root.stateLabel(st)
                                        color: root.stateColor(st, active)
                                        font.pixelSize: 10
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 6
                                        radius: 3
                                        color: Theme.baseBg
                                        Rectangle {
                                            width: parent.width * (progress / 100)
                                            height: parent.height
                                            radius: 3
                                            color: st === "error" ? Theme.errorText
                                                 : st === "done" ? Theme.successText : Theme.accent
                                        }
                                    }
                                    Text {
                                        text: progress + "%"
                                        color: Theme.textMuted
                                        font.pixelSize: 10
                                        Layout.preferredWidth: 32
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }
                            }

                            LcButton {
                                text: paused || st === "error" ? "▶" : "Ⅱ"
                                secondary: true
                                Layout.preferredWidth: 30; Layout.preferredHeight: 26
                                visible: st !== "done"
                                onClicked: paused || st === "error"
                                           ? App.resumeModelDownload(modelData.id ?? "")
                                           : App.pauseModelDownload(modelData.id ?? "")
                            }
                            LcButton {
                                text: "↑"; secondary: true
                                Layout.preferredWidth: 28; Layout.preferredHeight: 26
                                enabled: movable && index > 0
                                visible: st !== "done"
                                onClicked: App.moveModelDownload(modelData.id ?? "", -1)
                            }
                            LcButton {
                                text: "↓"; secondary: true
                                Layout.preferredWidth: 28; Layout.preferredHeight: 26
                                enabled: movable && index < App.modelDownloadQueue.length - 1
                                visible: st !== "done"
                                onClicked: App.moveModelDownload(modelData.id ?? "", 1)
                            }
                            LcButton {
                                text: "✕"; secondary: true
                                Layout.preferredWidth: 30; Layout.preferredHeight: 26
                                onClicked: App.cancelModelDownload(modelData.id ?? "")
                            }
                        }
                    }
                }

                // ── Historial ───────────────────────────────────────────
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    Text {
                        Layout.fillWidth: true
                        text: "Historial"
                        color: Theme.textSecondary
                        font { pixelSize: 12; bold: true }
                    }
                    LcButton {
                        text: "Limpiar historial"
                        secondary: true
                        visible: App.downloadHistory.length > 0
                        onClicked: App.clearDownloadHistory()
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: App.downloadHistory.length === 0
                    text: "Todavía no hay descargas finalizadas."
                    color: Theme.textDim
                    font.pixelSize: 11
                }

                Repeater {
                    model: App.downloadHistory
                    delegate: Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 48
                        radius: 7
                        color: Theme.surfaceBg
                        border.color: Theme.borderColor

                        readonly property bool ok: (modelData.state ?? "") === "done"

                        RowLayout {
                            anchors { fill: parent; margins: 10 }
                            spacing: 10
                            Rectangle {
                                Layout.preferredWidth: 8; Layout.preferredHeight: 8
                                radius: 4
                                color: ok ? Theme.successText : Theme.errorText
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                Text {
                                    Layout.fillWidth: true
                                    text: (modelData.name ?? "")
                                          + ((modelData.kind ?? "") === "binary" ? "  ·  binario" : "")
                                    color: Theme.textPrimary
                                    font.pixelSize: 12
                                    elide: Text.ElideMiddle
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: (modelData.repo ?? "")
                                          + ((modelData.sizeMb ?? 0) > 0
                                             ? "  ·  " + Math.round(modelData.sizeMb) + " MB" : "")
                                    color: Theme.textDim
                                    font.pixelSize: 10
                                    elide: Text.ElideMiddle
                                }
                            }
                            Text {
                                text: ok ? "Lista" : "Error"
                                color: ok ? Theme.successText : Theme.errorText
                                font.pixelSize: 10
                            }
                            Text {
                                text: (modelData.finishedAt ?? "").slice(0, 16).replace("T", " ")
                                color: Theme.textMuted
                                font.pixelSize: 10
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true; Layout.preferredHeight: 8 }
            }
        }
    }
}
