import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.Button {
    id: control
    property bool errorShown: false
    readonly property color errorColor: "#e55353"
    implicitHeight: 30
    padding: 7
    hoverEnabled: true
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.palette.buttonText
        opacity: control.enabled ? 1 : 0.6
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 4
        color: control.down || control.hovered ? control.palette.mid : control.palette.button
        border.color: control.errorShown ? control.errorColor : control.activeFocus ? control.palette.text : control.palette.mid
        border.width: control.errorShown || control.activeFocus ? 2 : 1
        opacity: control.enabled ? 1 : 0.6
    }
}
