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
    readonly property real canvasWidth: shape && shape.frameAspect ? canvasHeight * shape.frameAspect : imageWidth / Math.max(1e-6, cropWidth)
    readonly property real canvasHeight: imageHeight / Math.max(1e-6, cropHeight)

    function project(point, matrix) {
        if (!matrix || matrix.length !== 9) return point
        const w = matrix[6] * point.x + matrix[7] * point.y + matrix[8]
        if (w <= 1e-9) return Qt.point(NaN, NaN)
        return Qt.point((matrix[0]*point.x + matrix[1]*point.y + matrix[2])/w,
                        (matrix[3]*point.x + matrix[4]*point.y + matrix[5])/w)
    }
    // Input edits stay in the creation frame; display uses the current geometry.
    function toCanvas(x, y) {
        const point = Qt.point(cropX + (x-imageX)/Math.max(1e-6,imageWidth)*cropWidth,
                               cropY + (y-imageY)/Math.max(1e-6,imageHeight)*cropHeight)
        return project(point, shape ? shape.canvasToFrame : null)
    }
    function screenPoint(point) {
        const p = project(point, shape ? shape.frameToCanvas : null)
        return Qt.point(imageX + (p.x-cropX)/Math.max(1e-6,cropWidth)*imageWidth,
                        imageY + (p.y-cropY)/Math.max(1e-6,cropHeight)*imageHeight)
    }
    function screenX(u, v) { return screenPoint(Qt.point(u,v)).x }
    function screenY(v, u) { return screenPoint(Qt.point(u,v)).y }
    function radialPoint(angle) {
        const theta = (shape ? shape.angle : 0) * Math.PI / 180
        const x = radiusX * canvasWidth * Math.cos(angle)
        const y = radiusY * canvasHeight * Math.sin(angle)
        return Qt.point(centerPoint[0] + (Math.cos(theta)*x-Math.sin(theta)*y)/canvasWidth,
                        centerPoint[1] + (Math.sin(theta)*x+Math.cos(theta)*y)/canvasHeight)
    }

    // The gesture in progress; its values replace the engine's until the release.
    property var live: ({})
    property bool dragging: false
    function shown(name, fallback) { return live[name] !== undefined ? live[name] : fallback }
    readonly property var startPoint: dragging && live.start ? live.start : shape && shape.start ? shape.start : [0.5, 0.25]
    readonly property var endPoint: dragging && live.end ? live.end : shape && shape.end ? shape.end : [0.5, 0.75]
    readonly property var centerPoint: dragging && live.center ? live.center : shape && shape.center ? shape.center : [0.5, 0.5]
    readonly property real radiusX: dragging && live.radiusX !== undefined ? live.radiusX : shape ? shape.radiusX : 0.25
    readonly property real radiusY: dragging && live.radiusY !== undefined ? live.radiusY : shape ? shape.radiusY : 0.25

    readonly property bool cone: kind === "triangle" || kind === "trapezoid"
    readonly property real startWidth: kind === "triangle" ? 0
        : dragging && live.startWidth !== undefined ? live.startWidth : shape && shape.startWidth !== undefined ? shape.startWidth : 0.12
    readonly property real endWidth: dragging && live.endWidth !== undefined ? live.endWidth
        : shape && shape.endWidth !== undefined ? shape.endWidth : 0.4
    function coneCorner(atStart, side) {
        const dx = (endPoint[0] - startPoint[0]) * canvasWidth
        const dy = (endPoint[1] - startPoint[1]) * canvasHeight
        const length = Math.max(1e-6, Math.hypot(dx, dy))
        const base = atStart ? startPoint : endPoint
        const half = (atStart ? startWidth : endWidth) * Math.max(canvasWidth, canvasHeight) * side / 2
        return Qt.point(base[0] - dy / length * half / canvasWidth,
                        base[1] + dx / length * half / canvasHeight)
    }

    function begin(field) {
        activeField = field
        live = ({})
        dragging = true
    }
    property string activeField: ""
    function movedTo(x, y) {
        const p = toCanvas(x, y)
        if (!Number.isFinite(p.x) || !Number.isFinite(p.y)) return
        const next = Object.assign({}, live)
        if (activeField === "startWidth" || activeField === "endWidth") {
            const base = activeField === "startWidth" ? startPoint : endPoint
            const dx = (endPoint[0] - startPoint[0]) * canvasWidth
            const dy = (endPoint[1] - startPoint[1]) * canvasHeight
            const length = Math.max(1e-6, Math.hypot(dx, dy))
            const distance = ((p.x-base[0])*canvasWidth*(-dy) + (p.y-base[1])*canvasHeight*dx) / length
            next[activeField] = 2*Math.abs(distance) / Math.max(canvasWidth,canvasHeight)
        } else if (activeField === "radiusX" || activeField === "radiusY") {
            const theta = (shape ? shape.angle : 0) * Math.PI / 180
            const dx = (p.x-centerPoint[0])*canvasWidth, dy = (p.y-centerPoint[1])*canvasHeight
            if (activeField === "radiusX") next.radiusX = Math.abs(dx*Math.cos(theta)+dy*Math.sin(theta))/canvasWidth
            else next.radiusY = Math.abs(-dx*Math.sin(theta)+dy*Math.cos(theta))/canvasHeight
        }
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
                if (Number.isFinite(onCanvas.x) && Number.isFinite(onCanvas.y)) flat.push(onCanvas.x, onCanvas.y)
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
    Canvas {
        id: brushRing
        objectName: "maskBrushRing"
        anchors.fill: parent
        visible: root.kind === "brush" && painter.containsMouse
        readonly property var watched: [painter.mouseX, painter.mouseY, root.brushSize, root.shape,
                                        root.imageWidth, root.imageHeight, root.cropX, root.cropY]
        onWatchedChanged: requestPaint()
        onPaint: {
            const c = getContext("2d"); c.reset()
            const center = root.toCanvas(painter.mouseX,painter.mouseY)
            if (!Number.isFinite(center.x) || !Number.isFinite(center.y)) return
            const radius = root.brushSize * Math.max(root.canvasWidth,root.canvasHeight)
            c.strokeStyle = "#ffffff"; c.lineWidth = 1; c.beginPath()
            for (let i=0; i<=64; ++i) {
                const angle = i*2*Math.PI/64
                const p = root.screenPoint(Qt.point(center.x+radius*Math.cos(angle)/root.canvasWidth,
                                                   center.y+radius*Math.sin(angle)/root.canvasHeight))
                if (i===0) c.moveTo(p.x,p.y); else c.lineTo(p.x,p.y)
            }
            c.stroke()
        }
    }

    // Gradients: what their handles mean, drawn under them.
    Canvas {
        id: guides
        objectName: "maskGuides"
        anchors.fill: parent
        visible: root.kind === "linear" || root.kind === "radial" || root.cone
        readonly property var watched: [root.startPoint, root.endPoint, root.centerPoint, root.radiusX, root.radiusY,
                                        root.imageX, root.imageY, root.imageWidth, root.imageHeight, root.kind,
                                        root.shape ? root.shape.angle : 0, root.startWidth, root.endWidth, root.shape ? root.shape.frameToCanvas : null, root.canvasWidth, root.canvasHeight]
        onWatchedChanged: requestPaint()
        onPaint: {
            const c = getContext("2d")
            c.reset()
            c.strokeStyle = "#ffffff"
            c.lineWidth = 1
            if (root.kind === "linear") {
                const ax = root.screenX(root.startPoint[0], root.startPoint[1]), ay = root.screenY(root.startPoint[1], root.startPoint[0])
                const bx = root.screenX(root.endPoint[0], root.endPoint[1]), by = root.screenY(root.endPoint[1], root.endPoint[0])
                const dx=(root.endPoint[0]-root.startPoint[0])*root.canvasWidth
                const dy=(root.endPoint[1]-root.startPoint[1])*root.canvasHeight
                const length=Math.max(1e-6,Math.hypot(dx,dy))
                const reach=2*Math.max(root.canvasWidth,root.canvasHeight)
                const nx=-dy/length*reach/root.canvasWidth, ny=dx/length*reach/root.canvasHeight
                c.beginPath()
                for (const point of [root.startPoint,root.endPoint]) {
                    const a=root.screenPoint(Qt.point(point[0]-nx,point[1]-ny))
                    const b=root.screenPoint(Qt.point(point[0]+nx,point[1]+ny))
                    if (Number.isFinite(a.x) && Number.isFinite(b.x)) { c.moveTo(a.x,a.y); c.lineTo(b.x,b.y) }
                }
                c.moveTo(ax, ay); c.lineTo(bx, by)
                c.stroke()
            } else if (root.cone) {
                const points = [root.coneCorner(true,1), root.coneCorner(false,1),
                                root.coneCorner(false,-1), root.coneCorner(true,-1)]
                c.beginPath()
                for (let i=0; i<points.length; ++i) {
                    const x=root.screenX(points[i].x, points[i].y), y=root.screenY(points[i].y, points[i].x)
                    if (i===0) c.moveTo(x,y); else c.lineTo(x,y)
                }
                c.closePath()
                c.moveTo(root.screenX(root.startPoint[0], root.startPoint[1]),root.screenY(root.startPoint[1], root.startPoint[0]))
                c.lineTo(root.screenX(root.endPoint[0], root.endPoint[1]),root.screenY(root.endPoint[1], root.endPoint[0]))
                c.stroke()
            } else if (root.kind === "radial") {
                c.beginPath()
                for (let i=0; i<=128; ++i) {
                    const p = root.screenPoint(root.radialPoint(i*2*Math.PI/128))
                    if (i===0) c.moveTo(p.x,p.y); else c.lineTo(p.x,p.y)
                }
                c.stroke()
            }
        }
    }

    Repeater {
        // The handles depend on the kind alone: moving one must not recreate it under the hand.
        model: root.kind === "triangle" ? ["start", "end", "endWidth"]
            : root.kind === "trapezoid" ? ["start", "end", "startWidth", "endWidth"]
            : root.kind === "linear" ? ["start", "end"] : root.kind === "radial" ? ["center", "radiusX", "radiusY"]
            : root.kind === "point" ? ["center"] : []
        Rectangle {
            id: handle
            required property string modelData
            readonly property point place: modelData === "start" ? Qt.point(root.startPoint[0], root.startPoint[1])
                : modelData === "end" ? Qt.point(root.endPoint[0], root.endPoint[1])
                : modelData === "startWidth" ? root.coneCorner(true,1)
                : modelData === "endWidth" ? root.coneCorner(false,1)
                : modelData === "center" ? Qt.point(root.centerPoint[0], root.centerPoint[1])
                : modelData === "radiusX" ? root.radialPoint(0)
                : root.radialPoint(Math.PI/2)
            objectName: "maskHandle_" + modelData
            readonly property point displayPlace: root.screenPoint(place)
            visible: Number.isFinite(displayPlace.x) && Number.isFinite(displayPlace.y)
            x: visible ? displayPlace.x - width / 2 : 0
            y: visible ? displayPlace.y - height / 2 : 0
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
