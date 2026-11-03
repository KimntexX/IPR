import QtQuick
import QtQuick.Controls

// The right panel's tabs, as on mock-up A chosen by the user on 1 November 2026: joined
// to the top of the panel, the current one open into it, each one's name on hover.
// A tab appears with its panel's delivery. Icons are drawn in a 24-unit square.
Row {
    id: root
    objectName: "panelTabBar"

    property int currentIndex: 0
    readonly property string currentKey: tabs[currentIndex].key
    property color textColor: palette.windowText
    property color secondaryTextColor: palette.windowText
    property color panelColor: palette.window
    property color lineColor: palette.mid
    // The panel's own corners, which the outer tabs follow.
    property real outerRadius: 8
    readonly property var tabs: [
        {
            name: qsTr("Réglages"), key: "panelTabSettings",
            lines: [[4, 6, 14, 6], [18, 6, 20, 6], [4, 12, 8, 12], [12, 12, 20, 12], [4, 18, 16, 18]],
            rings: [[16, 6], [10, 12], [18, 18]]
        },
        {
            name: qsTr("Géométrie"), key: "panelTabGeometry",
            lines: [[6, 2, 6, 18, 22, 18], [2, 6, 18, 6, 18, 22]],
            rings: []
        },
        {
            name: qsTr("Masques"), key: "panelTabMasks",
            lines: [[12, 3, 21, 8, 12, 13, 3, 8, 12, 3], [3, 13, 12, 18, 21, 13]],
            rings: []
        },
        {
            name: qsTr("Assembler"), key: "panelTabAssemble",
            lines: [[7, 3, 21, 3, 21, 14, 7, 14, 7, 3], [4, 7, 4, 18, 18, 18], [1, 10, 1, 21, 15, 21]],
            rings: []
        }
    ]
    spacing: 0

    Repeater {
        model: root.tabs
        AbstractButton {
            id: tab
            required property var modelData
            required property int index
            readonly property bool first: index === 0
            readonly property bool last: index === root.tabs.length - 1
            objectName: modelData.key
            width: root.width / root.tabs.length
            height: 48
            hoverEnabled: true
            checked: root.currentIndex === index
            onClicked: root.currentIndex = index
            ToolTip.visible: hovered
            ToolTip.delay: 400
            ToolTip.text: modelData.name
            Accessible.name: modelData.name
            Accessible.role: Accessible.PageTab
            background: Canvas {
                objectName: "panelTabShape"
                readonly property color fill: tab.checked ? root.panelColor
                                                          : Qt.darker(root.panelColor, tab.hovered ? 1.12 : 1.3)
                readonly property color line: tab.visualFocus ? root.textColor : root.lineColor
                onFillChanged: requestPaint()
                onLineChanged: requestPaint()
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    const c = getContext("2d")
                    const edge = 0.5
                    const right = width - edge
                    const leftRadius = tab.first ? root.outerRadius : 4
                    const rightRadius = tab.last ? root.outerRadius : 4
                    c.reset()
                    c.beginPath()
                    c.moveTo(edge, height)
                    c.lineTo(edge, leftRadius)
                    c.arcTo(edge, edge, leftRadius, edge, leftRadius - edge)
                    c.lineTo(right - rightRadius, edge)
                    c.arcTo(right, edge, right, rightRadius, rightRadius - edge)
                    c.lineTo(right, height)
                    c.closePath()
                    c.fillStyle = fill
                    c.fill()
                    c.strokeStyle = line
                    c.lineWidth = 1
                    c.beginPath()
                    // The open side and the shared sides stay free of lines.
                    c.moveTo(edge, leftRadius)
                    c.arcTo(edge, edge, leftRadius, edge, leftRadius - edge)
                    c.lineTo(right - rightRadius, edge)
                    c.arcTo(right, edge, right, rightRadius, rightRadius - edge)
                    if (tab.checked || tab.first) {
                        c.moveTo(edge, leftRadius)
                        c.lineTo(edge, height)
                    }
                    if (tab.checked || tab.last) {
                        c.moveTo(right, rightRadius)
                        c.lineTo(right, height)
                    }
                    if (!tab.checked) {
                        c.moveTo(0, height - edge)
                        c.lineTo(width, height - edge)
                    }
                    c.stroke()
                }
            }
            contentItem: Canvas {
                readonly property color stroke: tab.checked ? root.textColor : root.secondaryTextColor
                onStrokeChanged: requestPaint()
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    const c = getContext("2d")
                    c.reset()
                    c.translate(Math.round(width / 2) - 12, Math.round(height / 2) - 12)
                    c.strokeStyle = stroke
                    c.lineWidth = 1.6
                    c.lineCap = "round"
                    c.lineJoin = "round"
                    for (const points of tab.modelData.lines) {
                        c.beginPath()
                        c.moveTo(points[0], points[1])
                        for (let at = 2; at < points.length; at += 2)
                            c.lineTo(points[at], points[at + 1])
                        c.stroke()
                    }
                    for (const ring of tab.modelData.rings) {
                        c.beginPath()
                        c.arc(ring[0], ring[1], 2, 0, 2 * Math.PI)
                        c.stroke()
                    }
                }
            }
        }
    }
}
