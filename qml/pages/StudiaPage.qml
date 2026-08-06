import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0

// StudIA — asistente de estudio para Ingenieria Mecatronica (PPS UNLZ).
//
// Barra superior: titulo + materia · estado del servidor · modos · indice.
// Panel izquierdo: selector de materia + historial de chats.
// Panel derecho: conversacion, fuentes citadas y barra de entrada con adjuntos.
Item {
    id: root

    Binding {
        target: Studia
        property: "serverUrl"
        value: App.serverRunning ? App.serverBaseUrl : ""
    }

    readonly property bool listoParaPreguntar:
        Studia.indiceListo && Studia.materiaElegida && App.serverRunning

    // Con la ventana angosta los chips de modo no entran y la barra se rompe:
    // por debajo de este ancho se colapsan en un menú desplegable.
    readonly property bool barraAngosta: root.width < 1150

    function etiquetaModo(id) {
        const ms = Studia.modos
        for (let i = 0; i < ms.length; ++i)
            if (ms[i].id === id) return ms[i].etiqueta
        return id
    }

    function enviar() {
        const t = entrada.text.trim()
        if (t.length === 0 || Studia.generando) return
        Studia.preguntar(t)
        // Se conserva el modo elegido para encadenar varias sin volver al menu.
        entrada.text = Studia.prefijoDeModo(Studia.modo)
        entrada.cursorPosition = entrada.text.length
    }

    function aplicarModo(id) {
        Studia.modo = id
        const pref = Studia.prefijoDeModo(id)
        // Se reemplaza el prefijo viejo sin pisar lo que el usuario ya escribio.
        const limpio = entrada.text.replace(/^\s*\/[A-Za-zÁÉÍÓÚáéíóúÑñ_-]+\/\s*/, "")
        entrada.text = pref + limpio
        entrada.cursorPosition = entrada.text.length
        entrada.forceActiveFocus()
    }

    Connections {
        target: Studia
        function onMensajesChanged() { listaMsgs.positionViewAtEnd() }
        function onTextoParcial(indice, contenido, bloques) {
            const it = listaMsgs.itemAtIndex(indice)
            if (it && it.actualizarTexto) it.actualizarTexto(contenido, bloques)
        }
        function onErrorOcurrido(msg) { aviso.mostrar(msg) }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Barra superior ──
        // Mismo alto y colores que PageHeader para no desentonar con el resto
        // de la app, pero con controles propios de StudIA.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: Theme.navBg

            Rectangle { anchors.bottom: parent.bottom; width: parent.width
                        height: 1; color: Theme.divider }

            RowLayout {
                anchors { fill: parent; leftMargin: 24; rightMargin: 16 }
                spacing: 14

                // Titulo + contadores DE LA MATERIA activa
                Column {
                    Layout.preferredWidth: 250
                    spacing: 2
                    Text {
                        width: parent.width
                        text: Studia.materiaElegida ? Studia.tituloSesion : "StudIA"
                        font { pixelSize: 16; bold: true }
                        color: Theme.textPrimary
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: {
                            if (!Studia.indiceListo) return "Sin índice"
                            if (!Studia.materiaElegida)
                                return Studia.estadisticas.materias + " materias disponibles"
                            const e = Studia.estadisticasMateria
                            const p = Studia.estadisticasPropias
                            let s = (e.indexados || 0) + " doc · "
                                    + (e.fragmentos || 0) + " frag"
                            if ((e.paginas || 0) > 0) s += " · " + e.paginas + " pág"
                            if ((p.indexados || 0) > 0) s += "  + " + p.indexados + " tuyos"
                            return s
                        }
                        font.pixelSize: 11
                        color: Theme.textMuted
                        elide: Text.ElideRight
                    }
                }

                // [4] Estado del servidor
                Rectangle {
                    Layout.preferredWidth: estadoRow.implicitWidth + 18
                    Layout.preferredHeight: 26
                    radius: 13
                    color: App.serverRunning ? Theme.successBg : Theme.errorBg
                    Row {
                        id: estadoRow
                        anchors.centerIn: parent
                        spacing: 6
                        Rectangle {
                            width: 8; height: 8; radius: 4
                            anchors.verticalCenter: parent.verticalCenter
                            color: App.serverRunning ? Theme.successText : Theme.errorText
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: App.serverRunning ? "Servidor activo" : "Servidor detenido"
                            font.pixelSize: 11
                            color: App.serverRunning ? Theme.successText : Theme.errorText
                        }
                    }
                }

                // [3] Modos de estudio. Ancho: chips a la vista. Angosto: menú.
                Row {
                    Layout.fillWidth: true
                    spacing: 4
                    visible: Studia.materiaElegida && !root.barraAngosta

                    Repeater {
                        model: Studia.modos
                        delegate: Rectangle {
                            height: 26
                            width: txtModo.implicitWidth + 20
                            radius: 13
                            color: Studia.modo === modelData.id ? Theme.accent : "transparent"
                            border.color: Studia.modo === modelData.id ? Theme.accent
                                                                       : Theme.borderColor
                            Text {
                                id: txtModo
                                anchors.centerIn: parent
                                text: modelData.etiqueta
                                font { pixelSize: 11; bold: Studia.modo === modelData.id }
                                color: Studia.modo === modelData.id ? Theme.btnPrimaryText
                                                                    : Theme.textSecondary
                            }
                            ToolTip.visible: areaModo.containsMouse
                            ToolTip.text: modelData.descripcion
                            ToolTip.delay: 500
                            MouseArea {
                                id: areaModo
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.aplicarModo(modelData.id)
                            }
                        }
                    }
                }

                // Versión compacta del selector de modos.
                Rectangle {
                    visible: Studia.materiaElegida && root.barraAngosta
                    Layout.preferredWidth: txtModoSel.implicitWidth + 34
                    Layout.preferredHeight: 26
                    radius: 13
                    color: Studia.modo !== "libre" ? Theme.accent : "transparent"
                    border.color: Studia.modo !== "libre" ? Theme.accent : Theme.borderColor

                    Row {
                        anchors.centerIn: parent
                        spacing: 6
                        Text {
                            id: txtModoSel
                            text: root.etiquetaModo(Studia.modo)
                            font { pixelSize: 11; bold: Studia.modo !== "libre" }
                            color: Studia.modo !== "libre" ? Theme.btnPrimaryText
                                                           : Theme.textSecondary
                        }
                        Text {
                            text: "▾"
                            font.pixelSize: 10
                            color: Studia.modo !== "libre" ? Theme.btnPrimaryText
                                                           : Theme.textSecondary
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: menuModos.open()
                    }
                    Menu {
                        id: menuModos
                        y: parent.height + 4
                        Repeater {
                            model: Studia.modos
                            delegate: MenuItem {
                                text: modelData.etiqueta
                                onTriggered: root.aplicarModo(modelData.id)
                            }
                        }
                    }
                }

                Item { Layout.fillWidth: true; visible: root.barraAngosta || !Studia.materiaElegida }

                // [2] Base documental + limpiar
                LcButton {
                    text: Studia.indiceListo ? "Base documental" : "Abrir índice"
                    secondary: Studia.indiceListo
                    ToolTip.visible: hovered && Studia.indiceListo
                    ToolTip.text: Studia.rutaIndice
                    onClicked: {
                        const f = Studia.elegirIndice()
                        if (f.length > 0) Studia.abrirIndice(f)
                    }
                }

                LcButton {
                    visible: Studia.mensajes.length > 0
                    text: "Limpiar chat"
                    secondary: true
                    onClicked: Studia.limpiar()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ── Panel izquierdo: materia + historial ──
            Rectangle {
                Layout.preferredWidth: 280
                Layout.fillHeight: true
                color: Theme.surfaceBg

                ColumnLayout {
                    anchors { fill: parent; margins: 14 }
                    spacing: 10

                    Text {
                        text: "MATERIA"
                        color: Studia.materiaElegida ? Theme.textSecondary : Theme.warnText
                        font { pixelSize: 10; bold: true }
                    }

                    LcComboBox {
                        id: comboMateria
                        Layout.fillWidth: true
                        enabled: Studia.indiceListo
                        // Sin opción "todas": estudiar es siempre sobre una materia.
                        model: ["— Elegí una materia —"].concat(Studia.materias)
                        currentIndex: {
                            const i = Studia.materias.indexOf(Studia.materia)
                            return i < 0 ? 0 : i + 1
                        }
                        onActivated: {
                            if (currentIndex > 0)
                                Studia.materia = Studia.materias[currentIndex - 1]
                        }
                    }

                    // Bibliografía propia de esta materia
                    Rectangle {
                        Layout.fillWidth: true; height: 1; color: Theme.divider
                        visible: Studia.materiaElegida
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: Studia.materiaElegida
                        Text {
                            Layout.fillWidth: true
                            text: "MI BIBLIOGRAFÍA"
                            color: Theme.textSecondary
                            font { pixelSize: 10; bold: true }
                        }
                        Text {
                            text: "+ agregar"
                            color: areaAgregar.containsMouse ? Theme.accent : Theme.textDim
                            font.pixelSize: 10
                            MouseArea {
                                id: areaAgregar
                                anchors { fill: parent; margins: -4 }
                                enabled: !Studia.adjuntando
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                // Selección múltiple: se pueden elegir varios de una.
                                onClicked: Studia.adjuntarBibliografia(Studia.elegirArchivos())
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: Studia.materiaElegida
                                 && Studia.bibliografiaPropia.length === 0
                        text: "Nada agregado. Lo que subas se guarda aparte del "
                            + "material de cátedra."
                        color: Theme.textMuted
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }

                    // Tope de 5 documentos a la vista; a partir de ahí scrollea,
                    // para no comerse el panel cuando hay muchos.
                    ListView {
                        id: listaBiblio
                        readonly property int alturaFila: 32   // 30 + spacing
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(contentHeight, alturaFila * 5)
                        visible: Studia.materiaElegida && count > 0
                        clip: true
                        spacing: 2
                        model: Studia.bibliografiaPropia
                        ScrollBar.vertical: LcScrollBar {}

                        delegate: Rectangle {
                            width: listaBiblio.width
                            height: 30
                            radius: 4
                            color: areaDoc.containsMouse ? Theme.hoverBg : "transparent"

                            Text {
                                anchors { left: parent.left; right: btnQuitar.left
                                          leftMargin: 6; rightMargin: 4
                                          verticalCenter: parent.verticalCenter }
                                text: "📎 " + modelData.nombre
                                color: modelData.estado === "ok" ? Theme.textPrimary
                                                                 : Theme.warnText
                                font.pixelSize: 11
                                elide: Text.ElideMiddle
                            }
                            ToolTip.visible: areaDoc.containsMouse
                            ToolTip.text: modelData.estado === "ok"
                                ? (modelData.fragmentos + " fragmentos · click para abrirlo")
                                : ("No se pudo indexar: " + modelData.detalle)

                            MouseArea {
                                id: areaDoc
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: Studia.abrirDocumento(modelData.ruta)
                            }

                            Rectangle {
                                id: btnQuitar
                                z: 2
                                width: 20; height: 20; radius: 10
                                anchors { right: parent.right; rightMargin: 4
                                          verticalCenter: parent.verticalCenter }
                                visible: areaDoc.containsMouse || areaQuitar.containsMouse
                                color: areaQuitar.containsMouse ? Theme.errorBg : "transparent"
                                Text {
                                    anchors.centerIn: parent
                                    text: "✕"
                                    color: areaQuitar.containsMouse ? Theme.errorText : Theme.textDim
                                    font.pixelSize: 10
                                }
                                MouseArea {
                                    id: areaQuitar
                                    anchors.fill: parent
                                    enabled: !Studia.adjuntando
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: Studia.quitarBibliografia(modelData.ruta)
                                }
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.divider }

                    // [5] Historial de chats
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: "MIS CHATS"
                            color: Theme.textSecondary
                            font { pixelSize: 10; bold: true }
                        }
                        Text {
                            text: Studia.sesiones.length
                            color: Theme.textDim
                            font.pixelSize: 10
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: Studia.sesiones.length === 0
                        text: "Todavía no abriste ningún chat. Elegí una materia "
                            + "arriba para empezar uno."
                        color: Theme.textMuted
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }

                    ListView {
                        id: listaSesiones
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 3
                        model: Studia.sesiones
                        ScrollBar.vertical: LcScrollBar {}

                        delegate: Rectangle {
                            width: listaSesiones.width
                            height: 44
                            radius: 6
                            readonly property bool activo: modelData.materia === Studia.materia
                            color: activo ? Theme.highlight
                                          : (areaSesion.containsMouse ? Theme.hoverBg
                                                                      : "transparent")

                            Rectangle {
                                visible: parent.activo
                                width: 3; height: parent.height - 12; radius: 2
                                anchors { left: parent.left; leftMargin: 2
                                          verticalCenter: parent.verticalCenter }
                                color: Theme.accent
                            }

                            Column {
                                anchors { left: parent.left; right: btnBorrar.left
                                          leftMargin: 10; rightMargin: 4
                                          verticalCenter: parent.verticalCenter }
                                spacing: 2
                                Text {
                                    width: parent.width
                                    text: modelData.materia
                                    color: Theme.textPrimary
                                    font { pixelSize: 12; bold: parent.parent.activo }
                                    elide: Text.ElideRight
                                }
                                Text {
                                    width: parent.width
                                    text: modelData.mensajes > 0
                                          ? modelData.mensajes + " mensajes"
                                          : "vacío"
                                    color: Theme.textDim
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }
                            }

                            MouseArea {
                                id: areaSesion
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                // El click abre esa materia: la sesión es la materia.
                                onClicked: Studia.materia = modelData.materia
                            }

                            // Va DESPUÉS de areaSesion y con z mayor: si no, el
                            // MouseArea que cubre toda la fila se come el click.
                            Rectangle {
                                id: btnBorrar
                                z: 2
                                width: 22; height: 22; radius: 11
                                anchors { right: parent.right; rightMargin: 6
                                          verticalCenter: parent.verticalCenter }
                                visible: areaSesion.containsMouse || areaBorrar.containsMouse
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
                                    onClicked: Studia.borrarSesion(modelData.materia)
                                }
                            }
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
                            width: Math.min(parent.width - 48, 560)
                            spacing: 10
                            Text {
                                width: parent.width
                                text: Studia.materiaElegida ? Studia.materia : "StudIA"
                                color: Theme.textPrimary
                                font { pixelSize: 22; bold: true }
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                            }
                            Text {
                                width: parent.width
                                text: !Studia.indiceListo
                                    ? "Abrí el índice documental desde «Abrir índice», arriba a la derecha.\n\n"
                                      + "Se genera con tools/studia/ingest.py sobre tu carpeta de material."
                                    : (!Studia.materiaElegida
                                       ? "Elegí una materia en el panel de la izquierda.\n\n"
                                         + "Cada materia abre su propia conversación."
                                       : "Preguntá sobre el material de esta materia. Respondo "
                                         + "con los apuntes indexados, citando de dónde salió cada cosa.\n\n"
                                         + "Arriba elegís el modo: resumen, explicación, autoevaluación, "
                                         + "flashcards o plan de estudio.")
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
                            // Texto plano para copiar; bloques para render.
                            property string textoPlano: modelData.contenido
                            property var bloques: modelData.bloques !== undefined
                                                  ? modelData.bloques : []
                            function actualizarTexto(t, bs) {
                                textoPlano = t
                                bloques = bs
                            }

                            Row {
                                spacing: 6
                                Text {
                                    text: burbuja.esUsuario ? "VOS" : "STUDIA"
                                    color: Theme.textDim
                                    font { pixelSize: 9; bold: true }
                                }
                                Text {
                                    visible: modelData.modo !== undefined
                                             && modelData.modo.length > 0
                                             && modelData.modo !== "libre"
                                    text: "· " + modelData.modo
                                    color: Theme.accent
                                    font { pixelSize: 9; bold: true }
                                }
                                Text {
                                    // Se usa Text (no TextEdit) para poder dar
                                    // interlineado: TextEdit no expone lineHeight.
                                    // El copiado va por este botón.
                                    visible: !burbuja.esUsuario && areaBurbuja.containsMouse
                                             && burbuja.textoPlano.length > 0
                                    text: "⧉ copiar"
                                    color: Theme.textDim
                                    font.pixelSize: 9
                                    MouseArea {
                                        anchors { fill: parent; margins: -4 }
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: App.copyToClipboard(burbuja.textoPlano)
                                    }
                                }
                            }

                            Rectangle {
                                width: parent.width
                                height: contenidoCol.implicitHeight + 28
                                radius: 8
                                color: burbuja.esUsuario ? Theme.chatUserBubble : Theme.chatAsstBubble

                                MouseArea {
                                    id: areaBurbuja
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    acceptedButtons: Qt.NoButton
                                }

                                Column {
                                    id: contenidoCol
                                    x: 14
                                    y: 14
                                    width: parent.width - 28
                                    spacing: 10

                                    // Mensaje del usuario o placeholder: un solo texto.
                                    Text {
                                        width: parent.width
                                        visible: burbuja.esUsuario
                                                 || burbuja.bloques.length === 0
                                        text: burbuja.textoPlano.length > 0
                                              ? burbuja.textoPlano
                                              : (modelData.escribiendo
                                                 ? "⏳ Buscando en la documentación…" : "")
                                        color: burbuja.esUsuario ? Theme.chatUserText
                                                                 : Theme.chatAsstText
                                        font.pixelSize: 15
                                        lineHeight: 1.45
                                        lineHeightMode: Text.ProportionalHeight
                                        wrapMode: Text.Wrap
                                        textFormat: Text.PlainText
                                    }

                                    // Respuesta de StudIA: texto Markdown y ecuaciones
                                    // en su propio renglón, centradas y más grandes.
                                    Repeater {
                                        model: burbuja.esUsuario ? [] : burbuja.bloques
                                        delegate: Loader {
                                            width: contenidoCol.width
                                            sourceComponent: modelData.tipo === "ecuacion"
                                                             ? compEcuacion : compTexto

                                            Component {
                                                id: compTexto
                                                Text {
                                                    width: contenidoCol.width
                                                    text: modelData.contenido
                                                    color: Theme.chatAsstText
                                                    font.pixelSize: 15
                                                    lineHeight: 1.45
                                                    lineHeightMode: Text.ProportionalHeight
                                                    wrapMode: Text.Wrap
                                                    // Markdown: títulos, negritas y listas
                                                    // se ven como tales, no como ** y ##.
                                                    textFormat: Text.MarkdownText
                                                }
                                            }
                                            Component {
                                                id: compEcuacion
                                                Rectangle {
                                                    width: contenidoCol.width
                                                    height: ecu.implicitHeight + 24
                                                    radius: 6
                                                    color: Theme.baseBg
                                                    Text {
                                                        id: ecu
                                                        anchors.centerIn: parent
                                                        width: parent.width - 24
                                                        text: modelData.contenido
                                                        color: Theme.textPrimary
                                                        // Cuerpo mayor y centrada, como
                                                        // una ecuación insertada en Word.
                                                        font.pixelSize: 19
                                                        lineHeight: 1.5
                                                        lineHeightMode: Text.ProportionalHeight
                                                        horizontalAlignment: Text.AlignHCenter
                                                        wrapMode: Text.Wrap
                                                        textFormat: Text.PlainText
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            // Fuentes citadas, una entrada por documento.
                            Flow {
                                width: parent.width
                                spacing: 6
                                visible: !burbuja.esUsuario && modelData.fuentes
                                         && modelData.fuentes.length > 0

                                Repeater {
                                    model: modelData.fuentes
                                    delegate: Rectangle {
                                        radius: 4
                                        // La bibliografía propia se distingue de la
                                        // de cátedra a simple vista.
                                        color: modelData.propio ? Theme.highlight : Theme.surfaceBg
                                        border.color: Theme.borderColor
                                        height: 22
                                        width: Math.min(etiq.implicitWidth + 14, listaMsgs.width - 20)

                                        Text {
                                            id: etiq
                                            anchors.centerIn: parent
                                            width: parent.width - 14
                                            text: (modelData.propio ? "📎 " : "")
                                                  + "[" + modelData.refs + "] " + modelData.documento
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

                    // Aviso (materia sin elegir, servidor caído, adjunto fallido…)
                    Rectangle {
                        id: aviso
                        Layout.fillWidth: true
                        Layout.preferredHeight: visible ? avisoTxt.implicitHeight + 16 : 0
                        visible: false
                        radius: 6
                        color: Theme.errorBg
                        function mostrar(m) { avisoTxt.text = m; visible = true; ocultar.restart() }
                        Timer { id: ocultar; interval: 7000; onTriggered: aviso.visible = false }
                        Text {
                            id: avisoTxt
                            anchors { fill: parent; margins: 8 }
                            color: Theme.errorText
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }
                    }

                    // Progreso del adjunto
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Studia.adjuntando ? 30 : 0
                        visible: Studia.adjuntando
                        radius: 6
                        color: Theme.surfaceBg
                        Text {
                            anchors { fill: parent; margins: 8 }
                            text: "📎 Procesando «" + Studia.ultimoAdjunto + "»…"
                                  + (Studia.adjuntosPendientes > 0
                                     ? "  (quedan " + Studia.adjuntosPendientes + ")" : "")
                            color: Theme.textMuted
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                        }
                    }

                    // ── Entrada ──
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        // [6] Adjuntar bibliografía propia
                        Rectangle {
                            Layout.preferredWidth: 40
                            Layout.preferredHeight: 40
                            radius: 8
                            color: areaClip.containsMouse && areaClip.enabled ? Theme.hoverBg
                                                                              : "transparent"
                            border.color: Theme.borderColor
                            opacity: areaClip.enabled ? 1.0 : 0.4

                            Text {
                                anchors.centerIn: parent
                                text: "📎"
                                font.pixelSize: 16
                            }
                            ToolTip.visible: areaClip.containsMouse
                            ToolTip.text: Studia.materiaElegida
                                ? "Agregar bibliografía propia a " + Studia.materia
                                  + " (va a un índice aparte del material de cátedra)"
                                : "Elegí una materia para poder adjuntar bibliografía"
                            MouseArea {
                                id: areaClip
                                anchors.fill: parent
                                enabled: Studia.materiaElegida && !Studia.adjuntando
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: Studia.adjuntarBibliografia(Studia.elegirArchivos())
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.min(Math.max(40, entrada.implicitHeight + 18), 130)
                            radius: 8
                            color: Theme.inputBg
                            border.color: entrada.activeFocus ? Theme.inputBorderFocus : Theme.borderColor
                            clip: true

                            ScrollView {
                                anchors { fill: parent; margins: 6 }
                                TextArea {
                                    id: entrada
                                    enabled: root.listoParaPreguntar && !Studia.generando
                                    placeholderText: !Studia.indiceListo
                                        ? "Abrí un índice para empezar…"
                                        : (!Studia.materiaElegida
                                           ? "Elegí una materia…"
                                           : (!App.serverRunning ? "Arrancá el servidor…"
                                                                 : "Preguntá algo de " + Studia.materia + "…"))
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
                                     || (root.listoParaPreguntar && entrada.text.trim().length > 0)
                            onClicked: Studia.generando ? Studia.detener() : root.enviar()
                        }
                    }
                }
            }
        }
    }
}
