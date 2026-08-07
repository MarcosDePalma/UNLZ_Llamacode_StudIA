import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode

// Lista de TEMAS de la materia activa: las conversaciones que conviven dentro
// de una misma asignatura sin mezclarse.
//
// Vive en un componente propio porque se usa en dos lados —el panel izquierdo,
// siempre a la vista, y el popup del boton "Temas"— y duplicar la lista seria
// duplicar tambien el renombre y el borrado.
ColumnLayout {
    id: root
    spacing: 8

    // Al abrirse desde el popup conviene cerrarlo despues de elegir un tema.
    signal temaElegido()

    // Id del tema que se esta renombrando; "" si no hay ninguno en edicion.
    property string editando: ""

    function empezarRenombre(id) {
        root.editando = id
    }

    RowLayout {
        Layout.fillWidth: true
        Text {
            Layout.fillWidth: true
            text: "TEMAS"
            color: Theme.textSecondary
            font { pixelSize: 10; bold: true }
        }
        Text {
            text: Studia.temas.length
            color: Theme.textDim
            font.pixelSize: 10
        }
    }

    Text {
        Layout.fillWidth: true
        visible: !Studia.materiaElegida
        text: "Elegí una materia para empezar."
        color: Theme.textMuted
        font.pixelSize: 11
        wrapMode: Text.WordWrap
    }

    ListView {
        id: lista
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 60
        visible: Studia.materiaElegida
        clip: true
        spacing: 3
        model: Studia.temas
        ScrollBar.vertical: LcScrollBar {}

        delegate: Rectangle {
            id: fila
            width: lista.width
            height: 46
            radius: 6
            readonly property bool activo: modelData.id === Studia.temaId
            readonly property bool enEdicion: root.editando === modelData.id
            color: activo ? Theme.highlight
                          : (areaTema.containsMouse ? Theme.hoverBg : "transparent")

            Rectangle {
                visible: fila.activo
                width: 3; height: parent.height - 12; radius: 2
                anchors { left: parent.left; leftMargin: 2
                          verticalCenter: parent.verticalCenter }
                color: Theme.accent
            }

            Column {
                id: datos
                visible: !fila.enEdicion
                anchors { left: parent.left; right: acciones.left
                          leftMargin: 10; rightMargin: 4
                          verticalCenter: parent.verticalCenter }
                spacing: 2
                Text {
                    width: parent.width
                    text: modelData.titulo
                    color: Theme.textPrimary
                    font { pixelSize: 12; bold: fila.activo }
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: modelData.mensajes > 0
                          ? modelData.mensajes + " mensajes"
                          : "sin preguntas todavía"
                    color: Theme.textDim
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
            }

            // Renombre en el lugar: Enter confirma, Escape cancela.
            LcTextField {
                id: campoNombre
                visible: fila.enEdicion
                anchors { left: parent.left; right: parent.right
                          leftMargin: 8; rightMargin: 8
                          verticalCenter: parent.verticalCenter }
                onVisibleChanged: if (visible) { text = modelData.titulo
                                                 forceActiveFocus(); selectAll() }
                onAccepted: {
                    Studia.renombrarTema(modelData.id, text)
                    root.editando = ""
                }
                Keys.onEscapePressed: root.editando = ""
                // Si pierde el foco sin confirmar, se cancela: guardar a
                // escondidas un nombre a medio escribir seria peor.
                onActiveFocusChanged: if (!activeFocus && fila.enEdicion)
                                          root.editando = ""
            }

            MouseArea {
                id: areaTema
                anchors.fill: parent
                enabled: !fila.enEdicion
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    Studia.abrirTema(modelData.id)
                    root.temaElegido()
                }
                // Doble click sobre el nombre: renombrar, como en el explorador.
                onDoubleClicked: root.empezarRenombre(modelData.id)
            }

            // Van DESPUES del MouseArea de la fila y con z mayor: si no, el que
            // cubre toda la fila se come el click de los botones.
            Row {
                id: acciones
                z: 2
                spacing: 2
                visible: !fila.enEdicion
                          && (areaTema.containsMouse || areaLapiz.containsMouse
                              || areaBorrar.containsMouse)
                anchors { right: parent.right; rightMargin: 6
                          verticalCenter: parent.verticalCenter }

                Rectangle {
                    width: 22; height: 22; radius: 11
                    color: areaLapiz.containsMouse ? Theme.hoverBg : "transparent"
                    Text {
                        anchors.centerIn: parent
                        text: "✎"
                        color: areaLapiz.containsMouse ? Theme.textPrimary : Theme.textDim
                        font.pixelSize: 12
                    }
                    MouseArea {
                        id: areaLapiz
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.empezarRenombre(modelData.id)
                    }
                }

                Rectangle {
                    width: 22; height: 22; radius: 11
                    color: areaBorrar.containsMouse ? Theme.errorBg : "transparent"
                    Text {
                        anchors.centerIn: parent
                        text: "✕"
                        color: areaBorrar.containsMouse ? Theme.errorText : Theme.textDim
                        font.pixelSize: 11
                    }
                    MouseArea {
                        id: areaBorrar
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Studia.borrarTema(modelData.id)
                    }
                }
            }
        }
    }

    LcButton {
        Layout.fillWidth: true
        visible: Studia.materiaElegida
        // Sin preguntas todavia no hay tema que dejar atras: crear otro dejaria
        // dos vacios y ninguno con nombre.
        enabled: Studia.puedeCrearTema
        text: "+ Nuevo tema"
        secondary: true
        ToolTip.visible: hovered && !Studia.puedeCrearTema
        ToolTip.text: "Preguntá algo primero: el tema toma su nombre de la "
                    + "primera pregunta"
        onClicked: {
            Studia.nuevoTema()
            root.temaElegido()
        }
    }
}
