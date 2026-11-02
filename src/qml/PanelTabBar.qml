import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic

// The right panel's tabs: icons across its width, each one's name on hover (mock-up A
// chosen by the user on 1 November 2026). A tab appears with its panel's delivery.
Row {
    id: root
    objectName: "panelTabBar"

    property int currentIndex: 0
    property color textColor: palette.windowText
    readonly property var tabs: [
        { name: qsTr("Réglages"), icon: "settings", key: "panelTabSettings" },
        { name: qsTr("Géométrie"), icon: "geometry", key: "panelTabGeometry" }
    ]
    spacing: 4

    Repeater {
        model: root.tabs
        Basic.AbstractButton {
            id: tab
            required property var modelData
            required property int index
            objectName: modelData.key
            width: (root.width - root.spacing * (root.tabs.length - 1)) / root.tabs.length
            height: 32
            hoverEnabled: true
            checked: root.currentIndex === index
            onClicked: root.currentIndex = index
            ToolTip.visible: hovered
            ToolTip.delay: 400
            ToolTip.text: modelData.name
            Accessible.name: modelData.name
            background: Rectangle {
                radius: 4
                color: tab.checked ? tab.palette.highlight : tab.hovered ? tab.palette.mid : tab.palette.button
                border.width: tab.activeFocus ? 2 : 1
                border.color: tab.activeFocus ? root.textColor : tab.palette.mid
            }
            contentItem: Canvas {
                id: icon
                readonly property color stroke: root.textColor
                onStrokeChanged: requestPaint()
                onPaint: {
                    const c = getContext("2d")
                    const middleX = width / 2, middleY = height / 2
                    c.reset()
                    c.strokeStyle = stroke
                    c.fillStyle = stroke
                    c.lineWidth = 1.5
                    c.lineCap = "round"
                    if (tab.modelData.icon === "settings") {
                        // Three sliders.
                        for (const [row, knob] of [[-5, 3], [0, -4], [5, 1]]) {
                            c.beginPath()
                            c.moveTo(middleX - 8, middleY + row)
                            c.lineTo(middleX + 8, middleY + row)
                            c.stroke()
                            c.beginPath()
                            c.arc(middleX + knob, middleY + row, 2.2, 0, 2 * Math.PI)
                            c.fill()
                        }
                    } else {
                        // Crop marks.
                        c.beginPath()
                        c.moveTo(middleX - 5, middleY - 9)
                        c.lineTo(middleX - 5, middleY + 5)
                        c.lineTo(middleX + 9, middleY + 5)
                        c.moveTo(middleX + 5, middleY + 9)
                        c.lineTo(middleX + 5, middleY - 5)
                        c.lineTo(middleX - 9, middleY - 5)
                        c.stroke()
                    }
                }
            }
        }
    }
}
