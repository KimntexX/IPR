import QtQuick

Canvas {
    id: icon
    property string direction: "right"
    property color strokeColor: "#d7dfeb"
    implicitWidth: 12
    implicitHeight: 12
    antialiasing: true
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onDirectionChanged: requestPaint()
    onStrokeColorChanged: requestPaint()
    onPaint: {
        const context = getContext("2d")
        context.reset()
        context.clearRect(0, 0, width, height)
        const cx = width / 2
        const cy = height / 2
        context.strokeStyle = strokeColor
        context.lineWidth = 1.6
        context.lineCap = "round"
        context.lineJoin = "round"
        context.beginPath()
        if (direction === "left" || direction === "right") {
            const sign = direction === "left" ? -1 : 1
            context.moveTo(cx - 2 * sign, cy - 4)
            context.lineTo(cx + 2 * sign, cy)
            context.lineTo(cx - 2 * sign, cy + 4)
        } else {
            const sign = direction === "up" ? -1 : 1
            context.moveTo(cx - 4, cy - 2 * sign)
            context.lineTo(cx, cy + 2 * sign)
            context.lineTo(cx + 4, cy - 2 * sign)
        }
        context.stroke()
    }
}
