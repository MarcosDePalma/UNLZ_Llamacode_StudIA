import QtQuick

Rectangle {
    property string title: ""
    property string subtitle: ""
    property string actionLabel: ""
    property string action2Label: ""
    signal actionClicked()
    signal action2Clicked()
    height: 56
    color: Theme.baseBg
}
