import QtQuick

// The crop frame over the whole canvas. A drag stays local and is sent once, at the
// release: the image does not change while the frame moves, and the gesture is one
// history step. The engine answers with the crop it keeps (ratio, constraint).
Item {
    id: root
    objectName: "cropOverlay"

    // The canvas on the screen, in the parent's coordinates.
    property real imageX: 0
    property real imageY: 0
    property real imageWidth: 1
    property real imageHeight: 1
    // Normalized on the canvas, from the engine.
    property real cropX: 0
    property real cropY: 0
    property real cropWidth: 1
    property real cropHeight: 1
    // Width over height of the normalized sides; 0 is free.
    property real normalizedRatio: 0
    property bool showGrid: false
    signal cropEdited(real x, real y, real width, real height)

    readonly property real minimumSide: 0.01
    property bool dragging: false
    property rect live: Qt.rect(0, 0, 1, 1)
    property rect start: Qt.rect(0, 0, 1, 1)
    readonly property rect shown: dragging ? live : Qt.rect(cropX, cropY, cropWidth, cropHeight)
    readonly property real frameX: imageX + shown.x * imageWidth
    readonly property real frameY: imageY + shown.y * imageHeight
    readonly property real frameWidth: shown.width * imageWidth
    readonly property real frameHeight: shown.height * imageHeight

    function begin() {
        start = Qt.rect(cropX, cropY, cropWidth, cropHeight)
        live = start
        dragging = true
    }

    function finish() {
        dragging = false
        if (live.x !== cropX || live.y !== cropY || live.width !== cropWidth || live.height !== cropHeight)
            cropEdited(live.x, live.y, live.width, live.height)
    }

    function moved(dx, dy) {
        live = Qt.rect(Math.max(0, Math.min(1 - start.width, start.x + dx)),
                       Math.max(0, Math.min(1 - start.height, start.y + dy)), start.width, start.height)
    }

    // ax and ay: the handle's place on the frame, 0, 0.5 or 1.
    function resized(ax, ay, u, v) {
        let left = start.x, top = start.y, right = start.x + start.width, bottom = start.y + start.height
        if (ax === 0) left = Math.max(0, Math.min(right - minimumSide, u))
        if (ax === 1) right = Math.min(1, Math.max(left + minimumSide, u))
        if (ay === 0) top = Math.max(0, Math.min(bottom - minimumSide, v))
        if (ay === 1) bottom = Math.min(1, Math.max(top + minimumSide, v))
        let width = right - left, height = bottom - top
        if (normalizedRatio > 0) {
            // A corner follows the hand's larger move; a side grows around its middle.
            if (ay === 0.5 || (ax !== 0.5 && width / start.width >= height / start.height))
                height = width / normalizedRatio
            else
                width = height * normalizedRatio
            const anchorX = ax === 0 ? right : ax === 1 ? left : (left + right) / 2
            const anchorY = ay === 0 ? bottom : ay === 1 ? top : (top + bottom) / 2
            const fractionX = ax === 0 ? 1 : ax === 1 ? 0 : 0.5
            const fractionY = ay === 0 ? 1 : ay === 1 ? 0 : 0.5
            // Shrunk until the frame fits on the canvas around its anchor.
            const available = Math.min(fractionX > 0 ? anchorX / (width * fractionX) : Infinity,
                                       fractionX < 1 ? (1 - anchorX) / (width * (1 - fractionX)) : Infinity,
                                       fractionY > 0 ? anchorY / (height * fractionY) : Infinity,
                                       fractionY < 1 ? (1 - anchorY) / (height * (1 - fractionY)) : Infinity, 1)
            width *= available
            height *= available
            left = anchorX - width * fractionX
            top = anchorY - height * fractionY
        }
        live = Qt.rect(left, top, width, height)
    }

    function normalizedX(item, x) { return (item.mapToItem(root, x, 0).x - imageX) / imageWidth }
    function normalizedY(item, y) { return (item.mapToItem(root, 0, y).y - imageY) / imageHeight }

    // What is cut away stays visible, dimmed.
    Repeater {
        model: [
            Qt.rect(root.imageX, root.imageY, root.imageWidth, root.frameY - root.imageY),
            Qt.rect(root.imageX, root.frameY + root.frameHeight, root.imageWidth,
                    root.imageY + root.imageHeight - root.frameY - root.frameHeight),
            Qt.rect(root.imageX, root.frameY, root.frameX - root.imageX, root.frameHeight),
            Qt.rect(root.frameX + root.frameWidth, root.frameY,
                    root.imageX + root.imageWidth - root.frameX - root.frameWidth, root.frameHeight)
        ]
        Rectangle {
            required property rect modelData
            x: modelData.x
            y: modelData.y
            width: Math.max(0, modelData.width)
            height: Math.max(0, modelData.height)
            color: "#99000000"
        }
    }

    // The level grid, on the whole canvas.
    Repeater {
        model: root.showGrid ? 14 : 0
        Rectangle {
            required property int index
            readonly property bool vertical: index < 7
            readonly property real position: ((vertical ? index : index - 7) + 1) / 8
            x: vertical ? root.imageX + position * root.imageWidth : root.imageX
            y: vertical ? root.imageY : root.imageY + position * root.imageHeight
            width: vertical ? 1 : root.imageWidth
            height: vertical ? root.imageHeight : 1
            color: "#66ffffff"
        }
    }

    Rectangle {
        id: frame
        objectName: "cropFrame"
        x: root.frameX
        y: root.frameY
        width: root.frameWidth
        height: root.frameHeight
        color: "transparent"
        border.color: "#ffffff"
        border.width: 1

        // Thirds, while the frame moves.
        Repeater {
            model: root.dragging ? 4 : 0
            Rectangle {
                required property int index
                readonly property bool vertical: index < 2
                readonly property real position: ((vertical ? index : index - 2) + 1) / 3
                x: vertical ? position * frame.width : 0
                y: vertical ? 0 : position * frame.height
                width: vertical ? 1 : frame.width
                height: vertical ? frame.height : 1
                color: "#99ffffff"
            }
        }

        MouseArea {
            id: body
            objectName: "cropBody"
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            cursorShape: Qt.SizeAllCursor
            preventStealing: true
            property point pressed: Qt.point(0, 0)
            onPressed: (mouse) => {
                root.begin()
                body.pressed = Qt.point(root.normalizedX(body, mouse.x), root.normalizedY(body, mouse.y))
            }
            onPositionChanged: (mouse) => {
                if (root.dragging)
                    root.moved(root.normalizedX(body, mouse.x) - body.pressed.x,
                               root.normalizedY(body, mouse.y) - body.pressed.y)
            }
            onReleased: root.finish()
            onCanceled: root.dragging = false
        }

        Repeater {
            model: [
                { ax: 0, ay: 0, cursor: Qt.SizeFDiagCursor }, { ax: 0.5, ay: 0, cursor: Qt.SizeVerCursor },
                { ax: 1, ay: 0, cursor: Qt.SizeBDiagCursor }, { ax: 1, ay: 0.5, cursor: Qt.SizeHorCursor },
                { ax: 1, ay: 1, cursor: Qt.SizeFDiagCursor }, { ax: 0.5, ay: 1, cursor: Qt.SizeVerCursor },
                { ax: 0, ay: 1, cursor: Qt.SizeBDiagCursor }, { ax: 0, ay: 0.5, cursor: Qt.SizeHorCursor }
            ]
            Rectangle {
                id: handle
                required property var modelData
                objectName: "cropHandle_" + modelData.ax + "_" + modelData.ay
                x: modelData.ax * frame.width - width / 2
                y: modelData.ay * frame.height - height / 2
                width: 11
                height: 11
                radius: 2
                color: "#ffffff"
                border.color: "#202020"
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -5
                    acceptedButtons: Qt.LeftButton
                    cursorShape: handle.modelData.cursor
                    preventStealing: true
                    onPressed: root.begin()
                    onPositionChanged: (mouse) => {
                        if (root.dragging)
                            root.resized(handle.modelData.ax, handle.modelData.ay,
                                         root.normalizedX(this, mouse.x), root.normalizedY(this, mouse.y))
                    }
                    onReleased: root.finish()
                    onCanceled: root.dragging = false
                }
            }
        }
    }
}
