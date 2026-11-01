import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.ScrollView {
    id: control
    clip: true
    contentWidth: availableWidth
    rightPadding: 10
    Basic.ScrollBar.horizontal.policy: Basic.ScrollBar.AlwaysOff
    Basic.ScrollBar.vertical: Basic.ScrollBar {
        id: bar
        objectName: "preferenceScrollBar"
        policy: Basic.ScrollBar.AsNeeded
        width: 6
        padding: 0
        hoverEnabled: true
        contentItem: Rectangle {
            implicitWidth: 6
            radius: width / 2
            color: control.palette.text
            opacity: bar.pressed || bar.hovered ? 0.58 : 0.24
            Behavior on opacity { NumberAnimation { duration: 140 } }
        }
        background: Item { }
    }
}
