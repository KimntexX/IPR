import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "imageViewport"
    clip: true

    property url source
    property url originalSource
    property bool comparisonEnabled: false
    property bool originalHeld: false
    property real comparisonPosition: 0.5
    // Stable photo identity: preview URLs change on every adjustment.
    property string imageKey: ""
    property bool hasImage: false
    property color textColor: "#edf2f8"
    property color panelColor: "#2a3039"
    signal contextMenuRequested(real x, real y)
    // Point colour eyedropper: a click reports its place in [0, 1] of the image.
    property bool picking: false
    signal pointPicked(real x, real y)
    signal pickingCancelled()
    property real previewWidth: 0
    property real previewHeight: 0
    property bool fitMode: true
    property real manualScale: 1
    property real offsetX: 0
    property real offsetY: 0
    readonly property real maximumScale: 8
    readonly property real fitScale: previewWidth > 0 && previewHeight > 0
        ? Math.min(1, width / previewWidth, height / previewHeight) : 1
    readonly property real minimumScale: Math.min(fitScale, 0.05)
    readonly property real zoomScale: fitMode ? fitScale : manualScale
    readonly property real imageLeft: (width - previewWidth * zoomScale) / 2 + offsetX
    readonly property real imageTop: (height - previewHeight * zoomScale) / 2 + offsetY
    readonly property bool imageReady: hasImage && canvas.status === Image.Ready
        && previewWidth > 0 && previewHeight > 0 && width > 0 && height > 0
    readonly property bool canPan: imageReady && (canvas.width > width || canvas.height > height)
    readonly property bool comparisonReady: imageReady && originalCanvas.status === Image.Ready
        && originalCanvas.sourceSize.width === previewWidth
        && originalCanvas.sourceSize.height === previewHeight
    readonly property bool showingOriginal: originalHeld && comparisonReady
    readonly property bool showingComparison: comparisonEnabled && comparisonReady && !showingOriginal
    // At fit the provider reduces the preview to the exact displayed size (area
    // average): sharper than the mipmapped GPU minification, which blends two
    // pre-reduced levels, and without the aliasing of a plain bilinear one.
    readonly property bool exactFit: fitMode && fitScale < 1 && imageReady
    // in device pixels: the reduction targets the screen's real pixels (Windows scaling)
    readonly property real devicePixelRatio: Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1
    readonly property size fitPixels: Qt.size(Math.round(previewWidth * fitScale * devicePixelRatio),
                                              Math.round(previewHeight * fitScale * devicePixelRatio))
    readonly property real maxOffsetX: Math.max(0, (canvas.width - width) / 2)
    readonly property real maxOffsetY: Math.max(0, (canvas.height - height) / 2)
    readonly property real toolbarHeight: zoomToolbar.visible ? zoomToolbar.height + 12 : 0

    function clampPan() {
        offsetX = Math.max(-maxOffsetX, Math.min(maxOffsetX, offsetX))
        offsetY = Math.max(-maxOffsetY, Math.min(maxOffsetY, offsetY))
    }

    function fitImage() {
        fitMode = true
        offsetX = 0
        offsetY = 0
    }

    function setZoom(scale, anchorX, anchorY) {
        if (!imageReady || !isFinite(scale) || scale <= 0)
            return
        const x = anchorX === undefined ? width / 2 : anchorX
        const y = anchorY === undefined ? height / 2 : anchorY
        const imageX = (x - imageLeft) / zoomScale
        const imageY = (y - imageTop) / zoomScale
        const next = Math.max(minimumScale, Math.min(maximumScale, scale))
        manualScale = next
        fitMode = false
        // Keep the image pixel below the mouse in place, subject to pan bounds.
        offsetX = x - width / 2 + (previewWidth / 2 - imageX) * next
        offsetY = y - height / 2 + (previewHeight / 2 - imageY) * next
        clampPan()
    }

    function zoomIn() { setZoom(zoomScale * 1.25) }
    function zoomOut() { setZoom(zoomScale / 1.25) }
    function actualSize() { setZoom(1) }

    function toggleZoom(x, y) {
        if (!imageReady || x < imageLeft || x > imageLeft + canvas.width
                || y < imageTop || y > imageTop + canvas.height)
            return
        if (fitMode)
            setZoom(Math.max(1, fitScale * 2), x, y)
        else
            fitImage()
    }

    // Wheel processing is shared by input events and deterministic QML tests.
    function handleWheel(angleX, angleY, pixelX, pixelY, modifiers, x, y) {
        if (!imageReady)
            return false
        if (modifiers & Qt.ControlModifier) {
            const delta = angleY !== 0 ? angleY : pixelY
            if (delta === 0)
                return false
            setZoom(zoomScale * Math.pow(1.25, delta / 120), x, y)
            return true
        }
        if (!canPan)
            return false
        let dx = pixelX !== 0 ? pixelX : angleX / 2
        let dy = pixelY !== 0 ? pixelY : angleY / 2
        if ((modifiers & Qt.ShiftModifier) && dx === 0) {
            dx = dy
            dy = 0
        }
        offsetX += dx
        offsetY += dy
        clampPan()
        return dx !== 0 || dy !== 0
    }

    function resetView() {
        originalHeld = false
        fitImage()
        comparisonEnabled = false
        comparisonPosition = 0.5
    }

    function toggleComparison() {
        if (comparisonReady)
            comparisonEnabled = !comparisonEnabled
    }

    function moveComparison(x) {
        if (width > 0 && isFinite(x))
            comparisonPosition = Math.max(0, Math.min(1, x / width))
    }

    onImageKeyChanged: {
        resetView()
        if (picking) pickingCancelled()
    }
    onHasImageChanged: if (!hasImage) resetView()
    onWidthChanged: clampPan()
    onHeightChanged: clampPan()
    onActiveFocusChanged: if (!activeFocus) originalHeld = false
    onVisibleChanged: if (!visible) originalHeld = false
    Keys.onPressed: (event) => {
        if (picking && event.key === Qt.Key_Escape) {
            pickingCancelled()
            event.accepted = true
            return
        }
        if (hasImage && (event.key === Qt.Key_Menu ||
                (event.key === Qt.Key_F10 && event.modifiers === Qt.ShiftModifier))) {
            contextMenuRequested(width / 2, height / 2)
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Space && event.modifiers === Qt.NoModifier && comparisonReady) {
            if (!event.isAutoRepeat) originalHeld = true
            event.accepted = true
        }
    }
    Keys.onReleased: (event) => {
        if (event.key === Qt.Key_Space) {
            if (!event.isAutoRepeat) originalHeld = false
            event.accepted = true
        }
    }
    Connections {
        target: root.Window.window
        function onActiveChanged() {
            if (!root.Window.window.active) root.originalHeld = false
        }
    }

    // Clip both halves: translucent images must not be painted twice on the left.
    Item {
        id: currentClip
        objectName: "currentClip"
        x: root.showingComparison ? root.width * root.comparisonPosition : 0
        width: root.showingOriginal ? 0 : root.width - x
        height: root.height
        clip: true
        Image {
            id: canvas
            objectName: "viewportImage"
            x: root.imageLeft - currentClip.x
            y: root.imageTop
            width: root.previewWidth * root.zoomScale
            height: root.previewHeight * root.zoomScale
            source: root.hasImage ? root.source : ""
            asynchronous: true
            retainWhileLoading: true
            cache: false
            smooth: true
            // Mipmapped minification out of fit: a plain bilinear reduction beyond 2x
            // aliases the grain and showed more noise than the image holds.
            mipmap: true
            visible: root.hasImage && !fitCanvas.shown
            fillMode: Image.PreserveAspectFit
            onStatusChanged: if (status === Image.Ready) {
                root.previewWidth = sourceSize.width
                root.previewHeight = sourceSize.height
                root.clampPan()
            }
        }
        Image {
            id: fitCanvas
            objectName: "fitViewportImage"
            readonly property bool shown: root.exactFit && status === Image.Ready
                && sourceSize.width === root.fitPixels.width && sourceSize.height === root.fitPixels.height
            x: Math.round(canvas.x * root.devicePixelRatio) / root.devicePixelRatio
            y: Math.round(canvas.y * root.devicePixelRatio) / root.devicePixelRatio
            width: root.fitPixels.width / root.devicePixelRatio
            height: root.fitPixels.height / root.devicePixelRatio
            source: root.exactFit ? root.source : ""
            sourceSize: root.fitPixels
            asynchronous: true
            retainWhileLoading: true
            cache: false
            smooth: false
            visible: shown
        }
    }

    // Both images use precisely the same transform in viewport coordinates.
    Item {
        objectName: "originalClip"
        width: root.showingOriginal ? root.width : root.width * root.comparisonPosition
        height: root.height
        clip: true
        visible: root.showingComparison || root.showingOriginal
        Image {
            id: originalCanvas
            objectName: "originalViewportImage"
            x: root.imageLeft
            y: root.imageTop
            width: canvas.width
            height: canvas.height
            source: root.hasImage ? root.originalSource : ""
            asynchronous: true
            retainWhileLoading: true
            cache: false
            smooth: true
            mipmap: true
            visible: !fitOriginal.shown
            fillMode: Image.PreserveAspectFit
        }
        Image {
            id: fitOriginal
            objectName: "fitOriginalViewportImage"
            readonly property bool shown: root.exactFit && status === Image.Ready
                && sourceSize.width === root.fitPixels.width && sourceSize.height === root.fitPixels.height
            x: Math.round(originalCanvas.x * root.devicePixelRatio) / root.devicePixelRatio
            y: Math.round(originalCanvas.y * root.devicePixelRatio) / root.devicePixelRatio
            width: root.fitPixels.width / root.devicePixelRatio
            height: root.fitPixels.height / root.devicePixelRatio
            source: root.exactFit && (root.showingComparison || root.showingOriginal) ? root.originalSource : ""
            sourceSize: root.fitPixels
            asynchronous: true
            retainWhileLoading: true
            cache: false
            smooth: false
            visible: shown
        }
    }

    MouseArea {
        id: interaction
        objectName: "viewportMouseArea"
        anchors.fill: parent
        enabled: root.imageReady
        acceptedButtons: Qt.LeftButton
        hoverEnabled: true
        preventStealing: true
        cursorShape: root.picking ? Qt.CrossCursor : root.canPan
            ? (pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor) : Qt.PointingHandCursor
        property real pressX: 0
        property real pressY: 0
        property real startOffsetX: 0
        property real startOffsetY: 0
        property bool dragged: false
        onPressed: (mouse) => {
            root.forceActiveFocus()
            pressX = mouse.x
            pressY = mouse.y
            startOffsetX = root.offsetX
            startOffsetY = root.offsetY
            dragged = false
        }
        onPositionChanged: (mouse) => {
            if (!pressed || !root.canPan)
                return
            const dx = mouse.x - pressX
            const dy = mouse.y - pressY
            if (Math.hypot(dx, dy) > Qt.styleHints.startDragDistance)
                dragged = true
            if (dragged) {
                root.offsetX = startOffsetX + dx
                root.offsetY = startOffsetY + dy
                root.clampPan()
            }
        }
        onClicked: (mouse) => {
            if (dragged)
                return
            if (root.picking) {
                const x = (mouse.x - root.imageLeft) / (root.previewWidth * root.zoomScale)
                const y = (mouse.y - root.imageTop) / (root.previewHeight * root.zoomScale)
                if (x >= 0 && x <= 1 && y >= 0 && y <= 1)
                    root.pointPicked(x, y)
                return
            }
            root.toggleZoom(mouse.x, mouse.y)
        }
        onWheel: (wheel) => {
            wheel.accepted = root.handleWheel(wheel.angleDelta.x, wheel.angleDelta.y,
                wheel.pixelDelta.x, wheel.pixelDelta.y, wheel.modifiers, wheel.x, wheel.y)
        }
    }

    MouseArea {
        objectName: "viewportContextArea"
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        enabled: root.hasImage
        onClicked: (mouse) => root.contextMenuRequested(mouse.x, mouse.y)
    }

    Item {
        id: divider
        objectName: "comparisonDivider"
        x: root.width * root.comparisonPosition - width / 2
        width: 24
        height: root.height
        visible: root.showingComparison
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 2
            height: parent.height
            color: root.textColor
            opacity: 0.85
        }
        Rectangle {
            anchors.centerIn: parent
            width: 24
            height: 38
            radius: 5
            color: root.panelColor
            border.color: root.textColor
            Text {
                anchors.centerIn: parent
                text: "↔"
                color: root.textColor
            }
        }
        MouseArea {
            objectName: "comparisonDragArea"
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            preventStealing: true
            cursorShape: Qt.SplitHCursor
            property real grabOffset: 0
            onPressed: (mouse) => {
                root.forceActiveFocus()
                grabOffset = root.mapFromItem(divider, mouse.x, mouse.y).x
                    - root.width * root.comparisonPosition
            }
            onPositionChanged: (mouse) => {
                if (pressed)
                    root.moveComparison(root.mapFromItem(divider, mouse.x, mouse.y).x - grabOffset)
            }
            // Ctrl-wheel above the separator has the same anchored zoom.
            onWheel: (wheel) => {
                const point = root.mapFromItem(divider, wheel.x, wheel.y)
                wheel.accepted = root.handleWheel(wheel.angleDelta.x, wheel.angleDelta.y,
                    wheel.pixelDelta.x, wheel.pixelDelta.y, wheel.modifiers, point.x, point.y)
            }
        }
    }

    Row {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 6
        anchors.topMargin: root.toolbarHeight + 4
        spacing: 1
        visible: root.showingComparison || root.showingOriginal
        Repeater {
            model: root.showingOriginal ? [qsTr("Origine — relâcher pour revenir")]
                                        : [qsTr("Origine ←"), qsTr("→ Actuel")]
            Rectangle {
                required property string modelData
                width: caption.implicitWidth + 14
                height: caption.implicitHeight + 8
                radius: 3
                color: Qt.rgba(root.panelColor.r, root.panelColor.g, root.panelColor.b, 0.92)
                Text {
                    id: caption
                    anchors.centerIn: parent
                    text: modelData
                    color: root.textColor
                    font.pixelSize: 12
                }
            }
        }
    }

    Rectangle {
        id: zoomToolbar
        objectName: "viewportToolbar"
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 6
        width: Math.max(0, Math.min(root.width - 12, controls.implicitWidth + 12))
        height: controls.implicitHeight + 8
        color: Qt.rgba(root.panelColor.r, root.panelColor.g, root.panelColor.b, 0.92)
        radius: 5
        visible: root.hasImage

        GridLayout {
            id: controls
            anchors.centerIn: parent
            width: Math.max(0, parent.width - 12)
            columns: root.width < 360 ? 3 : (root.width < 520 ? 4 : 7)
            columnSpacing: 3
            rowSpacing: 3

            Label {
                objectName: "zoomPercentage"
                Layout.minimumWidth: 46
                horizontalAlignment: Text.AlignHCenter
                color: root.textColor
                text: Math.round(root.zoomScale * 100) + " %"
                font.pixelSize: 12
            }
            PreferenceButton {
                objectName: "holdOriginalButton"
                text: qsTr("Origine")
                implicitHeight: 26
                enabled: root.comparisonReady
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Maintenir pour voir l’original — Espace dans la vue image")
                Accessible.name: ToolTip.text
                onPressed: root.originalHeld = true
                onReleased: root.originalHeld = false
                onCanceled: root.originalHeld = false
                onEnabledChanged: if (!enabled) root.originalHeld = false
            }
            Repeater {
                model: [
                    {name: "zoomOutButton", label: "−", tip: qsTr("Réduire — Ctrl −")},
                    {name: "zoomInButton", label: "+", tip: qsTr("Agrandir — Ctrl +")},
                    {name: "actualSizeButton", label: "100 %", tip: qsTr("100 % de l'aperçu — Ctrl 1")},
                    {name: "fitImageButton", label: qsTr("Ajuster"), tip: qsTr("Adapter à la fenêtre — Ctrl 0")},
                    {name: "compareImageButton", label: qsTr("Avant/après"), tip: qsTr("Origine à gauche / actuel à droite — Ctrl Maj Y")}
                ]
                PreferenceButton {
                    id: zoomButton
                    required property var modelData
                    required property int index
                    objectName: modelData.name
                    text: modelData.label
                    enabled: root.imageReady && (index !== 4 || root.comparisonReady)
                        && (index !== 0 || root.zoomScale > root.minimumScale)
                        && (index !== 1 || root.zoomScale < root.maximumScale)
                    implicitHeight: 26
                    implicitWidth: Math.max(28, contentItem.implicitWidth + 14)
                    hoverEnabled: true
                    checkable: index === 4
                    checked: index === 4 && root.comparisonEnabled
                    ToolTip.visible: hovered
                    ToolTip.text: modelData.tip
                    Accessible.name: modelData.tip
                    onClicked: {
                        if (index === 0) root.zoomOut()
                        else if (index === 1) root.zoomIn()
                        else if (index === 2) root.actualSize()
                        else if (index === 3) root.fitImage()
                        else root.toggleComparison()
                    }
                    contentItem: Text {
                        text: zoomButton.text
                        color: root.textColor
                        opacity: zoomButton.enabled ? 1 : 0.45
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 3
                        border.width: zoomButton.activeFocus ? 2 : 0
                        border.color: root.textColor
                        color: zoomButton.down || zoomButton.checked || zoomButton.hovered
                            ? zoomButton.palette.mid : zoomButton.palette.button
                    }
                }
            }
        }
    }

    Shortcut {
        sequences: ["Ctrl++", "Ctrl+=", "Ctrl+Shift++"]
        enabled: root.imageReady && root.visible
        onActivated: root.zoomIn()
    }
    Shortcut {
        sequence: "Ctrl+-"
        enabled: root.imageReady && root.visible
        onActivated: root.zoomOut()
    }
    Shortcut {
        sequence: "Ctrl+0"
        enabled: root.imageReady && root.visible
        onActivated: root.fitImage()
    }
    Shortcut {
        sequence: "Ctrl+1"
        enabled: root.imageReady && root.visible
        onActivated: root.actualSize()
    }
    Shortcut {
        sequence: "Ctrl+Shift+Y"
        enabled: root.comparisonReady && root.visible
        onActivated: root.toggleComparison()
    }
}
