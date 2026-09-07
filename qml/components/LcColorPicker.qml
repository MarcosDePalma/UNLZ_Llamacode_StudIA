import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// LcColorPicker — selector de color avanzado (plano SV + hue + alpha + hex/RGB).
//
// El estado vive en HSV (hue/sat/val/alpha): es lo que mueven el plano y los
// sliders. El hex y los campos RGB son vistas de ese estado; al editarlos se
// vuelve a HSV vía setFromHex(). Todo hex que entra pasa por Theme.normalizeHex
// (mismo saneo que usa ThemeProvider al guardar el tema).
LcDialog {
    id: root
    title: "Selector de color"
    standardButtons: Dialog.NoButton
    width: 380

    // Color con el que se abre; picked() devuelve el elegido al aceptar.
    property string initialHex: "#000000"
    property bool alphaEnabled: false
    signal picked(string hex)

    property real hue: 0
    property real sat: 1
    property real val: 1
    property real alpha: 1

    readonly property color currentColor: Qt.hsva(hue, sat, val, alpha)

    function _h2(v) {
        var s = Math.round(v * 255).toString(16)
        return s.length < 2 ? "0" + s : s
    }
    function currentHex() {
        var c = Qt.hsva(hue, sat, val, 1)
        var rgb = _h2(c.r) + _h2(c.g) + _h2(c.b)
        return (alphaEnabled && alpha < 1) ? "#" + _h2(alpha) + rgb : "#" + rgb
    }
    // Scratch para parsear un hex a color (asignar un string a una property color
    // hace la conversión; evita depender de helpers del objeto Qt).
    property color parseScratch: "#000000"

    function setFromHex(h) {
        var n = Theme.normalizeHex(h)
        if (n === "") return false
        parseScratch = n
        var c = parseScratch
        // hsvHue es -1 en grises: conservar el hue actual para no perderlo.
        if (c.hsvHue >= 0) hue = c.hsvHue
        sat = c.hsvSaturation
        val = c.hsvValue
        alpha = c.a
        return true
    }
    function setFromRgb(r, g, b) {
        var c = Qt.rgba(Math.max(0, Math.min(255, r)) / 255,
                        Math.max(0, Math.min(255, g)) / 255,
                        Math.max(0, Math.min(255, b)) / 255, 1)
        if (c.hsvHue >= 0) hue = c.hsvHue
        sat = c.hsvSaturation
        val = c.hsvValue
    }

    function openWith(hex) {
        initialHex = Theme.normalizeHex(hex) || "#000000"
        setFromHex(initialHex)
        // Rehacer los bindings rotos por la edición manual de la corrida previa.
        hexField.text = Qt.binding(function () { return root.currentHex() })
        rField.text = Qt.binding(function () { return Math.round(root.currentColor.r * 255) })
        gField.text = Qt.binding(function () { return Math.round(root.currentColor.g * 255) })
        bField.text = Qt.binding(function () { return Math.round(root.currentColor.b * 255) })
        open()
    }

    contentItem: ColumnLayout {
        spacing: 10

        // ── Plano saturación (x) / valor (y) ────────────────────────────────
        Rectangle {
            id: svPlane
            Layout.fillWidth: true
            Layout.preferredHeight: 170
            radius: 8
            clip: true
            color: Qt.hsva(root.hue, 1, 1, 1)

            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "#ffffffff" }
                    GradientStop { position: 1.0; color: "#00ffffff" }
                }
            }
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    orientation: Gradient.Vertical
                    GradientStop { position: 0.0; color: "#00000000" }
                    GradientStop { position: 1.0; color: "#ff000000" }
                }
            }

            Rectangle {   // handle
                width: 16; height: 16; radius: 8
                x: root.sat * svPlane.width - width / 2
                y: (1 - root.val) * svPlane.height - height / 2
                color: "transparent"
                border.color: "#ffffff"
                border.width: 2
                Rectangle {
                    anchors.fill: parent; anchors.margins: 2
                    radius: 6
                    color: "transparent"
                    border.color: "#80000000"
                    border.width: 1
                }
            }

            MouseArea {
                anchors.fill: parent
                function apply(mx, my) {
                    root.sat = Math.max(0, Math.min(1, mx / svPlane.width))
                    root.val = 1 - Math.max(0, Math.min(1, my / svPlane.height))
                }
                onPressed: (m) => apply(m.x, m.y)
                onPositionChanged: (m) => apply(m.x, m.y)
            }
        }

        // ── Tono ────────────────────────────────────────────────────────────
        Rectangle {
            id: hueBar
            Layout.fillWidth: true
            Layout.preferredHeight: 16
            radius: 8
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.000; color: "#ff0000" }
                GradientStop { position: 0.167; color: "#ffff00" }
                GradientStop { position: 0.333; color: "#00ff00" }
                GradientStop { position: 0.500; color: "#00ffff" }
                GradientStop { position: 0.667; color: "#0000ff" }
                GradientStop { position: 0.833; color: "#ff00ff" }
                GradientStop { position: 1.000; color: "#ff0000" }
            }
            Rectangle {
                width: 6; height: parent.height + 6; radius: 3
                y: -3
                x: root.hue * hueBar.width - width / 2
                color: "transparent"
                border.color: "#ffffff"; border.width: 2
            }
            MouseArea {
                anchors.fill: parent
                function apply(mx) { root.hue = Math.max(0, Math.min(0.9999, mx / hueBar.width)) }
                onPressed: (m) => apply(m.x)
                onPositionChanged: (m) => apply(m.x)
            }
        }

        // ── Alpha (opcional) ────────────────────────────────────────────────
        Rectangle {
            id: alphaBar
            visible: root.alphaEnabled
            Layout.fillWidth: true
            Layout.preferredHeight: 16
            radius: 8
            color: Theme.inputBg
            Rectangle {
                anchors.fill: parent
                radius: 8
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: Qt.hsva(root.hue, root.sat, root.val, 0) }
                    GradientStop { position: 1.0; color: Qt.hsva(root.hue, root.sat, root.val, 1) }
                }
            }
            Rectangle {
                width: 6; height: parent.height + 6; radius: 3
                y: -3
                x: root.alpha * alphaBar.width - width / 2
                color: "transparent"
                border.color: "#ffffff"; border.width: 2
            }
            MouseArea {
                anchors.fill: parent
                function apply(mx) { root.alpha = Math.max(0, Math.min(1, mx / alphaBar.width)) }
                onPressed: (m) => apply(m.x)
                onPositionChanged: (m) => apply(m.x)
            }
        }

        // ── Antes / ahora + hex ─────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Rectangle {
                Layout.preferredWidth: 46
                Layout.preferredHeight: 34
                radius: 6
                border.color: Theme.divider
                color: Theme.inputBg
                Rectangle {
                    anchors.fill: parent; anchors.margins: 1
                    radius: 5
                    color: root.initialHex
                    Rectangle {
                        width: parent.width / 2; height: parent.height
                        anchors.right: parent.right
                        color: root.currentColor
                    }
                }
                ToolTip.visible: hoverPrev.hovered
                ToolTip.text: "Izquierda: color previo. Derecha: elegido."
                HoverHandler { id: hoverPrev }
            }

            LcTextField {
                id: hexField
                Layout.fillWidth: true
                maximumLength: 9          // "#aarrggbb" — el hex tiene largo fijo
                placeholderText: root.alphaEnabled ? "#AARRGGBB" : "#RRGGBB"
                text: root.currentHex()
                validator: RegularExpressionValidator {
                    regularExpression: /#?[0-9a-fA-F]{0,8}/
                }
                onTextEdited: root.setFromHex(text)
                onEditingFinished: text = Qt.binding(function () { return root.currentHex() })
            }
        }

        // ── R / G / B ───────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: [
                    { lbl: "R", id: "r" },
                    { lbl: "G", id: "g" },
                    { lbl: "B", id: "b" },
                ]
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 4
                    Text {
                        text: modelData.lbl
                        color: Theme.textSecondary
                        font.pixelSize: 12
                    }
                    LcTextField {
                        Layout.fillWidth: true
                        maximumLength: 3
                        validator: IntValidator { bottom: 0; top: 255 }
                        text: modelData.id === "r" ? rField.text
                            : modelData.id === "g" ? gField.text : bField.text
                        onTextEdited: {
                            if (modelData.id === "r") rField.text = text
                            else if (modelData.id === "g") gField.text = text
                            else bField.text = text
                            root.setFromRgb(parseInt(rField.text || "0"),
                                            parseInt(gField.text || "0"),
                                            parseInt(bField.text || "0"))
                        }
                    }
                }
            }
        }

        // Campos reales (ocultos) que sostienen R/G/B; se rebindean en openWith().
        LcTextField { id: rField; visible: false; text: Math.round(root.currentColor.r * 255) }
        LcTextField { id: gField; visible: false; text: Math.round(root.currentColor.g * 255) }
        LcTextField { id: bField; visible: false; text: Math.round(root.currentColor.b * 255) }

        // ── Muestras rápidas ────────────────────────────────────────────────
        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: [Theme.accent, Theme.baseBg, Theme.textPrimary,
                        "#ffffff", "#000000", "#f38ba8", "#fab387", "#f9e2af",
                        "#a6e3a1", "#94e2d5", "#89b4fa", "#cba6f7"]
                delegate: Rectangle {
                    required property var modelData
                    width: 22; height: 22; radius: 5
                    color: modelData
                    border.color: Theme.divider
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.setFromHex(modelData)
                    }
                }
            }
        }
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
                text: (App.langV, App.l("common.cancel"))
                secondary: true
                onClicked: root.close()
            }
            LcButton {
                text: (App.langV, App.l("common.ok"))
                onClicked: {
                    root.picked(root.currentHex())
                    root.close()
                }
            }
        }
    }
}
