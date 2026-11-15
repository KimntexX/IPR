import QtQuick
import QtQuick.Controls.Basic as Basic

// The interface's thin vertical scroll bar, for the lists inside a panel.
Basic.ScrollBar {
    id: bar
    objectName: "panelScrollBar"
    policy: Basic.ScrollBar.AsNeeded
    orientation: Qt.Vertical
    width: 6
    padding: 0
    hoverEnabled: true

    contentItem: Rectangle {
        implicitWidth: 6
        radius: width / 2
        color: "#8c9caf"
        opacity: bar.pressed || bar.hovered ? 0.58 : 0.24

        Behavior on opacity {
            NumberAnimation { duration: 140 }
        }
    }

    background: Item { }
}
