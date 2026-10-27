import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts

ColumnLayout {
    id: root

    required property var editor
    property bool showTitle: true
    property int band: 0
    property bool reloading: false
    readonly property var bandNames: [qsTr("Rouges"), qsTr("Oranges"), qsTr("Jaunes"),
                                      qsTr("Verts"), qsTr("Cyans"),
                                      qsTr("Bleus"), qsTr("Violets"), qsTr("Magentas")]
    readonly property var bandColors: ["#e55353", "#ed923b", "#e6c34b", "#70bd66",
                                       "#40b8c2", "#5d8ee8", "#8e63d8", "#df65b5"]
    // Slider tracks: the colours each slider of the band reaches from -100 to
    // +100, computed by the engine from the measured Lightroom mixer model.
    readonly property var trackValues: [-100, -50, 0, 50, 100]
    function track(component) {
        return trackValues.map(value => editor.colorMixerTrackColor(band, component, value))
    }

    function reload() {
        reloading = true
        hueControl.value = editor.colorMixerValue(band, 0)
        saturationControl.value = editor.colorMixerValue(band, 1)
        luminanceControl.value = editor.colorMixerValue(band, 2)
        reloading = false
    }

    function selectBand(index) {
        if (index < 0 || index >= bandNames.length)
            return
        root.band = index
        root.reload()
    }

    function commit() {
        if (reloading)
            return
        editor.setColorMixer(band, Math.round(hueControl.value),
                             Math.round(saturationControl.value),
                             Math.round(luminanceControl.value))
    }

    Layout.fillWidth: true
    spacing: 6

    Label {
        visible: root.showTitle
        text: qsTr("Mélangeur de couleurs")
        color: palette.windowText
        font.bold: true
        font.pixelSize: 14
    }

    // Reserve the selector's height independently of its buttons. Deriving a
    // RowLayout's preferred height from its own negotiated width can collapse
    // it to zero when the settings section starts hidden.
    Item {
        id: swatchContainer
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        implicitHeight: 27

        Row {
            id: swatchRow
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4

            Repeater {
                model: root.bandNames.length

                Basic.ToolButton {
                    id: swatchButton

                    required property int index
                    objectName: "mixerBand" + index
                    readonly property real diameter: Math.min(27, Math.max(0,
                        (swatchContainer.width - (root.bandNames.length - 1) * swatchRow.spacing)
                        / root.bandNames.length))
                    width: diameter
                    height: diameter
                    enabled: root.editor.hasImage
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus
                    Accessible.name: root.bandNames[index]
                    ToolTip.visible: hovered
                    ToolTip.text: root.bandNames[index]
                    onClicked: root.selectBand(index)

                    contentItem: Item { }
                    background: Rectangle {
                        radius: width / 2
                        color: root.bandColors[swatchButton.index]
                        // One ring: thin, or thicker and light on the selected band.
                        border.width: root.band === swatchButton.index ? 2 : 1
                        border.color: root.band === swatchButton.index ? swatchButton.palette.text : swatchButton.palette.mid
                        opacity: swatchButton.enabled ? 1 : 0.35
                    }
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true

        Label {
            objectName: "mixerBandName"
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("%1 · %2 · %3 · %4").arg(root.bandNames[root.band]).arg(Math.round(hueControl.value))
                  .arg(Math.round(saturationControl.value)).arg(Math.round(luminanceControl.value))
            color: palette.windowText
            font.pixelSize: 12
        }

        PreferenceButton {
            id: resetBandButton
            Layout.preferredWidth: 21
            Layout.preferredHeight: 21
            text: "↺"
            font.pixelSize: 14
            padding: 0
            hoverEnabled: true
            enabled: root.editor.hasImage
            Accessible.name: qsTr("Réinitialiser la bande")
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Réinitialiser la bande %1").arg(root.bandNames[root.band])
            onClicked: {
                root.editor.setColorMixer(root.band, 0, 0, 0)
                root.reload()
            }

            background: Rectangle {
                radius: 3
                color: resetBandButton.hovered ? resetBandButton.palette.mid : "transparent"
            }
        }
    }

    AdjustmentControl {
        id: hueControl
        trackColors: root.track(0)
        Layout.fillWidth: true
        label: qsTr("Teinte")
        valueText: Math.round(value)
        minimumValue: -100
        maximumValue: 100
        adjustmentEnabled: root.editor.hasImage
        onValueEdited: root.commit()
    }

    AdjustmentControl {
        id: saturationControl
        trackColors: root.track(1)
        Layout.fillWidth: true
        label: qsTr("Saturation %1").arg(root.bandNames[root.band])
        valueText: Math.round(value)
        minimumValue: -100
        maximumValue: 100
        adjustmentEnabled: root.editor.hasImage
        onValueEdited: root.commit()
    }

    AdjustmentControl {
        id: luminanceControl
        trackColors: root.track(2)
        Layout.fillWidth: true
        label: qsTr("Luminance %1").arg(root.bandNames[root.band])
        valueText: Math.round(value)
        minimumValue: -100
        maximumValue: 100
        adjustmentEnabled: root.editor.hasImage
        onValueEdited: root.commit()
    }

    Component.onCompleted: reload()
}
