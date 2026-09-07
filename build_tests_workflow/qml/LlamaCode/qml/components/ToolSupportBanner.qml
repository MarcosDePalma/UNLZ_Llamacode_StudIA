import QtQuick
import QtQuick.Layouts
import LlamaCode 1.0

// Banner de aviso sobre la capacidad de tool-calling del perfil/modelo activo.
// `support`: "supported" | "unsupported" | "unknown". No se muestra si "supported"
// (o vacío). Solo advierte; nunca bloquea.
Rectangle {
    id: root
    property string support: "unknown"

    readonly property bool isUnsupported: support === "unsupported"
    visible: support === "unsupported" || support === "unknown"
    Layout.preferredHeight: visible ? row.implicitHeight + 20 : 0
    radius: 8
    color: Theme.surfaceBg
    border.color: isUnsupported ? Theme.errorText : Theme.warnText

    RowLayout {
        id: row
        anchors { fill: parent; leftMargin: 12; rightMargin: 12; topMargin: 10; bottomMargin: 10 }
        spacing: 10
        Text {
            text: root.isUnsupported ? "⚠" : "ⓘ"
            font.pixelSize: 14
            color: root.isUnsupported ? Theme.errorText : Theme.warnText
            Layout.alignment: Qt.AlignTop
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: 12
            color: Theme.textSecondary
            text: root.isUnsupported
                  ? "El modelo del perfil activo probablemente no soporta tool-calling (según el cookbook y/o su chat-template). "
                    + "Las Tasks y acciones que requieran herramientas pueden fallar. Usá un modelo Instruct con tool-calling "
                    + "(ej. Qwen2.5-Instruct, Hermes, Llama-3.1-Instruct)."
                  : "No pude confirmar si el modelo del perfil activo soporta tool-calling. Si las herramientas no se ejecutan, "
                    + "probá un modelo Instruct con tool-calling."
        }
    }
}
