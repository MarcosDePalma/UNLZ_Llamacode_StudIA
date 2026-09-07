import QtQuick
import QtQuick.Controls
import LlamaCode 1.0

// CheckBox con estilo del tema (oscuro/oled). Hereda CheckBox: cualquier
// propiedad/handler/id existente sigue funcionando (text, checked, onToggled),
// y un override per-uso (indicator/contentItem) gana sobre estos defaults.
CheckBox {
    id: control

    spacing: 8

    indicator: Rectangle {
        implicitWidth: 18
        implicitHeight: 18
        x: control.leftPadding
        y: control.height / 2 - height / 2
        radius: 4
        color: control.checked ? Theme.accent : Theme.inputBg
        border.color: control.checked ? Theme.accent : Theme.borderColor

        Text {
            anchors.centerIn: parent
            visible: control.checked
            text: "✓"
            color: "white"
            font.pixelSize: 13
            font.bold: true
        }
    }

    contentItem: Text {
        text: control.text
        color: Theme.theme === "oled" ? "white" : Theme.textPrimary
        font.pixelSize: 13
        verticalAlignment: Text.AlignVCenter
        leftPadding: control.indicator.width + control.spacing
        elide: Text.ElideRight
    }
}
