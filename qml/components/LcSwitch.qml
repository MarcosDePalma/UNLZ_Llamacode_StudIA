import QtQuick
import QtQuick.Controls
import LlamaCode 1.0

// Switch con estilo del tema. Hereda Switch para conservar checked, onToggled
// y el resto de la API del control nativo.
Switch {
    id: control

    indicator: Rectangle {
        implicitWidth: 48
        implicitHeight: 26
        x: control.leftPadding
        y: control.height / 2 - height / 2
        radius: height / 2
        color: control.checked ? Theme.accent : Theme.inputBg
        border.color: control.checked ? Theme.accent : Theme.borderColor
        border.width: 1

        Rectangle {
            width: 22
            height: 22
            x: control.checked ? parent.width - width - 2 : 2
            anchors.verticalCenter: parent.verticalCenter
            radius: height / 2
            color: control.checked ? Theme.btnPrimaryText : Theme.textPrimary

            Behavior on x {
                NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
            }
        }
    }
}
