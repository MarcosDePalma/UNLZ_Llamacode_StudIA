import QtQuick
import QtQuick.Controls

// ScrollBar que respeta los colores del tema activo.
ScrollBar {
    id: root

    implicitWidth: orientation === Qt.Vertical ? 14 : 100
    implicitHeight: orientation === Qt.Vertical ? 100 : 14
    padding: 3
    interactive: true
    minimumSize: 0.06

    contentItem: Rectangle {
        implicitWidth: 8
        implicitHeight: 8
        radius: Math.min(width, height) / 2
        color: root.pressed ? Theme.accent
                            : (root.hovered ? Theme.textPrimary : Theme.textSecondary)
        opacity: root.size >= 1.0 ? 0.0
                                  : (root.pressed ? 1.0
                                                  : (root.hovered || root.active ? 0.9 : 0.58))
        Behavior on color { ColorAnimation { duration: 100 } }
        Behavior on opacity { NumberAnimation { duration: 120 } }
    }

    background: Rectangle {
        radius: Math.min(width, height) / 2
        color: Theme.surfaceBg
        border.width: root.hovered || root.pressed ? 1 : 0
        border.color: Theme.borderColor
        opacity: root.size >= 1.0 ? 0.0
                                  : (root.hovered || root.pressed ? 0.8 : 0.28)
        Behavior on opacity { NumberAnimation { duration: 120 } }
    }
}
