import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Point colour, laid out as Lightroom's: pick a colour in the photo, then shift
// its hue, saturation and luminance within a range. The eyedropper puts the
// preview in picking mode (picking); a click there adds a point, a swatch selects
// it, and the sliders edit the selected point.
ColumnLayout {
    id: root

    required property var editor
    property color textColor: "#edf2f8"
    property color secondaryTextColor: "#d7dfeb"
    // The preview picks a colour while this is true; the panel owns the state.
    property bool picking: false
    signal pickingToggled()
    property int selected: -1
    property bool reloading: false
    readonly property var points: editor.pointColors
    readonly property bool hasPoint: selected >= 0 && selected < points.length

    function reload() {
        reloading = true
        const point = hasPoint ? points[selected] : null
        hueControl.value = point ? point.hue : 0
        saturationControl.value = point ? point.saturation : 0
        luminanceControl.value = point ? point.luminance : 0
        varianceControl.value = point ? point.variance : 0
        rangeControl.value = point ? point.range : 50
        reloading = false
    }

    function commit(component, value) {
        if (!reloading && hasPoint)
            editor.setPointColorValue(selected, component, Math.round(value))
    }

    // A new point is selected; a removal or another image keeps a valid selection.
    property int knownCount: 0
    onPointsChanged: {
        if (points.length > knownCount)
            selected = points.length - 1
        else if (selected >= points.length)
            selected = points.length - 1
        knownCount = points.length
        reload()
    }
    onSelectedChanged: reload()

    Layout.fillWidth: true
    Layout.minimumWidth: 0
    implicitWidth: 0
    spacing: 7

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        PreferenceButton {
            id: pickButton
            objectName: "pointColorPicker"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            implicitWidth: 120
            Layout.preferredHeight: 28
            text: root.picking ? qsTr("Cliquez dans la photo (Échap pour annuler)")
                               : qsTr("Sélectionner une couleur dans la photo")
            enabled: root.editor.hasImage && (root.picking || root.points.length < 8)
            Accessible.name: text
            onClicked: root.pickingToggled()

            contentItem: RowLayout {
                spacing: 6
                opacity: pickButton.enabled ? 1 : 0.45
                // Eyedropper.
                Canvas {
                    Layout.preferredWidth: 14
                    Layout.preferredHeight: 14
                    onPaint: {
                        const c = getContext("2d")
                        c.reset()
                        c.strokeStyle = String(root.textColor)
                        c.lineWidth = 1.5
                        c.lineCap = "round"
                        c.beginPath(); c.moveTo(2, 12); c.lineTo(9, 5); c.stroke()
                        c.beginPath(); c.moveTo(8, 3); c.lineTo(11, 6); c.stroke()
                        c.beginPath(); c.arc(11, 3, 2, 0, 2 * Math.PI); c.stroke()
                    }
                }
                Text {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: pickButton.text
                    color: root.textColor
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
            }
            background: Rectangle {
                radius: 3
                color: root.picking ? pickButton.palette.highlight
                       : pickButton.down || pickButton.hovered ? pickButton.palette.mid : pickButton.palette.button
                border.width: 1
                border.color: pickButton.palette.mid
            }
        }
    }

    // Picked colours, one swatch each; the selected one is ringed.
    RowLayout {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        spacing: 6

        Rectangle {
            objectName: "pointColorSwatches"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredHeight: 24
            radius: 3
            color: palette.base
            border.width: 1
            border.color: palette.mid

            Row {
                anchors.fill: parent
                anchors.margins: 3
                spacing: 4

                Repeater {
                    model: root.points

                    Rectangle {
                        required property var modelData
                        required property int index
                        objectName: "pointColorSwatch" + index
                        width: 18
                        height: 18
                        radius: 3
                        color: modelData.colour
                        border.width: root.selected === index ? 2 : 1
                        border.color: root.selected === index ? root.textColor : palette.mid

                        MouseArea {
                            anchors.fill: parent
                            onClicked: root.selected = parent.index
                        }
                    }
                }
            }
            Label {
                anchors.centerIn: parent
                visible: root.points.length === 0
                text: qsTr("Aucune couleur choisie")
                color: palette.windowText
                opacity: 0.6
                font.pixelSize: 11
            }
        }

        PreferenceButton {
            id: removeButton
            objectName: "pointColorRemove"
            Layout.preferredWidth: 24
            Layout.preferredHeight: 24
            text: "−"
            font.pixelSize: 14
            padding: 0
            hoverEnabled: true
            enabled: root.hasPoint
            Accessible.name: qsTr("Retirer la couleur sélectionnée")
            ToolTip.visible: hovered
            ToolTip.text: Accessible.name
            onClicked: root.editor.removePointColor(root.selected)
            background: Rectangle {
                radius: 3
                color: removeButton.hovered ? removeButton.palette.mid : "transparent"
                border.width: 1
                border.color: removeButton.palette.mid
            }
        }
    }

    AdjustmentControl {
        id: hueControl
        label: qsTr("Changement de teinte")
        valueText: Math.round(value)
        minimumValue: -100
        maximumValue: 100
        trackColors: ["#d9566b", "#d8b64a", "#5fb36a", "#4f9fd8", "#a66fd6"]
        adjustmentEnabled: root.hasPoint
        onValueEdited: (value) => root.commit(0, value)
    }
    AdjustmentControl {
        id: saturationControl
        label: qsTr("Changement de saturation")
        valueText: Math.round(value)
        minimumValue: -100
        maximumValue: 100
        trackColors: ["#8b8e94", "#c96b5a"]
        adjustmentEnabled: root.hasPoint
        onValueEdited: (value) => root.commit(1, value)
    }
    AdjustmentControl {
        id: luminanceControl
        label: qsTr("Changement de luminance")
        valueText: Math.round(value)
        minimumValue: -100
        maximumValue: 100
        trackColors: ["#15181d", "#8b8e94", "#f2f4f7"]
        adjustmentEnabled: root.hasPoint
        onValueEdited: (value) => root.commit(2, value)
    }
    AdjustmentControl {
        id: varianceControl
        label: qsTr("Variance")
        valueText: Math.round(value)
        minimumValue: -100
        maximumValue: 100
        adjustmentEnabled: root.hasPoint
        onValueEdited: (value) => root.commit(3, value)
    }
    AdjustmentControl {
        id: rangeControl
        label: qsTr("Gamme")
        valueText: Math.round(value)
        minimumValue: 0
        maximumValue: 100
        value: 50
        resetValue: 50
        adjustmentEnabled: root.hasPoint
        onValueEdited: (value) => root.commit(4, value)
    }

    PreferenceCheckBox {
        objectName: "pointColorRangeOverlay"
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        text: qsTr("Visualisation des plages")
        enabled: false
    }

    SectionNote {
        text: qsTr("Moteur calé sur Lightroom. La visualisation des plages viendra ensuite.")
    }
}
