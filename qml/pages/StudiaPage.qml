import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0

// StudIA — asistente de estudio para Ingenieria Mecatronica (PPS UNLZ).
// Panel izquierdo: estado del indice documental y filtro por materia.
// Panel derecho: conversacion, con las fuentes citadas debajo de cada respuesta.
Item {
    id: root

    // El controlador necesita saber a que servidor pegarle; la URL la maneja App.
    Binding {
        target: Studia
        property: "serverUrl"
        value: App.serverRunning ? App.serverBaseUrl : ""
    }

    function enviar() {
        const t = entrada.text.trim()
        if (t.length === 0 || Studia.generando) return
        Studia.preguntar(t)
        entrada.text = ""
    }

    Connections {
        target: Studia
        function onMensajesChanged() { listaMsgs.positionViewAtEnd() }
        function onTextoParcial(indice, contenido) {
            const it = listaMsgs.itemAtIndex(indice)
            if (it && it.actualizarTexto) it.actualizarTexto(contenido)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            Layout.fillWidth: true
            title: "StudIA"
            subtitle: Studia.indiceListo
                ? (Studia.estadisticas.indexados + " documentos · "
                   + Studia.estadisticas.fragmentos + " fragmentos · "
                   + Studia.estadisticas.materias + " materias")
                : "Asistente de estudio · Ingeniería Mecatrónica UNLZ"
            action2Label: Studia.mensajes.length > 0 ? "Limpiar" : ""
            onAction2Clicked: Studia.limpiar()
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ── Panel izquierdo ──
            Rectangle {
                Layout.preferredWidth: 320
                Layout.fillHeight: true
                color: Theme.surfaceBg

                ColumnLayout {
                    anchors { fill: parent; margins: 16 }
                    spacing: 12

                    Text {
                        text: "BASE DOCUMENTAL"
                        color: Theme.textSecondary
                        font { pixelSize: 10; bold: true }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 54
                        radius: 8
                        color: Theme.inputBg
                        border.color: Studia.indiceListo ? Theme.successText : Theme.borderColor

                        Text {
                            anchors { fill: parent; margins: 8 }
                            text: Studia.indiceListo
                                  ? Studia.rutaIndice
                                  : (Studia.errorIndice.length > 0
                                     ? Studia.errorIndice : "Sin índice configurado")
                            color: Studia.indiceListo ? Theme.textPrimary : Theme.textDim
                            font.pixelSize: 11
                            wrapMode: Text.WrapAnywhere
                            elide: Text.ElideMiddle
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    LcButton {
                        Layout.fillWidth: true
                        text: Studia.indiceListo ? "Cambiar índice" : "Abrir índice"
                        onClicked: {
                            const f = Studia.elegirIndice()
                            if (f.length > 0) Studia.abrirIndice(f)
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

                    Text {
                        text: "MATERIA"
                        color: Theme.textSecondary
                        font { pixelSize: 10; bold: true }
                        visible: Studia.indiceListo
                    }

                    LcComboBox {
                        id: comboMateria
                        Layout.fillWidth: true
                        visible: Studia.indiceListo
                        model: ["Todas las materias"].concat(Studia.materias)
                        currentIndex: 0
                        onActivated: Studia.materiaFiltro =
                            (currentIndex === 0 ? "" : Studia.materias[currentIndex - 1])
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: Studia.indiceListo
                        text: "Acotar a una materia mejora la precisión cuando "
                            + "el tema aparece en varias."
                        color: Theme.textMuted
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }

                    Item { Layout.fillHeight: true }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: estadoTxt.implicitHeight + 16
                        radius: 6
                        color: App.serverRunning ? Theme.successBg : Theme.errorBg
                        Text {
                            id: estadoTxt
                            anchors { fill: parent; margins: 8 }
                            text: App.serverRunning
                                  ? "Servidor activo"
                                  : "Servidor detenido — arrancalo desde Lanzar para poder preguntar"
                            color: App.serverRunning ? Theme.successText : Theme.errorText
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            Rectangle { width: 1; Layout.fillHeight: true; color: Theme.divider }

            // ── Panel derecho: conversacion ──
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.baseBg

                ColumnLayout {
                    anchors { fill: parent; margins: 16 }
                    spacing: 12

                    // Estado vacio
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: Studia.mensajes.length === 0

                        Column {
                            anchors.centerIn: parent
                            width: Math.min(parent.width - 48, 540)
                            spacing: 10
                            Text {
                                width: parent.width
                                text: "StudIA"
                                color: Theme.textPrimary
                                font { pixelSize: 22; bold: true }
                                horizontalAlignment: Text.AlignHCenter
                            }
                            Text {
                                width: parent.width
                                text: Studia.indiceListo
                                    ? "Preguntá sobre el material de tu carrera. Respondo "
                                      + "con los apuntes, libros y trabajos prácticos indexados, "
                                      + "citando de dónde salió cada cosa.\n\n"
                                      + "Si la documentación no alcanza, te lo digo en vez de inventar."
                                    : "Abrí el índice documental para empezar.\n\n"
                                      + "Se genera con tools/studia/ingest.py sobre tu carpeta de material."
                                color: Theme.textMuted
                                font.pixelSize: 13
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    ListView {
                        id: listaMsgs
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: Studia.mensajes.length > 0
                        clip: true
                        spacing: 14
                        model: Studia.mensajes
                        ScrollBar.vertical: LcScrollBar {}

                        delegate: Column {
                            id: burbuja
                            width: listaMsgs.width
                            spacing: 6

                            readonly property bool esUsuario: modelData.rol === "usuario"
                            // Permite al streaming actualizar solo esta burbuja.
                            function actualizarTexto(t) { cuerpo.text = t }

                            Text {
                                text: burbuja.esUsuario ? "VOS" : "STUDIA"
                                color: Theme.textDim
                                font { pixelSize: 9; bold: true }
                            }

                            Rectangle {
                                width: parent.width
                                height: cuerpo.implicitHeight + 20
                                radius: 8
                                color: burbuja.esUsuario ? Theme.chatUserBubble : Theme.chatAsstBubble

                                Text {
                                    id: cuerpo
                                    anchors { fill: parent; margins: 10 }
                                    text: modelData.contenido.length > 0
                                          ? modelData.contenido
                                          : (modelData.escribiendo ? "⏳ Buscando en la documentación…" : "")
                                    color: burbuja.esUsuario ? Theme.chatUserText : Theme.chatAsstText
                                    font.pixelSize: 13
                                    wrapMode: Text.WordWrap
                                    textFormat: Text.PlainText
                                }
                            }

                            // Fuentes citadas: clickeables, abren el documento original.
                            Flow {
                                width: parent.width
                                spacing: 6
                                visible: !burbuja.esUsuario && modelData.fuentes
                                         && modelData.fuentes.length > 0

                                Repeater {
                                    model: modelData.fuentes
                                    delegate: Rectangle {
                                        radius: 4
                                        color: Theme.surfaceBg
                                        border.color: Theme.borderColor
                                        height: 22
                                        width: etiq.implicitWidth + 14

                                        Text {
                                            id: etiq
                                            anchors.centerIn: parent
                                            // Una entrada por documento, con todas sus páginas
                                            // juntas: "[1,3] apunte.pdf · pág. 11, 14, 16".
                                            text: "[" + modelData.refs + "] " + modelData.documento
                                                  + (modelData.paginas.length > 0
                                                     ? " · pág. " + modelData.paginas : "")
                                            color: Theme.textMuted
                                            font.pixelSize: 10
                                            elide: Text.ElideMiddle
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            hoverEnabled: true
                                            onEntered: parent.border.color = Theme.accent
                                            onExited:  parent.border.color = Theme.borderColor
                                            onClicked: Studia.abrirDocumento(modelData.ruta)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // ── Entrada ──
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.min(Math.max(46, entrada.implicitHeight + 18), 130)
                            radius: 8
                            color: Theme.inputBg
                            border.color: entrada.activeFocus ? Theme.inputBorderFocus : Theme.borderColor
                            clip: true

                            ScrollView {
                                anchors { fill: parent; margins: 6 }
                                TextArea {
                                    id: entrada
                                    enabled: Studia.indiceListo && App.serverRunning && !Studia.generando
                                    placeholderText: !Studia.indiceListo
                                        ? "Abrí un índice para empezar…"
                                        : (!App.serverRunning ? "Arrancá el servidor…"
                                                              : "Preguntá algo de la carrera…")
                                    color: Theme.textPrimary
                                    font.pixelSize: 13
                                    wrapMode: TextArea.Wrap
                                    background: null
                                    Keys.onReturnPressed: function(e) {
                                        if (e.modifiers & Qt.ShiftModifier) { e.accepted = false; return }
                                        e.accepted = true
                                        root.enviar()
                                    }
                                }
                            }
                        }

                        LcButton {
                            text: Studia.generando ? "Parar" : "Preguntar"
                            danger: Studia.generando
                            enabled: Studia.generando
                                     || (Studia.indiceListo && App.serverRunning
                                         && entrada.text.trim().length > 0)
                            onClicked: Studia.generando ? Studia.detener() : root.enviar()
                        }
                    }
                }
            }
        }
    }
}
