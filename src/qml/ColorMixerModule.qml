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
    readonly property var bandHues: [0, 30, 60, 120, 180, 240, 270, 300]

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
        trackColors: [Qt.hsla(((root.bandHues[root.band] + 330) % 360) / 360, 0.65, 0.55, 1),
                      root.bandColors[root.band],
                      Qt.hsla(((root.bandHues[root.band] + 30) % 360) / 360, 0.65, 0.55, 1)]
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
        trackColors: ["#7d838b", root.bandColors[root.band]]
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
        trackColors: ["#20252c", root.bandColors[root.band], "#f2f2ee"]
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
