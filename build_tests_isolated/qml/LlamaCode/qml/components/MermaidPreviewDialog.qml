import QtQuick
import QtQuick.Controls
import LlamaCode 1.0

// Preview interactivo de un diagrama Mermaid renderizado (PNG).
//  - Zoom (rueda / botones), pan (arrastre).
//  - Copiar el source mermaid al portapapeles.
// Se abre desde ChatPage al clickear una imagen de diagrama.
Dialog {
    id: root
    modal: true
    parent: Overlay.overlay
    width: Math.round(parent.width * 0.86)
    height: Math.round(parent.height * 0.86)
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // API: setear antes de open().
    property string imageSource: ""
    property string mermaidSource: ""
    property bool isSvg: false   // define extensión/filtro al guardar la fuente

    onOpened: { zoom = 1.0; img.x = 0; img.y = 0 }
    property real zoom: 1.0

    background: Rectangle {
        color: Theme.popupBg
        radius: 12
        border.color: Theme.popupBorderColor
        border.width: 1
    }
    Overlay.modal: Rectangle { color: Theme.overlayColor }

    contentItem: Item {
        Flickable {
            id: flick
            anchors.fill: parent
            anchors.margins: 8
            anchors.bottomMargin: 52
            contentWidth: Math.max(width, img.width * root.zoom)
            contentHeight: Math.max(height, img.height * root.zoom)
            clip: true

            Image {
                id: img
                source: root.imageSource
                fillMode: Image.PreserveAspectFit
                smooth: true
                scale: root.zoom
                transformOrigin: Item.TopLeft
                anchors.centerIn: root.zoom <= 1.0 ? parent : undefined
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.NoButton
                onWheel: (w) => {
                    const f = w.angleDelta.y > 0 ? 1.1 : 0.9
                    root.zoom = Math.max(0.25, Math.min(6.0, root.zoom * f))
                }
            }
        }

        // Barra inferior: zoom + copiar source + cerrar.
        Row {
            anchors { right: parent.right; bottom: parent.bottom; margins: 12 }
            spacing: 8

            LcButton { text: "−"; secondary: true; onClicked: root.zoom = Math.max(0.25, root.zoom * 0.83) }
            LcButton { text: "100%"; secondary: true; onClicked: { root.zoom = 1.0; img.x = 0; img.y = 0 } }
            LcButton { text: "+"; secondary: true; onClicked: root.zoom = Math.min(6.0, root.zoom * 1.2) }
            LcButton {
                text: "Copiar fuente"; secondary: true
                onClicked: { copyHelper.text = root.mermaidSource; copyHelper.selectAll(); copyHelper.copy() }
            }
            LcButton {
                text: "Guardar PNG"; secondary: true
                onClicked: {
                    const p = App.pickSavePath("diagrama.png", "PNG (*.png)")
                    if (p.length > 0) Mermaid.exportPng(root.mermaidSource, p)
                }
            }
            LcButton {
                text: "Guardar fuente"; secondary: true
                onClicked: {
                    const name = root.isSvg ? "diagrama.svg" : "diagrama.mmd"
                    const filt = root.isSvg ? "SVG (*.svg)" : "Mermaid (*.mmd)"
                    const p = App.pickSavePath(name, filt)
                    if (p.length > 0) Mermaid.exportSource(root.mermaidSource, p)
                }
            }
            LcButton { text: "Cerrar"; onClicked: root.close() }
        }

        // TextEdit oculto para usar copy() al portapapeles sin Clipboard QML.
        TextEdit { id: copyHelper; visible: false }
    }
}
