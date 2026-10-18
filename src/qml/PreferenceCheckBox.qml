import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.CheckBox {
    id: control
    property color textColor: palette.windowText
    implicitHeight: Math.max(28, contentItem.implicitHeight + 8)
    spacing: 8
    padding: 4
    hoverEnabled: true
    indicator: Rectangle {
        x: control.leftPadding
        y: (control.height - height) / 2
        width: 16
        height: 16
        radius: 3
        color: control.checked ? control.palette.highlight : control.palette.base
        border.color: control.activeFocus || control.hovered ? control.palette.text : control.palette.mid
        opacity: control.enabled ? 1 : 0.45
        Text {
            anchors.centerIn: parent
            text: "✓"
            visible: control.checked
            color: control.palette.text
            font.pixelSize: 13
        }
    }
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.textColor
        opacity: control.enabled ? 1 : 0.45
        leftPadding: control.indicator.width + control.spacing
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.WordWrap
    }
}
