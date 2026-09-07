import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0
import "NavigationPolicy.js" as NavigationPolicy

Rectangle {
    id: root
    width: 200
    // Translúcida (tema custom): baja el alpha del fondo para dejar ver la ventana.
    readonly property color navBase: Theme.navBg
    color: Theme.sidebarTranslucent
        ? Qt.rgba(navBase.r, navBase.g, navBase.b, 0.8)
        : navBase

    property int currentIndex: 0
    signal pageSelected(int index)

    readonly property var pages: [
        { key: "nav.launch",   icon: "🚀",  serverOnly: false },
        { key: "nav.profiles", icon: "📋",  serverOnly: false },
        { key: "nav.models",   icon: "📦",  serverOnly: false },
        { key: "nav.binaries", icon: "⚙",   serverOnly: false },
        { key: "nav.chat",      icon: "💬",  serverOnly: true  },
        { key: "agent.title",   icon: "🤖",  serverOnly: true,
          keepDuringAgentTransition: true, keepDuringThinkingRestart: true },
        { key: "nav.research",  icon: "🔎",  serverOnly: true  },
        { key: "nav.datalab",   label: "Data Lab", icon: "🧾", serverOnly: false },
        { key: "nav.tasks",     icon: "🗒",  serverOnly: true, agentOnly: true,
          keepDuringAgentTransition: true },
        { key: "nav.charla",    icon: "🎙",  serverOnly: true  },
        { key: "nav.benchmark", icon: "📊",  serverOnly: false },
        { key: "tuner", label: "Tuner", icon: "🎛", serverOnly: false },
        { key: "nav.downloads", icon: "⬇",   serverOnly: false },
        { key: "agents", label: "Agentes", icon: "🧠", serverOnly: false },
    ]

    // Ajustes va al pie, fuera del Repeater: su índice es el que sigue a la
    // última página. Derivarlo evita que agregar una sección lo desincronice
    // del StackLayout (era un 12 hardcodeado en tres lugares).
    readonly property int settingsIndex: pages.length

    // Índice de una página por su key, para navegar sin números mágicos.
    // -1 si no existe.
    function indexOfKey(key) {
        for (var i = 0; i < pages.length; ++i)
            if (pages[i].key === key) return i
        return -1
    }

    ColumnLayout {
        anchors { fill: parent; margins: 0 }
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            height: 56
            color: Theme.titleBg
            Text {
                anchors.centerIn: parent
                text: "UNLZ_Llamacode"
                font { pixelSize: 16; bold: true }
                color: Theme.textPrimary
            }
        }

        Repeater {
            model: root.pages
            delegate: ItemDelegate {
                Layout.fillWidth: true
                height: 48
                highlighted: root.currentIndex === index
                // agentOnly: permite entrar también mientras el agente ARRANCA
                // (App.agentStarting) → la página muestra su popup "Iniciando agente"
                // con los botones deshabilitados, igual que Agente. Solo queda
                // grisada si el agente no fue iniciado en absoluto.
                enabled: NavigationPolicy.pageEnabled(modelData, App.backendAvailable,
                                                       App.agentRunning, App.agentStarting)
                opacity: enabled ? 1.0 : 0.35
                background: Rectangle {
                    color: parent.highlighted ? Theme.highlight : (parent.hovered && parent.enabled ? Theme.hoverBg : "transparent")
                    Rectangle {
                        visible: parent.parent.highlighted
                        width: 3; height: parent.height
                        anchors.left: parent.left
                        color: Theme.accent
                    }
                }
                contentItem: Row {
                    spacing: 12
                    anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                    Text {
                        text: modelData.label || (App.langV, App.l(modelData.key))
                        font.pixelSize: 14
                        color: root.currentIndex === index ? Theme.textPrimary : Theme.textSecondary
                    }
                }
                onClicked: { root.currentIndex = index; root.pageSelected(index) }
            }
        }

        Item { Layout.fillHeight: true }

        ItemDelegate {
            Layout.fillWidth: true
            height: 48
            highlighted: root.currentIndex === root.settingsIndex
            background: Rectangle {
                color: parent.highlighted ? Theme.highlight : (parent.hovered ? Theme.hoverBg : "transparent")
                Rectangle {
                    visible: parent.parent.highlighted
                    width: 3; height: parent.height
                    anchors.left: parent.left
                    color: Theme.accent
                }
            }
            contentItem: Row {
                spacing: 12
                anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                Text {
                    text: (App.langV, App.l("nav.settings"))
                    font.pixelSize: 14
                    color: root.currentIndex === root.settingsIndex ? Theme.textPrimary : Theme.textSecondary
                }
            }
            onClicked: { root.currentIndex = root.settingsIndex; root.pageSelected(root.settingsIndex) }
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            bottomPadding: 12
            text: "v" + App.version()
            font.pixelSize: 11
            color: Theme.textDim
        }
    }
}
