import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The Geometry tab: every value goes through the engine (ImageEditor::setGeometryValue
// and its neighbours), which bounds it and answers through editor.geometry.
Item {
    id: root
    objectName: "geometryPanel"

    required property var editor
    property var preferences: null
    readonly property color textColor: preferences ? preferences.theme.text : "#edf2f8"
    readonly property color secondaryTextColor: preferences ? preferences.theme.secondaryText : "#d7dee8"
    // The alignment grid over the image, always or while a transform moves; a viewing
    // aid, not part of the recipe.
    readonly property bool transforming: rotationControl.pressed || verticalControl.pressed
        || horizontalControl.pressed || aspectControl.pressed || scaleControl.pressed
        || offsetXControl.pressed || offsetYControl.pressed
    readonly property bool showGrid: gridCheck.checked || transforming
    property bool syncing: false
    readonly property var values: editor.geometry
    readonly property var ratios: [
        { label: qsTr("Libre"), value: 0 },
        { label: qsTr("Original"), value: -1 },
        { label: "1:1", value: 1 },
        { label: "3:2", value: 3 / 2 }, { label: "2:3", value: 2 / 3 },
        { label: "4:3", value: 4 / 3 }, { label: "3:4", value: 3 / 4 },
        { label: "5:4", value: 5 / 4 }, { label: "4:5", value: 4 / 5 },
        { label: "16:9", value: 16 / 9 }, { label: "9:16", value: 9 / 16 }
    ]

    function originalRatio() {
        return values.canvasHeight > 0 ? values.canvasWidth / values.canvasHeight : 0
    }

    function ratioIndex() {
        for (let index = 2; index < ratios.length; ++index)
            if (Math.abs(ratios[index].value - values.cropRatio) < 0.001)
                return index
        if (values.cropRatio > 0 && Math.abs(originalRatio() - values.cropRatio) < 0.001)
            return 1
        return 0
    }

    // Showing the engine's values must not send them back.
    function reload() {
        syncing = true
        rotationControl.value = values.rotation
        verticalControl.value = values.vertical
        horizontalControl.value = values.horizontal
        aspectControl.value = values.aspect
        scaleControl.value = values.scale
        offsetXControl.value = values.offsetX
        offsetYControl.value = values.offsetY
        cropXControl.value = values.cropX * 100
        cropYControl.value = values.cropY * 100
        cropWidthControl.value = values.cropWidth * 100
        cropHeightControl.value = values.cropHeight * 100
        ratioSelector.currentIndex = ratioIndex()
        constrainCheck.checked = values.constrain
        syncing = false
    }

    function sendCrop() {
        if (!syncing)
            editor.setGeometryCrop(cropXControl.value / 100, cropYControl.value / 100,
                                   cropWidthControl.value / 100, cropHeightControl.value / 100)
    }

    function sendValue(name, value) {
        if (!syncing)
            editor.setGeometryValue(name, value)
    }

    Component.onCompleted: reload()
    Connections {
        target: root.editor
        function onGeometryChanged() { root.reload() }
    }

    Label {
        id: title
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        text: qsTr("Géométrie")
        font.bold: true
        font.pixelSize: 18
        color: root.textColor
        padding: 0
        background: Item { }
    }

    ScrollView {
        id: scroll
        objectName: "geometryScrollView"
        anchors.top: title.bottom
        anchors.topMargin: 8
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        rightPadding: 8
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical: PanelScrollBar {
            parent: scroll
            x: scroll.width - width
            y: 0
            height: scroll.height
        }

        ColumnLayout {
            width: scroll.availableWidth
            spacing: 10

            SettingsSection {
                objectName: "geometryOrientationSection"
                textColor: root.textColor
                expanded: true
                title: qsTr("Orientation")

                PreferenceButton {
                    objectName: "geometryTurnButton"
                    text: qsTr("Pivoter de 90° à droite")
                    Layout.fillWidth: true
                    enabled: root.editor.hasImage
                    onClicked: root.editor.turnGeometry()
                }
                RowLayout {
                    Layout.fillWidth: true
                    PreferenceButton {
                        objectName: "geometryMirrorHorizontalButton"
                        text: qsTr("Miroir horizontal")
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        enabled: root.editor.hasImage
                        onClicked: root.editor.mirrorGeometry(true)
                    }
                    PreferenceButton {
                        objectName: "geometryMirrorVerticalButton"
                        text: qsTr("Miroir vertical")
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        enabled: root.editor.hasImage
                        onClicked: root.editor.mirrorGeometry(false)
                    }
                }
            }

            SettingsSection {
                objectName: "geometryCropSection"
                textColor: root.textColor
                expanded: true
                title: qsTr("Recadrage")

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("Format")
                        color: root.secondaryTextColor
                    }
                    PreferenceComboBox {
                        id: ratioSelector
                        objectName: "geometryRatioSelector"
                        Layout.fillWidth: true
                        enabled: root.editor.hasImage
                        model: root.ratios
                        textRole: "label"
                        onActivated: (index) => root.editor.setGeometryCropRatio(
                            root.ratios[index].value < 0 ? root.originalRatio() : root.ratios[index].value)
                    }
                }
                AdjustmentControl {
                    id: cropXControl
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    label: qsTr("Gauche (%)")
                    valueText: value.toFixed(1)
                    maximumValue: 100
                    increment: 0.1
                    adjustmentEnabled: root.editor.hasImage
                    onValueEdited: root.sendCrop()
                }
                AdjustmentControl {
                    id: cropYControl
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    label: qsTr("Haut (%)")
                    valueText: value.toFixed(1)
                    maximumValue: 100
                    increment: 0.1
                    adjustmentEnabled: root.editor.hasImage
                    onValueEdited: root.sendCrop()
                }
                AdjustmentControl {
                    id: cropWidthControl
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    label: qsTr("Largeur (%)")
                    valueText: value.toFixed(1)
                    minimumValue: 0.1
                    maximumValue: 100
                    resetValue: 100
                    increment: 0.1
                    adjustmentEnabled: root.editor.hasImage
                    onValueEdited: root.sendCrop()
                }
                AdjustmentControl {
                    id: cropHeightControl
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    label: qsTr("Hauteur (%)")
                    valueText: value.toFixed(1)
                    minimumValue: 0.1
                    maximumValue: 100
                    resetValue: 100
                    increment: 0.1
                    adjustmentEnabled: root.editor.hasImage
                    onValueEdited: root.sendCrop()
                }
                SectionNote {
                    objectName: "geometryCropSize"
                    text: qsTr("%1 × %2 px")
                        .arg(Math.round(root.values.canvasWidth * root.values.cropWidth))
                        .arg(Math.round(root.values.canvasHeight * root.values.cropHeight))
                }
                PreferenceCheckBox {
                    id: constrainCheck
                    objectName: "geometryConstrainCheck"
                    text: qsTr("Contraindre à l'image")
                    textColor: root.textColor
                    Layout.fillWidth: true
                    enabled: root.editor.hasImage
                    onToggled: root.editor.setGeometryConstrain(checked)
                }
                SectionNote {
                    objectName: "geometryEmptyZonesNote"
                    visible: root.values.emptyZones === true
                    text: qsTr("L'image ne couvre pas tout le cadre : ces zones restent vides, transparentes en PNG et TIFF, noires en JPEG et DNG. Recadrez ou contraignez pour les retirer.")
                }
                PreferenceButton {
                    objectName: "geometryResetCropButton"
                    text: qsTr("Réinitialiser le recadrage")
                    Layout.fillWidth: true
                    enabled: root.editor.hasImage && root.values.hasCrop === true
                    onClicked: root.editor.resetGeometryCrop()
                }
            }

            SettingsSection {
                objectName: "geometryTransformSection"
                textColor: root.textColor
                expanded: true
                title: qsTr("Transformation")

                AdjustmentControl {
                    id: rotationControl
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    label: qsTr("Rotation")
                    valueText: value.toFixed(1) + "°"
                    minimumValue: -45
                    maximumValue: 45
                    increment: 0.1
                    adjustmentEnabled: root.editor.hasImage
                    onValueEdited: root.sendValue("rotation", value)
                }
                AdjustmentControl {
                    id: verticalControl
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    label: qsTr("Vertical")
                    valueText: Math.round(value)
                    minimumValue: -100
                    maximumValue: 100
                    adjustmentEnabled: root.editor.hasImage
                    onValueEdited: root.sendValue("vertical", value)
                }
                AdjustmentControl {
                    id: horizontalControl
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    label: qsTr("Horizontal")
                    valueText: Math.round(value)
                    minimumValue: -100
                    maximumValue: 100
                    adjustmentEnabled: root.editor.hasImage
                    onValueEdited: root.sendValue("horizontal", value)
                }
                AdjustmentControl {
                    id: aspectControl
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    label: qsTr("Aspect")
                    valueText: Math.round(value)
                    minimumValue: -100
                    maximumValue: 100
                    adjustmentEnabled: root.editor.hasImage
                    onValueEdited: root.sendValue("aspect", value)
                }
                AdjustmentControl {
                    id: scaleControl
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    label: qsTr("Échelle (%)")
                    valueText: Math.round(value)
                    minimumValue: 50
                    maximumValue: 150
                    resetValue: 100
                    adjustmentEnabled: root.editor.hasImage
                    onValueEdited: root.sendValue("scale", value)
                }
                AdjustmentControl {
                    id: offsetXControl
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    label: qsTr("Décalage horizontal")
                    valueText: Math.round(value)
                    minimumValue: -100
                    maximumValue: 100
                    adjustmentEnabled: root.editor.hasImage
                    onValueEdited: root.sendValue("offsetX", value)
                }
                AdjustmentControl {
                    id: offsetYControl
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    label: qsTr("Décalage vertical")
                    valueText: Math.round(value)
                    minimumValue: -100
                    maximumValue: 100
                    adjustmentEnabled: root.editor.hasImage
                    onValueEdited: root.sendValue("offsetY", value)
                }
                PreferenceCheckBox {
                    id: gridCheck
                    objectName: "geometryGridCheck"
                    text: qsTr("Toujours afficher la grille")
                    textColor: root.textColor
                    Layout.fillWidth: true
                }
            }

            PreferenceButton {
                objectName: "geometryResetButton"
                text: qsTr("Réinitialiser la géométrie")
                Layout.fillWidth: true
                enabled: root.editor.hasImage && root.values.identity === false
                onClicked: root.editor.resetGeometry()
            }
        }
    }
}
