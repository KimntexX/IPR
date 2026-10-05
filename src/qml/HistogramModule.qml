import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: module

    required property var histogram
    property var imageDetails: ({})
    property color textColor: "#d7dfeb"
    property color panelColor: "#171b21"
    property color borderColor: "#3d4654"

    Layout.fillWidth: true
    spacing: 5

    function requestPaint() {
        histogramCanvas.requestPaint()
    }

    readonly property string captureDetails: {
        const parts = []
        if (imageDetails.iso)
            parts.push(imageDetails.iso)
        if (imageDetails.focalLength)
            parts.push(imageDetails.focalLength)
        if (imageDetails.exposureTime)
            parts.push(imageDetails.exposureTime)
        return parts.join("  ·  ")
    }

    Label {
        text: qsTr("Histogramme")
        color: module.textColor
        font.bold: true
        font.pixelSize: 13
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 92
        color: module.panelColor
        border.color: module.borderColor
        radius: 4

        Canvas {
            id: histogramCanvas
            anchors.fill: parent
            anchors.margins: 7
            antialiasing: true

            onPaint: {
                const context = getContext("2d")
                context.reset()
                context.clearRect(0, 0, width, height)

                if (!module.histogram || module.histogram.length !== 4)
                    return

                let maximum = 1
                for (let channel = 0; channel < 4; ++channel) {
                    const values = module.histogram[channel]
                    for (let index = 0; index < values.length; ++index)
                        maximum = Math.max(maximum, values[index])
                }

                const colors = ["#c7d2e2", "#f07178", "#98c379", "#61afef"]
                for (let channel = 0; channel < 4; ++channel) {
                    const values = module.histogram[channel]
                    if (values.length !== 256)
                        continue

                    context.strokeStyle = colors[channel]
                    context.globalAlpha = channel === 0 ? 0.62 : 0.42
                    context.lineWidth = channel === 0 ? 1.25 : 1
                    context.beginPath()
                    for (let index = 0; index < values.length; ++index) {
                        const x = index * width / 255
                        const ratio = Math.log1p(values[index]) / Math.log1p(maximum)
                        const y = height - ratio * height
                        if (index === 0)
                            context.moveTo(x, y)
                        else
                            context.lineTo(x, y)
                    }
                    context.stroke()
                }
                context.globalAlpha = 1
            }
        }
    }

    Label {
        Layout.fillWidth: true
        visible: module.captureDetails.length > 0
        text: module.captureDetails
        color: "#9dabbc"
        font.pixelSize: 11
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
    }
}
