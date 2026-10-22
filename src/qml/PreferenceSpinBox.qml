import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.SpinBox {
    id: control
    implicitWidth: 116
    implicitHeight: 30
    leftPadding: 28
    rightPadding: 28
    topPadding: 3
    bottomPadding: 3
    editable: true
    contentItem: TextInput {
        z: 2
        text: control.displayText
        font: control.font
        color: control.palette.text
        opacity: control.enabled ? 1 : 0.45
        selectionColor: control.palette.highlight
        selectedTextColor: control.palette.highlightedText
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        selectByMouse: true
        clip: true
    }
    up.indicator: Rectangle {
        x: control.width - width
        width: 26
        height: control.height
        radius: 4
        color: control.up.pressed || control.up.hovered ? control.palette.mid : "transparent"
        Text {
            anchors.centerIn: parent
            text: "+"
            color: control.palette.text
            opacity: control.enabled && control.value < control.to ? 1 : 0.3
            font.pixelSize: 16
        }
    }
    down.indicator: Rectangle {
        width: 26
        height: control.height
        radius: 4
        color: control.down.pressed || control.down.hovered ? control.palette.mid : "transparent"
        Text {
            anchors.centerIn: parent
            text: "−"
            color: control.palette.text
            opacity: control.enabled && control.value > control.from ? 1 : 0.3
            font.pixelSize: 16
        }
    }
    background: Rectangle {
        color: control.palette.base
        radius: 4
        border.color: control.activeFocus ? control.palette.text : control.palette.mid
        border.width: control.activeFocus || control.contentItem.activeFocus ? 2 : 1
    }
}
