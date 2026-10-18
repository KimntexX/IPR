import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts

ColumnLayout {
    id: root

    required property var editor
    property bool showTitle: true
    property int zone: 0
    property int hue: 0
    readonly property var zoneNames: [qsTr("Ombres"), qsTr("Tons moyens"),
                                      qsTr("Hautes lumières")]
    readonly property var zoneIcons: ["☾", "◐", "☀"]

    function reload() {
        root.hue = Math.round(editor.colorGradingValue(zone, 0))
        intensityControl.value = editor.colorGradingValue(zone, 1)
        gradingWheel.requestPaint()
    }

    onVisibleChanged: if (visible) gradingWheel.requestPaint()

    function selectZone(index) {
        root.zone = index
        root.reload()
    }

    function commit() {
        editor.setColorGrading(zone, root.hue, Math.round(intensityControl.value))
    }

    function updateFromWheel(mouseX, mouseY, clampOutside) {
        const centerX = gradingWheel.width / 2
        const centerY = gradingWheel.height / 2
        const dx = mouseX - centerX
        const dy = mouseY - centerY
        const distance = Math.sqrt(dx * dx + dy * dy)
        const outerRadius = Math.min(gradingWheel.width, gradingWheel.height) / 2 - 8
        const innerRadius = 22
        // A press must start on the wheel.  During a drag, however, keep the
        // pointer interaction alive outside its edge and project it back to
        // the outer ring: the selected intensity then remains at 100 instead
        // of the gesture appearing to stop.
        if (distance > outerRadius && !clampOutside)
            return
        const effectiveDistance = Math.min(distance, outerRadius)

        if (effectiveDistance > innerRadius) {
            const angle = Math.atan2(dy, dx)
            root.hue = Math.round((angle * 180 / Math.PI + 450) % 360)
        }
        intensityControl.value = Math.round(Math.max(0, Math.min(100,
            (effectiveDistance - innerRadius) / (outerRadius - innerRadius) * 100)))
        root.commit()
        gradingWheel.requestPaint()
    }

    Layout.fillWidth: true
    spacing: 7

    Label {
        visible: root.showTitle
        text: qsTr("Color grading")
        color: palette.windowText
        font.bold: true
        font.pixelSize: 14
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        Repeater {
            model: root.zoneNames.length

            Basic.ToolButton {
                id: zoneButton

                required property int index
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                enabled: root.editor.hasImage
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                ToolTip.visible: hovered
                ToolTip.text: root.zoneNames[index]
                onClicked: root.selectZone(index)

                contentItem: Text {
                    text: root.zoneIcons[zoneButton.index]
                    color: zoneButton.palette.buttonText
                    opacity: zoneButton.enabled ? 1 : 0.4
                    font.pixelSize: 18
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    radius: 4
                    color: root.zone === zoneButton.index || zoneButton.hovered
                           ? zoneButton.palette.mid : zoneButton.palette.button
                    border.width: 1
                    border.color: root.zone === zoneButton.index ? zoneButton.palette.text : zoneButton.palette.mid
                }
            }
        }
    }

    Rectangle {
        Layout.alignment: Qt.AlignHCenter
        Layout.preferredWidth: 174
        Layout.preferredHeight: 174
        radius: width / 2
        color: "#171b21"
        border.width: 1
        border.color: "#4b596b"
        clip: true

        Canvas {
            id: gradingWheel
            anchors.fill: parent
            antialiasing: true

            onPaint: {
                const context = getContext("2d")
                context.reset()
                context.clearRect(0, 0, width, height)

                const centerX = width / 2
                const centerY = height / 2
                const outerRadius = Math.min(width, height) / 2 - 7
                const innerRadius = 22
                const sectors = 72
                const rings = 7

                for (let ring = 0; ring < rings; ++ring) {
                    const radius0 = innerRadius + ring * (outerRadius - innerRadius) / rings
                    const radius1 = innerRadius + (ring + 1) * (outerRadius - innerRadius) / rings + 0.7
                    const saturation = 0.18 + (ring + 1) / rings * 0.72
                    const lightness = 0.88 - ring / rings * 0.40
                    for (let sector = 0; sector < sectors; ++sector) {
                        const start = sector / sectors * 2 * Math.PI - Math.PI / 2
                        const end = (sector + 1) / sectors * 2 * Math.PI - Math.PI / 2
                        context.fillStyle = Qt.hsla(sector / sectors, saturation, lightness, 1)
                        context.beginPath()
                        context.moveTo(centerX + Math.cos(start) * radius0,
                                       centerY + Math.sin(start) * radius0)
                        context.arc(centerX, centerY, radius1, start, end)
                        context.lineTo(centerX + Math.cos(end) * radius0,
                                       centerY + Math.sin(end) * radius0)
                        context.arc(centerX, centerY, radius0, end, start, true)
                        context.closePath()
                        context.fill()
                    }
                }

                context.fillStyle = "#e7edf4"
                context.beginPath()
                context.arc(centerX, centerY, innerRadius - 1, 0, 2 * Math.PI)
                context.fill()

                const selectionRadius = innerRadius +
                        Math.max(0, Math.min(100, intensityControl.value)) / 100 *
                        (outerRadius - innerRadius)
                const selectionAngle = (root.hue - 90) * Math.PI / 180
                const selectionX = centerX + Math.cos(selectionAngle) * selectionRadius
                const selectionY = centerY + Math.sin(selectionAngle) * selectionRadius
                context.strokeStyle = "#ffffff"
                context.lineWidth = 2
                context.beginPath()
                context.arc(selectionX, selectionY, 5, 0, 2 * Math.PI)
                context.stroke()
                context.strokeStyle = "#1a2028"
                context.lineWidth = 1
                context.beginPath()
                context.arc(selectionX, selectionY, 6.5, 0, 2 * Math.PI)
                context.stroke()
            }
        }

        MouseArea {
            id: wheelPointerArea
            anchors.fill: parent
            enabled: root.editor.hasImage
            // The enclosing settings pane is a ScrollView.  Without this,
            // Flickable may take ownership of a drag after its movement
            // threshold, so the wheel receives no further position changes
            // until the user presses it again.
            preventStealing: true
            onPressed: function(mouse) {
                mouse.accepted = true
                root.updateFromWheel(mouse.x, mouse.y, false)
            }
            onPositionChanged: function(mouse) {
                if (pressed)
                    root.updateFromWheel(mouse.x, mouse.y, true)
            }
        }
    }

    Label {
        Layout.alignment: Qt.AlignHCenter
        text: qsTr("%1 · %2°").arg(root.zoneNames[root.zone]).arg(root.hue)
        color: palette.windowText
        font.pixelSize: 12
    }

    AdjustmentControl {
        id: intensityControl
        trackColors: ["#8b8e94", Qt.hsla(root.hue / 360, 0.72, 0.55, 1)]
        Layout.fillWidth: true
        label: qsTr("Intensité")
        valueText: Math.round(value)
        minimumValue: 0
        maximumValue: 100
        adjustmentEnabled: root.editor.hasImage
        onValueChanged: gradingWheel.requestPaint()
        onValueEdited: root.commit()
    }

    Component.onCompleted: reload()
}
