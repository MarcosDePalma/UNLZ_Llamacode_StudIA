import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0

Rectangle {
    id: root
    width: 200
    color: Theme.navBg

    property int currentIndex: 0
    signal pageSelected(int index)

    readonly property var pages: [
        { key: "nav.launch",   icon: "🚀",  serverOnly: false },
        { key: "nav.profiles", icon: "📋",  serverOnly: false },
        { key: "nav.models",   icon: "📦",  serverOnly: false },
        { key: "nav.binaries", icon: "⚙",   serverOnly: false },
        { key: "nav.chat",      icon: "💬",  serverOnly: true  },
        { key: "agent.title",   icon: "🤖",  serverOnly: true  },
        { key: "nav.research",  icon: "🔎",  serverOnly: true  },
        { key: "nav.tasks",     icon: "🗒",  serverOnly: false },
        { key: "nav.benchmark", icon: "📊",  serverOnly: false },
        { key: "nav.charla",    icon: "🎙",  serverOnly: true  },
        { key: "nav.studia",    icon: "🎓",  serverOnly: false },
    ]

    // Ajustes va siempre al final, después de las páginas de la lista.
    readonly property int settingsIndex: pages.length

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
                enabled: !modelData.serverOnly || App.serverRunning
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
                        text: (App.langV, App.l(modelData.key))
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
