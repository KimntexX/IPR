import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.Button {
    id: control
    implicitHeight: 30
    padding: 7
    hoverEnabled: true
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.palette.buttonText
        opacity: control.enabled ? 1 : 0.45
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 4
        color: control.down || control.hovered ? control.palette.mid : control.palette.button
        border.color: control.activeFocus ? control.palette.text : control.palette.mid
        opacity: control.enabled ? 1 : 0.6
    }
}
