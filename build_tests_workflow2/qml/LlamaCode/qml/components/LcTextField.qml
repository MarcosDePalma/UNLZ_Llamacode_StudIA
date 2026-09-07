import QtQuick
import QtQuick.Controls

TextField {
    id: root
    color: Theme.textPrimary
    placeholderTextColor: Theme.textMuted
    font.pixelSize: 13
    leftPadding: 10
    rightPadding: 10
    horizontalAlignment: TextInput.AlignHCenter
    verticalAlignment: TextInput.AlignVCenter
    implicitHeight: 34

    background: Rectangle {
        radius: 6
        color: Theme.inputBg
        border.color: root.activeFocus ? Theme.inputBorderFocus : Theme.inputBorderColor
        border.width: root.activeFocus ? 2 : 1
    }
}
