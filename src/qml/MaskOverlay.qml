import QtQuick

// What the shape in hand shows over the image: a brush paints, a gradient has handles. A
// gesture stays local and is sent once, at the release: one history step. Everything is sent
// on the normalized canvas, whatever the zoom and the crop.
Item {
    id: root
    objectName: "maskOverlay"

    // The displayed image, in the parent's coordinates.
    property real imageX: 0
    property real imageY: 0
    property real imageWidth: 1
    property real imageHeight: 1
    // The crop it shows, normalized on the canvas.
    property real cropX: 0
    property real cropY: 0
    property real cropWidth: 1
    property real cropHeight: 1
    property var shape: null
    property real brushSize: 0.05     // radius, as a share of the canvas's long side
    signal shapeEdited(var values)
    signal strokeDrawn(var points)

    readonly property string kind: shape ? shape.kind : ""
    // The whole canvas on the screen, of which the image shows the crop.
    readonly property real canvasWidth: imageWidth / Math.max(1e-6, cropWidth)
    readonly property real canvasHeight: imageHeight / Math.max(1e-6, cropHeight)

    function toCanvas(x, y) {
        return Qt.point(cropX + (x - imageX) / Math.max(1e-6, imageWidth) * cropWidth,
                        cropY + (y - imageY) / Math.max(1e-6, imageHeight) * cropHeight)
    }
    function screenX(u) { return imageX + (u - cropX) / Math.max(1e-6, cropWidth) * imageWidth }
    function screenY(v) { return imageY + (v - cropY) / Math.max(1e-6, cropHeight) * imageHeight }

    // The gesture in progress; its values replace the engine's until the release.
    property var live: ({})
    property bool dragging: false
    function shown(name, fallback) { return live[name] !== undefined ? live[name] : fallback }
    readonly property var startPoint: dragging && live.start ? live.start : shape && shape.start ? shape.start : [0.5, 0.25]
    readonly property var endPoint: dragging && live.end ? live.end : shape && shape.end ? shape.end : [0.5, 0.75]
    readonly property var centerPoint: dragging && live.center ? live.center : shape && shape.center ? shape.center : [0.5, 0.5]
    readonly property real radiusX: dragging && live.radiusX !== undefined ? live.radiusX : shape ? shape.radiusX : 0.25
    readonly property real radiusY: dragging && live.radiusY !== undefined ? live.radiusY : shape ? shape.radiusY : 0.25

    function begin(field) {
        activeField = field
        live = ({})
        dragging = true
    }
    property string activeField: ""
    function movedTo(x, y) {
        const p = toCanvas(x, y)
        const next = Object.assign({}, live)
        if (activeField === "radiusX")
            next.radiusX = Math.abs(p.x - centerPoint[0])
        else if (activeField === "radiusY")
            next.radiusY = Math.abs(p.y - centerPoint[1])
        else
            next[activeField] = [p.x, p.y]
        live = next
    }
    function finish() {
        const sent = live
        dragging = false
        activeField = ""
        live = ({})
        if (Object.keys(sent).length > 0)
            shapeEdited(sent)
    }
    function cancel() {
        dragging = false
        activeField = ""
        live = ({})
    }

    // A brush: the stroke follows the hand, and goes to the engine at the release.
    MouseArea {
        id: painter
        objectName: "maskPainter"
        anchors.fill: parent
        enabled: root.kind === "brush"
        visible: enabled
        acceptedButtons: Qt.LeftButton
        hoverEnabled: true
        preventStealing: true
        cursorShape: Qt.CrossCursor
        property var points: []
        onPressed: (mouse) => { points = [Qt.point(mouse.x, mouse.y)]; stroke.requestPaint() }
        onPositionChanged: (mouse) => {
            if (!pressed)
                return
            points.push(Qt.point(mouse.x, mouse.y))
            stroke.requestPaint()
        }
        onReleased: {
            const flat = []
            for (const point of points) {
                const onCanvas = root.toCanvas(point.x, point.y)
                flat.push(onCanvas.x, onCanvas.y)
            }
            points = []
            stroke.requestPaint()
            if (flat.length > 0)
                root.strokeDrawn(flat)
        }
        onCanceled: { points = []; stroke.requestPaint() }
    }
    Canvas {
        id: stroke
        objectName: "maskStroke"
        anchors.fill: parent
        visible: root.kind === "brush"
        onPaint: {
            const c = getContext("2d")
            c.reset()
            const points = painter.points
            const radius = root.brushSize * Math.max(root.canvasWidth, root.canvasHeight)
            if (points.length > 0) {
                c.strokeStyle = "rgba(255, 255, 255, 0.45)"
                c.lineWidth = 2 * radius
                c.lineCap = "round"
                c.lineJoin = "round"
                c.beginPath()
                c.moveTo(points[0].x, points[0].y)
                for (let i = 1; i < points.length; ++i)
                    c.lineTo(points[i].x, points[i].y)
                if (points.length === 1)
                    c.lineTo(points[0].x + 0.01, points[0].y)
                c.stroke()
            }
        }
    }
    // The brush's size under the hand.
    Rectangle {
        objectName: "maskBrushRing"
        visible: root.kind === "brush" && painter.containsMouse
        readonly property real radius2: root.brushSize * Math.max(root.canvasWidth, root.canvasHeight)
        x: painter.mouseX - radius2
        y: painter.mouseY - radius2
        width: 2 * radius2
        height: 2 * radius2
        radius: radius2
        color: "transparent"
        border.color: "#ffffff"
        border.width: 1
    }

    // Gradients: what their handles mean, drawn under them.
    Canvas {
        id: guides
        objectName: "maskGuides"
        anchors.fill: parent
        visible: root.kind === "linear" || root.kind === "radial"
        readonly property var watched: [root.startPoint, root.endPoint, root.centerPoint, root.radiusX, root.radiusY,
                                        root.imageX, root.imageY, root.imageWidth, root.imageHeight, root.kind,
                                        root.shape ? root.shape.angle : 0]
        onWatchedChanged: requestPaint()
        onPaint: {
            const c = getContext("2d")
            c.reset()
            c.strokeStyle = "#ffffff"
            c.lineWidth = 1
            if (root.kind === "linear") {
                const ax = root.screenX(root.startPoint[0]), ay = root.screenY(root.startPoint[1])
                const bx = root.screenX(root.endPoint[0]), by = root.screenY(root.endPoint[1])
                const length = Math.max(1, Math.hypot(bx - ax, by - ay))
                // Two lines across the axis, as far as the view goes.
                const nx = -(by - ay) / length * 4000, ny = (bx - ax) / length * 4000
                c.beginPath()
                c.moveTo(ax - nx, ay - ny); c.lineTo(ax + nx, ay + ny)
                c.moveTo(bx - nx, by - ny); c.lineTo(bx + nx, by + ny)
                c.moveTo(ax, ay); c.lineTo(bx, by)
                c.stroke()
            } else if (root.kind === "radial") {
                c.save()
                c.translate(root.screenX(root.centerPoint[0]), root.screenY(root.centerPoint[1]))
                c.rotate((root.shape ? root.shape.angle : 0) * Math.PI / 180)
                c.scale(Math.max(1, root.radiusX * root.canvasWidth), Math.max(1, root.radiusY * root.canvasHeight))
                c.beginPath()
                c.arc(0, 0, 1, 0, 2 * Math.PI)
                c.restore()
                c.stroke()
            }
        }
    }

    Repeater {
        // The handles depend on the kind alone: moving one must not recreate it under the hand.
        model: root.kind === "linear" ? ["start", "end"] : root.kind === "radial" ? ["center", "radiusX", "radiusY"] : []
        Rectangle {
            id: handle
            required property string modelData
            readonly property point place: modelData === "start" ? Qt.point(root.startPoint[0], root.startPoint[1])
                : modelData === "end" ? Qt.point(root.endPoint[0], root.endPoint[1])
                : modelData === "center" ? Qt.point(root.centerPoint[0], root.centerPoint[1])
                : modelData === "radiusX" ? Qt.point(root.centerPoint[0] + root.radiusX, root.centerPoint[1])
                : Qt.point(root.centerPoint[0], root.centerPoint[1] + root.radiusY)
            objectName: "maskHandle_" + modelData
            x: root.screenX(place.x) - width / 2
            y: root.screenY(place.y) - height / 2
            width: 13
            height: 13
            radius: width / 2
            color: "#ffffff"
            border.color: "#202020"
            MouseArea {
                anchors.fill: parent
                anchors.margins: -6
                acceptedButtons: Qt.LeftButton
                preventStealing: true
                cursorShape: Qt.SizeAllCursor
                // The handle moves under the hand: its gesture runs in the overlay's coordinates.
                onPressed: root.begin(handle.modelData)
                onPositionChanged: (mouse) => {
                    if (!pressed || !root.dragging)
                        return
                    const at = mapToItem(root, mouse.x, mouse.y)
                    root.movedTo(at.x, at.y)
                }
                onReleased: root.finish()
                onCanceled: root.cancel()
            }
        }
    }
}
