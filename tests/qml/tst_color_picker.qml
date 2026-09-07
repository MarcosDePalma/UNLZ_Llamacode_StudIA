import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Test del editor de tema (parte QML): fila de color -> selector avanzado -> vuelta.
// Corre con el runtime `qml` en offscreen (ver add_test qml_color_picker en
// CMakeLists.txt); LcColorPicker/LcColorRow son los archivos REALES, copiados
// al lado de los stubs de Theme/App para que resuelvan sus nombres.
// Sale 0 si todo pasa, 1 si algo falla.
ApplicationWindow {
    id: win
    width: 500; height: 500
    visible: true

    property int fails: 0
    function check(cond, msg) {
        console.log((cond ? "  PASS " : "  FAIL ") + msg)
        if (!cond) win.fails++
    }

    LcColorPicker {
        id: picker
        property var target: null
        onPicked: function (hex) { if (target) target.value = hex }
    }

    ColumnLayout {
        anchors.fill: parent
        LcColorRow { id: row; label: "Acento"; picker: picker }
    }

    Component.onCompleted: {
        row.value = "#89b4fa"
        check(row.valid, "hex inicial valido")

        // Clic en el swatch.
        check(row.openPicker(), "el swatch abre el picker")
        check(picker.visible, "picker visible")
        check(picker.target === row, "picker apunta a la fila clickeada")
        check(picker.currentHex() === "#89b4fa", "picker arranca en el color de la fila")

        // Elegir otro color y aceptar -> la fila se actualiza.
        picker.setFromHex("#ff00aa")
        picker.picked(picker.currentHex())
        check(row.value === "#ff00aa", "aceptar actualiza la fila: " + row.value)

        // Plano SV / tono: mover el estado HSV cambia el hex.
        picker.setFromHex("#89b4fa")
        picker.sat = 0.5; picker.val = 0.5
        check(picker.currentHex() !== "#89b4fa", "mover el plano SV cambia el hex: " + picker.currentHex())
        check(picker.currentColor.hsvValue < 0.51 && picker.currentColor.hsvSaturation > 0.49,
              "el hex refleja el HSV del plano")

        // Gris: hsvHue vale -1 y no debe pisar el tono elegido.
        var hueBefore = picker.hue
        picker.setFromHex("#808080")
        check(Math.abs(picker.hue - hueBefore) < 0.001, "un gris conserva el tono")
        check(picker.currentHex() === "#808080", "gris roundtrip: " + picker.currentHex())

        // Hex tipeado a mano en la fila.
        row.value = "f0a"; row.commitTypedHex()
        check(row.value === "#ff00aa", "shorthand tipeado se normaliza: " + row.value)
        row.value = "#zz"; row.commitTypedHex()
        check(row.value === "#ff00aa", "basura vuelve al ultimo valido: " + row.value)
        check(!row.valid || row.normalized !== "", "el swatch nunca recibe un color vacio")

        // Fila sin picker inyectado: no explota y sigue editable por hex.
        row.picker = null
        check(!row.openPicker(), "sin picker inyectado el clic no hace nada")

        console.log(win.fails === 0 ? "TODO OK" : (win.fails + " FALLAS"))
        Qt.exit(win.fails === 0 ? 0 : 1)
    }
}
