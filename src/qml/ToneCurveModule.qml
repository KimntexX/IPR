import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: module

    required property var editor
    property bool showTitle: true
    required property int activeCurve
    required property var curvePoints
    required property var curveName
    required property var curveColor
    required property var evaluateCurve
    property color textColor: "#edf2f8"
    property color secondaryTextColor: "#d7dfeb"
    // Display reference: the current sRGB preview counts. The curves act on ProPhoto
    // encoded with the same sRGB transfer, so the two scales nearly match.
    property var histogram: []
    readonly property var luminanceHistogram: histogram && histogram.length === 4 &&
        histogram[0].length === 256 ? histogram[0] : []
    readonly property real histogramPeak: {
        let peak = 1
        for (const count of luminanceHistogram)
            if (Number.isFinite(count)) peak = Math.max(peak, count)
        return peak
    }
    signal curvePointsEdited(var points)
    signal commitRequested()
    signal activeCurveRequested(int channel)

    Layout.fillWidth: true
    Layout.minimumWidth: 0
    spacing: 12

    function requestPaint() {
        if (toneCanvas) toneCanvas.requestPaint()
    }
    function histogramHeight(count) {
        return Number.isFinite(count) ? Math.max(0, Math.min(1, count / histogramPeak)) : 0
    }
    onHistogramChanged: requestPaint()

    onVisibleChanged: if (visible) requestPaint()

    Label {
        visible: module.showTitle
        text: qsTr("Courbe de tonalité")
        color: module.secondaryTextColor
        font.bold: true
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 196
        color: "#171b21"
        border.color: "#3d4654"
        radius: 4

        Canvas {
            id: toneCanvas
            objectName: "toneCurveCanvas"
            anchors.fill: parent
            anchors.margins: 10
            antialiasing: true
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            Component.onCompleted: requestPaint()

            property real leftMargin: 8
            property real rightMargin: 8
            property real topMargin: 8
            property real bottomMargin: 8

            function curveX(point) {
                return leftMargin + point.x * (width - leftMargin - rightMargin)
            }

            function curveY(point) {
                return height - bottomMargin -
                       point.y * (height - topMargin - bottomMargin)
            }

            function normalizedX(position) {
                return Math.max(0, Math.min(1,
                    (position - leftMargin) / (width - leftMargin - rightMargin)))
            }

            function normalizedY(position) {
                return Math.max(0, Math.min(1,
                    (height - bottomMargin - position) /
                    (height - topMargin - bottomMargin)))
            }

            onPaint: {
                const context = getContext("2d")
                context.reset()
                context.fillStyle = "#171b21"
                context.fillRect(0, 0, width, height)

                const graphWidth = width - leftMargin - rightMargin
                const graphHeight = height - topMargin - bottomMargin
                // One subdued, filled luminance silhouette, behind both the
                // grid and editable curve. Never stack the RGB channels here.
                if (module.luminanceHistogram.length === 256 && graphWidth > 0 && graphHeight > 0) {
                    const baseline = height - bottomMargin
                    context.save()
                    context.beginPath()
                    context.rect(leftMargin, topMargin, graphWidth, graphHeight)
                    context.clip()
                    context.beginPath()
                    context.moveTo(leftMargin, baseline)
                    for (let bin = 0; bin < 256; ++bin)
                        context.lineTo(leftMargin + bin / 255 * graphWidth,
                            baseline - module.histogramHeight(module.luminanceHistogram[bin]) * graphHeight * 0.72)
                    context.lineTo(leftMargin + graphWidth, baseline)
                    context.closePath()
                    context.fillStyle = "#aeb8c8"
                    context.globalAlpha = 0.18
                    context.fill()
                    context.restore()
                }
                context.strokeStyle = "#303946"
                context.lineWidth = 1
                for (let step = 0; step <= 4; ++step) {
                    const horizontal = topMargin + step * graphHeight / 4
                    const vertical = leftMargin + step * graphWidth / 4
                    context.beginPath()
                    context.moveTo(leftMargin, horizontal)
                    context.lineTo(width - rightMargin, horizontal)
                    context.moveTo(vertical, topMargin)
                    context.lineTo(vertical, height - bottomMargin)
                    context.stroke()
                }

                const points = module.curvePoints
                if (points.length < 2)
                    return
                context.strokeStyle = module.curveColor(module.activeCurve)
                context.lineWidth = 2
                context.beginPath()
                const sampleCount = 128
                for (let index = 0; index <= sampleCount; ++index) {
                    const sample = index / sampleCount
                    const x = leftMargin + sample * graphWidth
                    const y = curveY({
                        "x": sample,
                        "y": module.evaluateCurve(points, sample)
                    })
                    if (index === 0)
                        context.moveTo(x, y)
                    else
                        context.lineTo(x, y)
                }
                context.stroke()
                for (let index = 0; index < points.length; ++index) {
                    const x = curveX(points[index])
                    const y = curveY(points[index])
                    context.fillStyle = index === 0 || index === points.length - 1
                                      ? "#d7dfeb" : module.curveColor(module.activeCurve)
                    context.beginPath()
                    context.arc(x, y, 5, 0, 2 * Math.PI)
                    context.fill()
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            enabled: module.editor.hasImage
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            property int draggedIndex: -1
            ToolTip.visible: containsMouse && pressedButtons === Qt.NoButton
            ToolTip.delay: 900
            ToolTip.text: qsTr("Fond monochrome : luminance de l’aperçu sRGB, repère visuel après rendu (pas le signal linéaire d’entrée des courbes).")

            function nearestPoint(mouse) {
                let nearest = -1
                let distance = 11
                for (let index = 0; index < module.curvePoints.length; ++index) {
                    const point = module.curvePoints[index]
                    const dx = toneCanvas.curveX(point) - mouse.x
                    const dy = toneCanvas.curveY(point) - mouse.y
                    const candidate = Math.sqrt(dx * dx + dy * dy)
                    if (candidate < distance) {
                        distance = candidate
                        nearest = index
                    }
                }
                return nearest
            }

            function updateDraggedPoint(mouse) {
                if (draggedIndex <= 0 ||
                    draggedIndex >= module.curvePoints.length - 1)
                    return
                const points = module.curvePoints.slice()
                const previous = points[draggedIndex - 1]
                const next = points[draggedIndex + 1]
                points[draggedIndex] = {
                    "x": Math.max(previous.x + 0.001,
                                  Math.min(next.x - 0.001,
                                           toneCanvas.normalizedX(mouse.x))),
                    "y": toneCanvas.normalizedY(mouse.y)
                }
                module.curvePointsEdited(points)
                toneCanvas.requestPaint()
            }

            onPressed: function(mouse) {
                const nearest = nearestPoint(mouse)
                if (mouse.button === Qt.RightButton) {
                    if (nearest > 0 && nearest < module.curvePoints.length - 1) {
                        const points = module.curvePoints.slice()
                        points.splice(nearest, 1)
                        module.curvePointsEdited(points)
                        module.commitRequested()
                    }
                    return
                }
                draggedIndex = nearest
            }

            onPositionChanged: function(mouse) {
                if (pressedButtons & Qt.LeftButton)
                    updateDraggedPoint(mouse)
            }

            onReleased: {
                if (draggedIndex >= 0) {
                    module.commitRequested()
                    draggedIndex = -1
                }
            }

            onDoubleClicked: function(mouse) {
                if (mouse.button !== Qt.LeftButton)
                    return
                const points = module.curvePoints.slice()
                points.push({
                    "x": toneCanvas.normalizedX(mouse.x),
                    "y": toneCanvas.normalizedY(mouse.y)
                })
                points.sort(function(left, right) { return left.x - right.x })
                module.curvePointsEdited(points)
                module.commitRequested()
            }
        }
    }

    SectionNote {
        text: qsTr("Double-cliquez pour ajouter une poignée, faites-la glisser pour modifier la courbe et utilisez le clic droit pour supprimer une poignée.")
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        CurveChannelButton {
            channel: 0
            activeCurve: module.activeCurve
            curveName: module.curveName
            curveColor: module.curveColor
            onChannelRequested: function(requestedChannel) {
                module.activeCurveRequested(requestedChannel)
            }
        }
        CurveChannelButton {
            channel: 1
            activeCurve: module.activeCurve
            curveName: module.curveName
            curveColor: module.curveColor
            onChannelRequested: function(requestedChannel) {
                module.activeCurveRequested(requestedChannel)
            }
        }
        CurveChannelButton {
            channel: 2
            activeCurve: module.activeCurve
            curveName: module.curveName
            curveColor: module.curveColor
            onChannelRequested: function(requestedChannel) {
                module.activeCurveRequested(requestedChannel)
            }
        }
        CurveChannelButton {
            channel: 3
            activeCurve: module.activeCurve
            curveName: module.curveName
            curveColor: module.curveColor
            onChannelRequested: function(requestedChannel) {
                module.activeCurveRequested(requestedChannel)
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            color: module.textColor
            font.pixelSize: 12
            text: module.curveName(module.activeCurve)
        }
        PreferenceButton {
            id: resetCurveButton
            objectName: "resetCurveButton"
            Layout.preferredWidth: 21
            Layout.preferredHeight: 21
            text: "↺"
            font.pixelSize: 14
            padding: 0
            hoverEnabled: true
            enabled: module.editor.hasImage
            Accessible.name: qsTr("Réinitialiser cette courbe")
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Réinitialiser cette courbe")
            onClicked: module.editor.resetToneCurve(module.activeCurve)
            background: Rectangle {
                radius: 3
                color: resetCurveButton.hovered ? resetCurveButton.palette.mid : "transparent"
            }
        }
        PreferenceButton {
            id: resetCurvesButton
            objectName: "resetCurvesButton"
            Layout.preferredHeight: 21
            text: qsTr("Toutes")
            font.pixelSize: 11
            hoverEnabled: true
            enabled: module.editor.hasImage
            Accessible.name: qsTr("Réinitialiser les 4 courbes")
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Réinitialiser les 4 courbes")
            onClicked: module.editor.resetToneCurves()
            background: Rectangle {
                radius: 3
                color: resetCurvesButton.hovered ? resetCurvesButton.palette.mid : "transparent"
                border.width: 1
                border.color: resetCurvesButton.palette.mid
            }
        }
    }
}
