import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0

Rectangle {
    id: root
    color: Theme.inputBg
    radius: 8
    border.color: Theme.borderColor
    clip: true

    property string commandLine: ""
    property var warnings: []
    property var errors: []
    property bool isValid: errors.length === 0

    ColumnLayout {
        anchors { fill: parent; margins: 12 }
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "$ "
                font { family: Theme.codeFont; pixelSize: 13 }
                color: Theme.successText
            }
            Text {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: {
                    const _lang = App.langV
                    return root.commandLine.length > 0 ? root.commandLine : App.l("cmd.noProfile")
                }
                font { family: Theme.codeFont; pixelSize: 13 }
                color: root.commandLine.length > 0 ? Theme.textPrimary : Theme.textMuted
                wrapMode: Text.WrapAnywhere
                maximumLineCount: 5
                elide: Text.ElideRight
            }
            LcButton {
                property bool copied: false
                visible: root.commandLine.length > 0
                text: copied ? (App.langV, App.l("cmd.copied")) : (App.langV, App.l("cmd.copy"))
                secondary: true
                onClicked: {
                    App.copyToClipboard(root.commandLine)
                    copied = true
                    copyTimer.start()
                }
                Timer {
                    id: copyTimer; interval: 1500
                    onTriggered: parent.copied = false
                }
            }
        }

        Repeater {
            model: root.warnings
            delegate: Row {
                spacing: 6
                Text { text: "⚠"; font.pixelSize: 12; color: Theme.warnText }
                Text { text: modelData; font.pixelSize: 12; color: Theme.warnText; wrapMode: Text.Wrap }
            }
        }

        Repeater {
            model: root.errors
            delegate: Row {
                spacing: 6
                Text { text: "✗"; font.pixelSize: 12; color: Theme.errorText }
                Text { text: modelData; font.pixelSize: 12; color: Theme.errorText; wrapMode: Text.Wrap }
            }
        }
    }
}
