import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LlamaCode 1.0

Rectangle {
    id: root
    property int workflowCount: workflowRepeater.count
    signal workflowInstalled(string taskId)
    color: Theme.surfaceBg
    implicitHeight: 48

    function installWorkflow(workflowId) {
        const taskId = App.installEngineeringWorkflow(workflowId)
        if (taskId) workflowInstalled(taskId)
        return taskId
    }

    RowLayout {
        anchors { fill: parent; leftMargin: 24; rightMargin: 16 }
        spacing: 8
        Text {
            text: "Workflows:"
            color: Theme.textSecondary
            font { pixelSize: 12; bold: true }
        }
        Repeater {
            id: workflowRepeater
            model: App.engineeringWorkflows()
            delegate: LcButton {
                text: modelData.name
                secondary: true
                ToolTip.visible: hovered
                ToolTip.text: modelData.description
                onClicked: root.installWorkflow(modelData.id)
            }
        }
        Item { Layout.fillWidth: true }
    }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.divider }
}
