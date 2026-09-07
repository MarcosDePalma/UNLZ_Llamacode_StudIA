import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0

Dialog {
    id: root
    modal: true
    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    leftPadding: 18
    rightPadding: 18
    topPadding: 14
    bottomPadding: 14
    clip: true

    implicitWidth: Math.max(420, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: implicitHeaderHeight + implicitContentHeight + implicitFooterHeight + topPadding + bottomPadding
    // Popup no siempre adopta implicitHeight cuando el consumidor fija width.
    // Mantener un alto real evita que header/footer se monten sobre el contenido.
    height: Math.min(parent ? Math.max(180, parent.height - 48) : 720,
                     Math.max(180, implicitHeight))
    closePolicy: Popup.CloseOnEscape

    background: Rectangle {
        color: Theme.popupBg
        radius: 12
        border.color: Theme.popupBorderColor
        border.width: 1
    }

    Overlay.modal: Rectangle { color: Theme.overlayColor }

    header: Rectangle {
        color: Theme.popupHeaderBg
        height: 56
        radius: 12
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 12; color: Theme.popupHeaderBg }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.popupHeaderBorder }
        Text {
            anchors { left: parent.left; leftMargin: 22; verticalCenter: parent.verticalCenter }
            text: root.title
            font { pixelSize: 14; bold: true }
            color: Theme.textPrimary
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
            LcButton { text: (App.langV, App.l("common.cancel")); secondary: true; onClicked: root.reject() }
            LcButton { text: (App.langV, App.l("common.ok")); onClicked: root.accept() }
        }
    }
}
