import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// LcColorRow — fila de ancla de color: label + swatch clickeable + hex.
//
// `value` es la fuente de verdad; el campo hex se sincroniza a mano en
// onValueChanged (un binding se rompería al tipear y loadDef() ya no lo
// actualizaría). El picker se inyecta desde afuera (property picker) para que
// la fila no dependa de ningún id del archivo que la usa.
RowLayout {
    id: root

    property string label: ""
    property string value: ""
    property string lastValid: "#000000"
    property var picker: null
    readonly property string normalized: Theme.normalizeHex(value)
    readonly property bool valid: normalized !== ""

    Layout.fillWidth: true
    spacing: 8

    onValueChanged: {
        if (hexInput.text !== value) hexInput.text = value
        var n = Theme.normalizeHex(value)
        if (n !== "") lastValid = n
    }

    // Abre el selector avanzado apuntándolo a esta fila. Sin picker inyectado
    // no hace nada (la fila sigue siendo editable por hex).
    function openPicker() {
        if (!picker) return false
        picker.target = root
        picker.openWith(safeHex())
        return true
    }

    // Normaliza lo tipeado ("f0a" → "#ff00aa"); si quedó basura, vuelve al
    // último válido en vez de persistirla.
    function commitTypedHex() { value = safeHex() }

    // Siempre un hex usable. Normaliza acá y no vía la property `valid`: dentro
    // del mismo tick el binding puede estar sin reevaluar y se guardaba "".
    function safeHex() {
        var n = Theme.normalizeHex(value)
        return n !== "" ? n : lastValid
    }

    Text {
        text: root.label
        color: Theme.textSecondary
        font.pixelSize: 12
        Layout.preferredWidth: 110
    }

    Rectangle {
        Layout.preferredWidth: 28
        Layout.preferredHeight: 28
        radius: 6
        color: root.valid ? root.normalized : "transparent"
        border.width: swatchMouse.containsMouse ? 2 : 1
        border.color: !root.valid ? Theme.errorBorder
                    : swatchMouse.containsMouse ? Theme.accent : Theme.divider

        MouseArea {
            id: swatchMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.openPicker()
        }
        ToolTip.visible: swatchMouse.containsMouse
        ToolTip.text: "Elegir color"
    }

    LcTextField {
        id: hexInput
        Layout.fillWidth: true
        placeholderText: "#RRGGBB"
        maximumLength: 7                      // "#rrggbb" — el hex tiene largo fijo
        validator: RegularExpressionValidator {
            regularExpression: /#?[0-9a-fA-F]{0,6}/
        }
        onTextEdited: root.value = text
        onEditingFinished: root.commitTypedHex()
    }
}
