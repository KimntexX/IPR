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
    // Slider tracks: the colour each slider reaches at -100 and +100, measured on
    // the Lightroom mixer (scripts/mixer_track_colours.py from the model
    // modele-melangeur-lr-v001); regenerate them with the model.
    readonly property var centreColors: ["#e07080", "#df784d", "#bc9202", "#76aa49", "#08b09f", "#24a3e3", "#a384e3", "#d273b0"]
    readonly property var hueLowColors: ["#e265a1", "#ef4772", "#e1603a", "#b3994e", "#4bb067", "#07c6ac", "#629ce0", "#b47dc9"]
    readonly property var hueHighColors: ["#d5926f", "#bd9f37", "#6caa02", "#05ab84", "#5989b7", "#b077e9", "#e071dc", "#d47893"]
    readonly property var saturationLowColors: ["#a39d9d", "#999391", "#8e8b87", "#8e918b", "#8f9694", "#a3a7ab", "#a89ec1", "#a29d9f"]
    readonly property var saturationHighColors: ["#ea084d", "#d76a0b", "#b49305", "#6aa80a", "#04ad9c", "#0992c4", "#9858f9", "#db00aa"]
    readonly property var luminanceLowColors: ["#991b39", "#a34302", "#7f6404", "#497a02", "#008072", "#006185", "#764abf", "#932473"]
    readonly property var luminanceHighColors: ["#ffa6b1", "#fda07d", "#dbb356", "#9ac877", "#56d6c4", "#8bcffe", "#c2a9fb", "#f3a4d4"]

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
                        border.width: root.band === swatchButton.index ? 3 : 1
                        border.color: root.band === swatchButton.index ? swatchButton.palette.text : swatchButton.palette.mid
                        opacity: swatchButton.enabled ? 1 : 0.35

                        Rectangle {
                            anchors.centerIn: parent
                            width: parent.width - 9
                            height: width
                            radius: width / 2
                            color: "transparent"
                            border.width: 1
                            border.color: "#ffffff"
                            opacity: root.band === swatchButton.index ? 0.65 : 0.18
                        }
                    }
                }
            }
        }
    }

    Label {
        text: root.bandNames[root.band]
        color: root.bandColors[root.band]
        font.pixelSize: 12
        font.bold: true
    }

    AdjustmentControl {
        id: hueControl
        trackColors: [root.hueLowColors[root.band], root.centreColors[root.band],
                      root.hueHighColors[root.band]]
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
        trackColors: [root.saturationLowColors[root.band], root.saturationHighColors[root.band]]
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
        trackColors: [root.luminanceLowColors[root.band], root.centreColors[root.band],
                      root.luminanceHighColors[root.band]]
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
