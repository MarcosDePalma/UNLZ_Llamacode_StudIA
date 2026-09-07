import QtQuick
import QtQuick.Controls
import LlamaCode 1.0

// ComboBox con estilo del tema (oscuro/oled) y popup scrollable.
// Hereda ComboBox: cualquier propiedad/handler/id existente sigue funcionando,
// y un override per-uso (contentItem/background/delegate) gana sobre estos defaults.
ComboBox {
    id: control

    background: Rectangle {
        color: Theme.inputBg
        radius: 6
        border.color: Theme.borderColor
    }

    contentItem: Text {
        text: control.displayText
        color: Theme.theme === "oled" ? "white" : Theme.textPrimary
        font.pixelSize: 13
        leftPadding: 10
        rightPadding: control.indicator ? control.indicator.width + 4 : 10
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    delegate: ItemDelegate {
        id: lcDelegate
        width: control.width
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: {
                var role = control.textRole
                if (role && role.length > 0) {
                    // array-de-objetos: el item es modelData
                    if (modelData !== undefined && modelData !== null && typeof modelData === 'object'
                        && modelData[role] !== undefined && modelData[role] !== null)
                        return String(modelData[role])
                    // QAbstractItemModel: roles vía model
                    if (typeof model === 'object' && model !== null
                        && model[role] !== undefined && model[role] !== null)
                        return String(model[role])
                }
                // array-de-strings / valores planos
                if (modelData !== undefined && modelData !== null && typeof modelData !== 'object')
                    return String(modelData)
                return ""
            }
            color: Theme.theme === "oled" ? "white" : Theme.textPrimary
            font.pixelSize: 13
            leftPadding: 6
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: lcDelegate.highlighted ? Theme.borderColor : Theme.inputBg
        }
    }

    popup: Popup {
        y: control.height
        width: control.width
        padding: 1
        implicitHeight: Math.min(lcListView.contentHeight + 2, 360)
        contentItem: ListView {
            id: lcListView
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        }
        background: Rectangle {
            color: Theme.inputBg
            border.color: Theme.borderColor
            radius: 6
        }
    }
}
