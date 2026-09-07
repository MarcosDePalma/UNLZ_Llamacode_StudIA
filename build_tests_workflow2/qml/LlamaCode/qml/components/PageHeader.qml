import QtQuick
import QtQuick.Layouts
import LlamaCode 1.0

Rectangle {
    id: root
    height: 56
    color: Theme.navBg

    property string title: ""
    property string subtitle: ""
    property alias actionLabel: actionBtn.text
    property string action2Label: ""
    signal actionClicked()
    signal action2Clicked()

    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.divider }

    RowLayout {
        anchors { fill: parent; leftMargin: 24; rightMargin: 16 }

        Column {
            Layout.fillWidth: true
            spacing: 2
            Text {
                text: root.title
                font { pixelSize: 18; bold: true }
                color: Theme.textPrimary
            }
            Text {
                visible: root.subtitle.length > 0
                text: root.subtitle
                font.pixelSize: 12
                color: Theme.textMuted
            }
        }

        LcButton {
            id: action2Btn
            visible: root.action2Label.length > 0
            text: root.action2Label
            secondary: true
            onClicked: root.action2Clicked()
        }

        LcButton {
            id: actionBtn
            visible: text.length > 0
            onClicked: root.actionClicked()
        }
    }
}
