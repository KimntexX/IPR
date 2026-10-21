import QtQuick
import QtQuick.Controls

Item {
    id: root
    property int orientation: Qt.Horizontal
    property real currentSize: 0
    property real minimumSize: 0
    property real maximumSize: 1000
    property real defaultSize: minimumSize
    property real direction: 1
    property color handleColor: "#8c9caf"
    property string description: ""
    property bool writable: true
    readonly property bool horizontal: orientation === Qt.Horizontal
    signal sizeEdited(real size)
    implicitWidth: horizontal ? 8 : 60
    implicitHeight: horizontal ? 60 : 8
    activeFocusOnTab: true
    Accessible.role: Accessible.Separator
    Accessible.name: description

    function resizeTo(size) {
        if (writable && isFinite(size))
            sizeEdited(Math.round(Math.max(minimumSize, Math.min(maximumSize, size))))
    }
    Keys.onPressed: (event) => {
        let delta = 0
        if (horizontal && event.key === Qt.Key_Left) delta = -10
        else if (horizontal && event.key === Qt.Key_Right) delta = 10
        else if (!horizontal && event.key === Qt.Key_Up) delta = -10
        else if (!horizontal && event.key === Qt.Key_Down) delta = 10
        if (delta !== 0) {
            resizeTo(currentSize + direction * delta)
            event.accepted = true
        }
    }
    Rectangle {
        anchors.centerIn: parent
        width: root.horizontal ? 2 : 28
        height: root.horizontal ? 28 : 2
        radius: 1
        color: root.handleColor
        opacity: pointer.containsMouse || pointer.pressed || root.activeFocus ? 0.9 : 0.35
    }
    MouseArea {
        id: pointer
        anchors.fill: parent
        enabled: root.writable
        hoverEnabled: true
        cursorShape: root.horizontal ? Qt.SplitHCursor : Qt.SplitVCursor
        property real initialCoordinate: 0
        property real initialSize: 0
        onPressed: (mouse) => {
            root.forceActiveFocus()
            const point = mapToItem(null, mouse.x, mouse.y)
            initialCoordinate = root.horizontal ? point.x : point.y
            initialSize = root.currentSize
        }
        onPositionChanged: (mouse) => {
            if (!pressed) return
            const point = mapToItem(null, mouse.x, mouse.y)
            root.resizeTo(initialSize + root.direction *
                          ((root.horizontal ? point.x : point.y) - initialCoordinate))
        }
        onDoubleClicked: root.resizeTo(root.defaultSize)
    }
    ToolTip.visible: pointer.containsMouse && !pointer.pressed
    ToolTip.text: description + qsTr(" — glisser ou flèches ; double-clic pour réinitialiser")
}
