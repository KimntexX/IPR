import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import QtQuick.Window

ColumnLayout {
    id: root

    required property var editor
    property bool showTitle: true
    property int zone: 0
    property int hue: 0
    readonly property var zoneNames: [qsTr("Ombres"), qsTr("Tons moyens"),
                                      qsTr("Hautes lumières")]
    readonly property var zoneShortNames: [qsTr("Ombres"), qsTr("Moyens"), qsTr("Hautes")]
    // Hue scale at full intensity, from the engine (the zone's measured effect).
    property var hueTrack: []
    // Bumped on every grading change, so the zone swatches follow the engine.
    property int revision: 0

    // Wheel geometry: the hue cursor turns just outside the wheel.
    readonly property real areaSize: Math.max(120, Math.min(250, root.width))
    readonly property real wheelRadius: areaSize / 2 - 15
    readonly property real fullIntensityRadius: wheelRadius * 0.9

    function refreshHueTrack() {
        const track = []
        for (let h = 0; h <= 360; h += 30)
            track.push(editor.colorGradingDisplayColor(zone, h % 360, 100))
        hueTrack = track
    }

    // revision: passed by the bindings so that they follow every grading change.
    function zoneSwatch(index, revision) {
        const saturation = editor.colorGradingValue(index, 1)
        return saturation > 0 ? editor.colorGradingDisplayColor(index, editor.colorGradingValue(index, 0), saturation)
                              : "transparent"
    }

    function reload() {
        blendingControl.value = editor.colorGradingBlending()
        balanceControl.value = editor.colorGradingBalance()
        root.refreshHueTrack()
        root.hue = Math.round(editor.colorGradingValue(zone, 0))
        hueControl.value = root.hue
        intensityControl.value = editor.colorGradingValue(zone, 1)
        root.revision++
        wheelMarks.requestPaint()
    }

    onVisibleChanged: if (visible) wheelMarks.requestPaint()
    onAreaSizeChanged: wheelMarks.requestPaint()

    function selectZone(index) {
        root.zone = index
        root.reload()
    }

    function commit() {
        editor.setColorGrading(zone, root.hue, Math.round(intensityControl.value))
        root.revision++
        wheelMarks.requestPaint()
    }

    function resetZone() {
        root.hue = 0
        hueControl.value = 0
        intensityControl.value = 0
        root.commit()
    }

    // Position on the wheel, in pixels from its centre, of a hue and an intensity.
    function wheelPoint(hueDegrees, intensity, radius) {
        const angle = hueDegrees * Math.PI / 180
        const r = radius === undefined ? intensity / 100 * fullIntensityRadius : radius
        return { x: areaSize / 2 + r * Math.sin(angle), y: areaSize / 2 - r * Math.cos(angle) }
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
        spacing: 4

        Repeater {
            model: root.zoneNames.length

            Basic.ToolButton {
                id: zoneButton

                required property int index
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                enabled: root.editor.hasImage
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.name: root.zoneNames[index]
                ToolTip.visible: hovered
                ToolTip.text: root.zoneNames[index]
                onClicked: root.selectZone(index)

                contentItem: ColumnLayout {
                    spacing: 3
                    opacity: zoneButton.enabled ? 1 : 0.4

                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        width: 10
                        height: 10
                        radius: 5
                        color: root.zoneSwatch(zoneButton.index, root.revision)
                        border.width: 1
                        border.color: "#5b6878"
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: root.zoneShortNames[zoneButton.index]
                        color: zoneButton.palette.buttonText
                        font.pixelSize: 11
                    }
                }

                background: Rectangle {
                    radius: 6
                    color: root.zone === zoneButton.index || zoneButton.hovered
                           ? zoneButton.palette.mid : zoneButton.palette.button
                    border.width: 1
                    border.color: root.zone === zoneButton.index ? zoneButton.palette.text : zoneButton.palette.mid
                }
            }
        }
    }

    // Takes the panel's width, never imposes one: the wheel follows it.
    Item {
        Layout.fillWidth: true
        Layout.preferredWidth: 120
        Layout.preferredHeight: root.areaSize

        Item {
            id: wheelArea
            objectName: "gradingWheel"
            anchors.horizontalCenter: parent.horizontalCenter
            width: root.areaSize
            height: root.areaSize

            // The wheel itself, computed by the engine once per zone and size:
            // each pixel shows the colour its hue and intensity give.
            Image {
                anchors.centerIn: parent
                width: 2 * root.wheelRadius
                height: width
                readonly property int pixels: Math.round(width * Screen.devicePixelRatio)
                source: pixels > 0 ? "image://gradingwheel/" + root.zone + "/" + pixels : ""
                sourceSize: Qt.size(pixels, pixels)
                smooth: true
            }
            Rectangle {
                anchors.centerIn: parent
                width: 2 * root.wheelRadius + 2
                height: width
                radius: width / 2
                color: "transparent"
                border.width: 1
                border.color: "#4b596b"
            }

            // Selection marks, repainted while dragging: the other zones in
            // outline, the selected point filled with the colour it applies, and
            // the hue cursor outside the wheel in the hue's full colour.
            Canvas {
                id: wheelMarks
                anchors.fill: parent
                antialiasing: true
                onPaint: {
                    const context = getContext("2d")
                    context.reset()
                    const centre = width / 2
                    context.strokeStyle = "rgba(27,31,38,0.55)"
                    context.lineWidth = 1
                    context.beginPath()
                    context.moveTo(centre - 6, centre); context.lineTo(centre + 6, centre)
                    context.moveTo(centre, centre - 6); context.lineTo(centre, centre + 6)
                    context.stroke()

                    for (let other = 0; other < root.zoneNames.length; ++other) {
                        const saturation = root.editor.colorGradingValue(other, 1)
                        if (other === root.zone || saturation === 0)
                            continue
                        const p = root.wheelPoint(root.editor.colorGradingValue(other, 0), saturation)
                        context.strokeStyle = "rgba(255,255,255,0.7)"
                        context.lineWidth = 1.5
                        context.beginPath()
                        context.arc(p.x, p.y, 4.5, 0, 2 * Math.PI)
                        context.stroke()
                    }

                    const intensity = Math.max(0, Math.min(100, intensityControl.value))
                    const marker = root.wheelPoint(root.hue, intensity)
                    context.fillStyle = String(root.editor.colorGradingDisplayColor(root.zone, root.hue, Math.round(intensity)))
                    context.strokeStyle = "#ffffff"
                    context.lineWidth = 2
                    context.beginPath()
                    context.arc(marker.x, marker.y, 8, 0, 2 * Math.PI)
                    context.fill()
                    context.stroke()
                    context.strokeStyle = "rgba(0,0,0,0.6)"
                    context.lineWidth = 1
                    context.beginPath()
                    context.arc(marker.x, marker.y, 9.5, 0, 2 * Math.PI)
                    context.stroke()

                    const cursor = root.wheelPoint(root.hue, 0, root.wheelRadius + 9)
                    context.save()
                    context.translate(cursor.x, cursor.y)
                    context.rotate(root.hue * Math.PI / 180)
                    context.fillStyle = String(root.editor.colorGradingDisplayColor(root.zone, root.hue, 100))
                    context.strokeStyle = "#ffffff"
                    context.lineWidth = 2
                    context.beginPath()
                    context.roundedRect(-4, -8, 8, 16, 3, 3)
                    context.fill()
                    context.stroke()
                    context.restore()
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
                property bool hueOnly: false
                property point last
                property point virtualPoint

                function apply(point) {
                    const dx = point.x - root.areaSize / 2
                    const dy = point.y - root.areaSize / 2
                    const distance = Math.sqrt(dx * dx + dy * dy)
                    if (distance > 3)
                        root.hue = Math.round((Math.atan2(dx, -dy) * 180 / Math.PI + 360) % 360)
                    hueControl.value = root.hue
                    if (!hueOnly)
                        intensityControl.value = Math.round(Math.min(100, distance / root.fullIntensityRadius * 100))
                    root.commit()
                }

                onPressed: function(mouse) {
                    const dx = mouse.x - root.areaSize / 2
                    const dy = mouse.y - root.areaSize / 2
                    const distance = Math.sqrt(dx * dx + dy * dy)
                    if (distance > root.wheelRadius + 15) {
                        mouse.accepted = false
                        return
                    }
                    // The cursor outside the wheel, or a press around the wheel,
                    // turns the hue only; inside, hue and intensity follow the pointer.
                    const cursor = root.wheelPoint(root.hue, 0, root.wheelRadius + 9)
                    hueOnly = distance > root.wheelRadius ||
                              Math.hypot(mouse.x - cursor.x, mouse.y - cursor.y) < 12
                    last = Qt.point(mouse.x, mouse.y)
                    virtualPoint = last
                    apply(virtualPoint)
                }
                onPositionChanged: function(mouse) {
                    if (!pressed)
                        return
                    // Shift: fine adjustment, a quarter of the pointer's movement.
                    const k = (mouse.modifiers & Qt.ShiftModifier) ? 0.25 : 1
                    virtualPoint = Qt.point(virtualPoint.x + (mouse.x - last.x) * k,
                                            virtualPoint.y + (mouse.y - last.y) * k)
                    last = Qt.point(mouse.x, mouse.y)
                    apply(virtualPoint)
                }
                onDoubleClicked: root.resetZone()
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("%1 · %2° · %3").arg(root.zoneNames[root.zone]).arg(root.hue)
                  .arg(Math.round(intensityControl.value))
            color: palette.windowText
            font.pixelSize: 12
        }

        PreferenceButton {
            id: resetZoneButton
            Layout.preferredWidth: 21
            Layout.preferredHeight: 21
            text: "↺"
            font.pixelSize: 14
            padding: 0
            hoverEnabled: true
            enabled: root.editor.hasImage
            Accessible.name: qsTr("Réinitialiser la zone")
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Réinitialiser la zone (double-clic sur la roue)")
            onClicked: root.resetZone()

            background: Rectangle {
                radius: 3
                color: resetZoneButton.hovered ? resetZoneButton.palette.mid : "transparent"
            }
        }
    }

    AdjustmentControl {
        id: hueControl
        trackColors: root.hueTrack
        trackThickness: 6
        Layout.fillWidth: true
        label: qsTr("Teinte")
        valueText: Math.round(value) + "°"
        minimumValue: 0
        maximumValue: 359
        adjustmentEnabled: root.editor.hasImage
        onValueEdited: {
            if (Math.round(value) === root.hue)
                return
            root.hue = Math.round(value)
            root.commit()
        }
    }

    AdjustmentControl {
        id: intensityControl
        trackColors: [root.editor.colorGradingDisplayColor(root.zone, root.hue, 0),
                      root.editor.colorGradingDisplayColor(root.zone, root.hue, 100)]
        trackThickness: 6
        Layout.fillWidth: true
        label: qsTr("Intensité")
        valueText: Math.round(value)
        minimumValue: 0
        maximumValue: 100
        adjustmentEnabled: root.editor.hasImage
        onValueChanged: wheelMarks.requestPaint()
        onValueEdited: root.commit()
    }

    // Shared by the three zones, as in Lightroom.
    AdjustmentControl {
        id: blendingControl
        Layout.fillWidth: true
        label: qsTr("Fusion")
        valueText: Math.round(value)
        minimumValue: 0
        maximumValue: 100
        resetValue: 50
        adjustmentEnabled: root.editor.hasImage
        onValueEdited: root.editor.setColorGradingBlending(Math.round(value))
    }

    AdjustmentControl {
        id: balanceControl
        Layout.fillWidth: true
        label: qsTr("Balance")
        valueText: Math.round(value)
        minimumValue: -100
        maximumValue: 100
        adjustmentEnabled: root.editor.hasImage
        onValueEdited: root.editor.setColorGradingBalance(Math.round(value))
    }

    Component.onCompleted: reload()
}
