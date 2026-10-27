import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Point colour, laid out as Lightroom's: pick a colour in the photo, then shift
// its hue, saturation and luminance within a range. Interface only: the engine
// has no point colour yet (docs/architecture/suivi-raccordements-ihm.md), so the
// controls stay disabled and say so.
ColumnLayout {
    id: root

    required property var editor
    property color textColor: "#edf2f8"
    property color secondaryTextColor: "#d7dfeb"
    readonly property bool connected: false

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
            text: qsTr("Sélectionner une couleur dans la photo")
            enabled: root.connected && root.editor.hasImage
            Accessible.name: text

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
        }
    }

    // Picked colours appear here, one swatch each.
    Rectangle {
        objectName: "pointColorSwatches"
        Layout.fillWidth: true
        Layout.preferredHeight: 22
        radius: 3
        color: palette.base
        border.width: 1
        border.color: palette.mid
    }

    AdjustmentControl {
        label: qsTr("Changement de teinte")
        valueText: Math.round(value)
        minimumValue: -100
        maximumValue: 100
        trackColors: ["#d9566b", "#d8b64a", "#5fb36a", "#4f9fd8", "#a66fd6"]
        adjustmentEnabled: root.connected
    }
    AdjustmentControl {
        label: qsTr("Changement de saturation")
        valueText: Math.round(value)
        minimumValue: -100
        maximumValue: 100
        trackColors: ["#8b8e94", "#c96b5a"]
        adjustmentEnabled: root.connected
    }
    AdjustmentControl {
        label: qsTr("Changement de luminance")
        valueText: Math.round(value)
        minimumValue: -100
        maximumValue: 100
        trackColors: ["#15181d", "#8b8e94", "#f2f4f7"]
        adjustmentEnabled: root.connected
    }
    AdjustmentControl {
        label: qsTr("Variance")
        valueText: Math.round(value)
        minimumValue: -100
        maximumValue: 100
        adjustmentEnabled: root.connected
    }
    AdjustmentControl {
        label: qsTr("Gamme")
        valueText: Math.round(value)
        minimumValue: 0
        maximumValue: 100
        value: 50
        resetValue: 50
        adjustmentEnabled: root.connected
    }

    PreferenceCheckBox {
        objectName: "pointColorRangeOverlay"
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        text: qsTr("Visualisation des plages")
        enabled: root.connected
    }

    SectionNote {
        text: qsTr("À venir : le moteur de la couleur du point sera calé sur des exports Lightroom, comme le mélangeur.")
    }
}
