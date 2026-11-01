import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.ComboBox {
    id: control
    implicitHeight: 30
    leftPadding: 8
    rightPadding: 26
    contentItem: Text {
        text: control.displayText
        font: control.font
        color: control.palette.text
        opacity: control.enabled ? 1 : 0.45
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Text {
        x: control.width - width - 8
        y: (control.height - height) / 2
        text: "▾"
        color: control.palette.text
    }
    background: Rectangle {
        color: control.palette.base
        radius: 4
        border.color: control.activeFocus ? control.palette.text : control.palette.mid
        border.width: control.activeFocus ? 2 : 1
    }
    delegate: Basic.ItemDelegate {
        id: entry
        required property int index
        width: control.width
        height: 30
        text: control.textAt(index)
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: entry.text
            font: control.font
            color: control.palette.text
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: entry.highlighted ? control.palette.mid : control.palette.base
        }
    }
}
