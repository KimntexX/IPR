import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: module
    required property var histogram
    property var imageDetails: ({})
    property real exposureEV: 0
    property color textColor: "#d7dfeb"
    property color panelColor: "#171b21"
    property color borderColor: "#3d4654"
    property int hoveredZone: -1
    readonly property var toneZones: [
        { name: qsTr("Noirs"), start: 0, end: 0.10, range: qsTr("0–10 % · Y < 0,01 · < −6,6 EV") },
        { name: qsTr("Ombres"), start: 0.10, end: 0.33, range: qsTr("10–33 % · Y ≈ 0,01–0,09 · −6,6 à −3,5 EV") },
        { name: qsTr("Exposition"), start: 0.33, end: 0.66, range: qsTr("33–66 % · Y ≈ 0,09–0,39 · −3,5 à −1,35 EV") },
        { name: qsTr("Hautes lumières"), start: 0.66, end: 0.90, range: qsTr("66–90 % · Y ≈ 0,39–0,79 · −1,35 à −0,34 EV") },
        { name: qsTr("Blancs"), start: 0.90, end: 1, range: qsTr("90–100 % · Y > 0,79 · > −0,34 EV") }
    ]
    readonly property real peakCount: {
        let peak = 1
        if (!histogram || histogram.length !== 4) return peak
        for (const channel of histogram)
            for (const count of channel)
                if (Number.isFinite(count)) peak = Math.max(peak, count)
        return peak
    }
    readonly property bool shadowsClipped: edgePresent(0)
    readonly property bool highlightsClipped: edgePresent(255)
    readonly property string captureDetails: {
        const parts = []
        if (imageDetails.iso) parts.push(imageDetails.iso)
        if (imageDetails.focalLength) parts.push(imageDetails.focalLength)
        if (imageDetails.exposureTime) parts.push(imageDetails.exposureTime)
        return parts.join("  ·  ")
    }
    Layout.fillWidth: true
    spacing: 5

    function requestPaint() { if (histogramCanvas) histogramCanvas.requestPaint() }
    function relativeHeight(count) {
        return Number.isFinite(count) ? Math.max(0, Math.min(1, count / peakCount)) : 0
    }
    function zoneAt(position) {
        if (!Number.isFinite(position)) return -1
        const bounded = Math.max(0, Math.min(1, position))
        for (let index = 0; index < toneZones.length; ++index)
            if (bounded < toneZones[index].end) return index
        return toneZones.length - 1
    }
    function edgePresent(bin) {
        if (!histogram || histogram.length !== 4) return false
        for (let channel = 1; channel < 4; ++channel)
            if (histogram[channel].length === 256 && histogram[channel][bin] > 0) return true
        return false
    }
    onHistogramChanged: requestPaint()
    onHoveredZoneChanged: requestPaint()

    Label {
        text: qsTr("Histogramme")
        color: module.textColor
        font.bold: true
        font.pixelSize: 13
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 126
        color: module.panelColor
        border.color: module.borderColor
        radius: 4
        Canvas {
            id: histogramCanvas
            objectName: "histogramPlot"
            anchors.fill: parent
            anchors.margins: 6
            antialiasing: true
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                const context = getContext("2d")
                context.reset()
                context.clearRect(0, 0, width, height)
                for (let zone = 0; zone < module.toneZones.length; ++zone) {
                    const band = module.toneZones[zone]
                    context.fillStyle = "rgba(255,255,255," + (0.018 * zone + (zone === module.hoveredZone ? 0.09 : 0)) + ")"
                    context.fillRect(band.start * width, 0, (band.end - band.start) * width, height)
                    if (zone > 0) {
                        context.strokeStyle = "rgba(255,255,255,0.10)"
                        context.lineWidth = 1
                        context.beginPath()
                        context.moveTo(band.start * width, 0)
                        context.lineTo(band.start * width, height)
                        context.stroke()
                    }
                }
                if (!module.histogram || module.histogram.length !== 4) return
                const colors = ["#c7d2e2", "#ef6464", "#60cc75", "#409af5"]
                for (let channel = 0; channel < 4; ++channel) {
                    const values = module.histogram[channel]
                    if (values.length !== 256) continue
                    context.beginPath()
                    context.moveTo(0, height)
                    for (let index = 0; index < values.length; ++index)
                        context.lineTo(index * width / 255,
                                       height - module.relativeHeight(values[index]) * (height - 4))
                    context.lineTo(width, height)
                    context.closePath()
                    context.fillStyle = colors[channel]
                    context.globalAlpha = channel === 0 ? 0.22 : 0.06
                    context.fill()
                    context.beginPath()
                    for (let index = 0; index < values.length; ++index) {
                        const x = index * width / 255
                        const y = height - module.relativeHeight(values[index]) * (height - 4)
                        if (index === 0) context.moveTo(x, y)
                        else context.lineTo(x, y)
                    }
                    context.strokeStyle = colors[channel]
                    context.globalAlpha = channel === 0 ? 0.6 : 0.95
                    context.lineWidth = 1.1
                    context.stroke()
                }
                context.globalAlpha = 1
            }
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.NoButton
                onPositionChanged: function(mouse) { module.hoveredZone = module.zoneAt(mouse.x / width) }
                onExited: module.hoveredZone = -1
                ToolTip.visible: containsMouse
                ToolTip.text: module.hoveredZone >= 0
                    ? module.toneZones[module.hoveredZone].range + qsTr("\nRepères sRGB indicatifs, pas les masques des curseurs.") : ""
            }
        }
        Repeater {
            model: [0, 255]
            Label {
                required property int modelData
                x: modelData === 0 ? 3 : parent.width - width - 3
                y: 2
                text: "▲"
                font.pixelSize: 10
                color: modelData === 0
                    ? (module.shadowsClipped ? "#61afef" : "#58616b")
                    : (module.highlightsClipped ? "#e6c34b" : "#58616b")
                ToolTip.visible: edgeHover.hovered
                ToolTip.text: qsTr("Un canal de l’aperçu sRGB atteint %1. Ce n’est pas une mesure d’écrêtage du capteur RAW.").arg(modelData)
                HoverHandler { id: edgeHover }
            }
        }
    }
    Label {
        objectName: "histogramZoneLabel"
        Layout.fillWidth: true
        text: module.hoveredZone >= 0 ? module.toneZones[module.hoveredZone].name
             : qsTr("Exposition %1 EV").arg(Number(module.exposureEV).toLocaleString(Qt.locale(), 'f', 2))
        color: module.textColor
        font.pixelSize: 11
        horizontalAlignment: Text.AlignHCenter
    }
    Label {
        Layout.fillWidth: true
        visible: module.captureDetails.length > 0
        text: module.captureDetails
        color: module.textColor
        font.pixelSize: 11
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
    }
}
