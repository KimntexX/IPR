import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts

// The Masks tab: every value goes through the engine (editor.*LocalMask*), which bounds it and
// answers through editor.localMasks. A mask is a zone with its own values of the settings,
// added to the global ones: list order organizes masks, without compositing blend modes.
Item {
    id: root
    objectName: "masksPanel"

    required property var editor
    property var preferences: null
    readonly property color textColor: preferences ? preferences.theme.text : "#edf2f8"
    readonly property color secondaryTextColor: preferences ? preferences.theme.secondaryText : "#d7dee8"

    readonly property var masks: editor.localMasks
    property int selectedMask: -1
    property int selectedShape: 0
    readonly property var currentMask: selectedMask >= 0 && selectedMask < masks.length ? masks[selectedMask] : null
    readonly property var currentShape: currentMask && selectedShape >= 0 && selectedShape < currentMask.shapes.length
                                        ? currentMask.shapes[selectedShape] : null
    readonly property string tool: currentShape ? currentShape.kind : ""
    // The next stroke's brush: a tool, not part of the recipe.
    property real brushSize: 0.05
    property real brushFeather: 0.5
    property real brushFlow: 1
    property alias showZone: zoneCheck.checked
    property bool syncing: false
    property int draggedMask: -1
    property int dropMask: -1

    function moveMask(from, to) {
        const selected = selectedMask
        const shape = selectedShape
        if (!editor.moveLocalMask(from, to))
            return
        if (selected === from)
            selectedMask = to
        else if (from < selected && selected <= to)
            selectedMask = selected - 1
        else if (to <= selected && selected < from)
            selectedMask = selected + 1
        selectedShape = shape
    }

    function updateDrop(y) {
        let distance = Infinity
        dropMask = -1
        for (let i = 0; i < maskRows.count; ++i) {
            const row = maskRows.itemAt(i)
            const delta = row ? Math.abs(y - row.mapToItem(root, 0, row.height / 2).y) : Infinity
            if (delta < distance) {
                distance = delta
                dropMask = i
            }
        }
    }

    AppMenu {
        id: orderMenu
        parent: scroll
        objectName: "maskOrderMenu"
        property int maskIndex: -1
        ActionMenuItem {
            objectName: "maskMoveForward"
            text: qsTr("Passer devant")
            enabled: orderMenu.maskIndex > 0 && orderMenu.maskIndex < root.masks.length
            onTriggered: root.moveMask(orderMenu.maskIndex, orderMenu.maskIndex - 1)
        }
        ActionMenuItem {
            objectName: "maskMoveBackward"
            text: qsTr("Passer derrière")
            enabled: orderMenu.maskIndex >= 0 && orderMenu.maskIndex < root.masks.length - 1
            onTriggered: root.moveMask(orderMenu.maskIndex, orderMenu.maskIndex + 1)
        }
    }

    readonly property var kinds: [
        { key: "brush", label: qsTr("Pinceau") }, { key: "linear", label: qsTr("Dégradé linéaire") },
        { key: "radial", label: qsTr("Dégradé radial") }, { key: "luminanceRange", label: qsTr("Plage") },
        { key: "triangle", label: qsTr("Cône triangulaire") }, { key: "trapezoid", label: qsTr("Cône trapézoïdal") }
    ]
    readonly property var combinations: [
        { key: "add", label: qsTr("Ajouter") }, { key: "subtract", label: qsTr("Soustraire") },
        { key: "intersect", label: qsTr("Intersecter") }
    ]
    readonly property var groups: [
        { title: qsTr("Lumière"), settings: [
            { key: "exposure", label: qsTr("Exposition"), low: -5, high: 5, step: 0.01, digits: 2 },
            { key: "contrast", label: qsTr("Contraste"), low: -100, high: 100, step: 1, digits: 0 },
            { key: "highlights", label: qsTr("Hautes lumières"), low: -100, high: 100, step: 1, digits: 0 },
            { key: "shadows", label: qsTr("Ombres"), low: -100, high: 100, step: 1, digits: 0 },
            { key: "whites", label: qsTr("Blancs"), low: -100, high: 100, step: 1, digits: 0 },
            { key: "blacks", label: qsTr("Noirs"), low: -100, high: 100, step: 1, digits: 0 } ] },
        { title: qsTr("Couleur"), settings: [
            { key: "temperature", label: qsTr("Température"), low: -3000, high: 3000, step: 10, digits: 0,
              track: ["#477dda", "#c4c6c4", "#e8ae4e"] },
            { key: "tint", label: qsTr("Nuance"), low: -150, high: 150, step: 1, digits: 0,
              track: ["#63b777", "#c4c6c4", "#c46eb5"] },
            { key: "vibrance", label: qsTr("Vibrance"), low: -100, high: 100, step: 1, digits: 0,
              track: ["#78828b", "#719e9e", "#b4b93c", "#dc6845"] },
            { key: "saturation", label: qsTr("Saturation"), low: -100, high: 100, step: 1, digits: 0,
              track: ["#78828b", "#78a5a4", "#5cae6c", "#d2c247", "#db6559"] } ] },
        { title: qsTr("Effets"), settings: [
            { key: "texture", label: qsTr("Texture"), low: -100, high: 100, step: 1, digits: 0 },
            { key: "clarity", label: qsTr("Clarté"), low: -100, high: 100, step: 1, digits: 0 },
            { key: "dehaze", label: qsTr("Correction du voile"), low: -100, high: 100, step: 1, digits: 0 } ] },
        { title: qsTr("Détail"), settings: [
            { key: "denoiseLuminance", label: qsTr("Bruit de luminance"), low: -100, high: 100, step: 1, digits: 0 },
            { key: "denoiseChrominance", label: qsTr("Bruit de couleur"), low: -100, high: 100, step: 1, digits: 0 },
            { key: "sharpening", label: qsTr("Netteté"), low: -200, high: 200, step: 1, digits: 0 } ] }
    ]

    function addMask(kind) {
        const index = editor.addLocalMask(kind)
        if (index >= 0) {
            selectedMask = index
            selectedShape = 0
        }
    }

    function duplicateMask(index) {
        const copy = editor.duplicateLocalMask(index)
        if (copy >= 0)
            selectedMask = copy
    }

    function addShape(kind, combine) {
        const index = editor.addLocalMaskShape(selectedMask, kind, combine)
        if (index >= 0)
            selectedShape = index
    }

    // What the viewport draws, already on the normalized canvas.
    function applyStroke(points) {
        if (tool === "brush")
            editor.addLocalMaskStroke(selectedMask, selectedShape, points, brushSize, brushFeather, brushFlow)
    }

    function applyShape(values) {
        if (currentShape)
            editor.setLocalMaskShape(selectedMask, selectedShape, values)
    }

    function sendShapeField(name, value) {
        if (!syncing)
            applyShape({ [name]: value })
    }

    // Showing the engine's values must not send them back.
    function reload() {
        syncing = true
        opacityControl.value = currentMask ? currentMask.opacity * 100 : 100
        const shape = currentShape
        if (shape) {
            featherControl.value = shape.feather * 100
            angleControl.value = shape.angle
            startWidthControl.value = shape.startWidth * 100
            endWidthControl.value = shape.endWidth * 100
            diffusionControl.value = shape.diffusion * 100
            rangeLowControl.value = shape.rangeLow * 100
            rangeHighControl.value = shape.rangeHigh * 100
            rangeFeatherControl.value = shape.rangeFeather * 100
            invertedCheck.checked = shape.inverted
        }
        syncing = false
        syncValues()
    }

    function syncValues() {
        syncing = true
        for (let g = 0; g < groupRepeater.count; ++g) {
            const group = groupRepeater.itemAt(g)
            if (group)
                group.reload()
        }
        syncing = false
    }

    // Undo can remove the mask or the shape in hand. Later, because the properties read from
    // masks follow it only after this handler.
    function followEngine() {
        if (selectedMask >= masks.length)
            selectedMask = masks.length - 1
        if (currentMask && selectedShape >= currentMask.shapes.length)
            selectedShape = Math.max(0, currentMask.shapes.length - 1)
        reload()
    }
    onMasksChanged: Qt.callLater(followEngine)
    onSelectedMaskChanged: Qt.callLater(reload)
    onSelectedShapeChanged: Qt.callLater(reload)
    Component.onCompleted: reload()

    // The zone shows only while this tab is open, on demand or while a range slider is held:
    // what those sliders select is hard to judge from the edit alone.
    readonly property bool rangeSliderHeld: rangeLowControl.pressed || rangeHighControl.pressed || rangeFeatherControl.pressed
    Binding {
        target: root.editor
        property: "localMaskOverlay"
        value: root.visible && (zoneCheck.checked || root.rangeSliderHeld) ? root.selectedMask : -1
    }

    Label {
        id: title
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        text: qsTr("Masques")
        font.bold: true
        font.pixelSize: 18
        color: root.textColor
        padding: 0
        background: Item { }
    }

    ScrollView {
        id: scroll
        objectName: "masksScrollView"
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

            SectionNote {
                text: qsTr("Chaque masque porte ses propres réglages, ajoutés aux réglages globaux dans sa zone.")
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Nouveau masque")
                font.bold: true
                color: root.secondaryTextColor
                padding: 0
                background: Item { }
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 6
                rowSpacing: 6
                Repeater {
                    model: root.kinds
                    PreferenceButton {
                        required property var modelData
                        objectName: "maskNew_" + modelData.key
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        text: modelData.label
                        enabled: root.editor.hasImage
                        onClicked: root.addMask(modelData.key)
                    }
                }
            }

            // The masks of the photo. A row follows the count, not the values: a slider must not
            // rebuild the list, nor a click remove the button it runs in.
            Repeater {
                id: maskRows
                model: root.masks.length
                RowLayout {
                    id: maskRow
                    required property int index
                    readonly property var modelData: root.masks[index] || ({})
                    objectName: "maskRow_" + index
                    Layout.fillWidth: true
                    spacing: 4
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: (eventPoint) => {
                            orderMenu.maskIndex = maskRow.index
                            orderMenu.popup(maskRow, eventPoint.position.x, eventPoint.position.y)
                        }
                    }
                    Rectangle {
                        Layout.preferredWidth: 22
                        Layout.fillHeight: true
                        color: root.dropMask === maskRow.index ? dragGlyph.palette.highlight : "transparent"
                        radius: 3
                        Label {
                            id: dragGlyph
                            anchors.centerIn: parent
                            text: "⠿"
                            color: root.textColor
                        }
                        MouseArea {
                            objectName: "maskDrag_" + maskRow.index
                            anchors.fill: parent
                            preventStealing: true
                            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                            property real pressY: 0
                            onPressed: (mouse) => { pressY = mouse.y }
                            onPositionChanged: (mouse) => {
                                if (pressed && (root.draggedMask >= 0 || Math.abs(mouse.y - pressY) > 6)) {
                                    root.draggedMask = maskRow.index
                                    root.updateDrop(mapToItem(root, mouse.x, mouse.y).y)
                                }
                            }
                            onReleased: {
                                const from = root.draggedMask
                                const to = root.dropMask
                                root.draggedMask = -1
                                root.dropMask = -1
                                if (from >= 0 && to >= 0)
                                    root.moveMask(from, to)
                            }
                            onCanceled: { root.draggedMask = -1; root.dropMask = -1 }
                            Accessible.name: qsTr("Déplacer %1").arg(maskRow.modelData.name)
                        }
                    }
                    PreferenceCheckBox {
                        objectName: "maskVisible_" + maskRow.index
                        textColor: root.textColor
                        checked: maskRow.modelData.visible
                        onToggled: root.editor.setLocalMaskVisible(maskRow.index, checked)
                        Accessible.name: qsTr("Afficher %1").arg(maskRow.modelData.name)
                    }
                    PreferenceButton {
                        objectName: "maskSelect_" + maskRow.index
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: maskRow.modelData.name
                        font.bold: maskRow.index === root.selectedMask
                        onClicked: {
                            root.selectedMask = maskRow.index
                            root.selectedShape = 0
                        }
                    }
                    PreferenceButton {
                        objectName: "maskDuplicate_" + maskRow.index
                        text: "⧉"
                        Layout.preferredWidth: 30
                        Accessible.name: qsTr("Dupliquer %1").arg(maskRow.modelData.name)
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Dupliquer")
                        hoverEnabled: true
                        onClicked: root.duplicateMask(maskRow.index)
                    }
                    PreferenceButton {
                        objectName: "maskRemove_" + maskRow.index
                        text: "✕"
                        Layout.preferredWidth: 30
                        Accessible.name: qsTr("Supprimer %1").arg(maskRow.modelData.name)
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Supprimer")
                        hoverEnabled: true
                        onClicked: root.editor.removeLocalMask(maskRow.index)
                    }
                }
            }

            // The mask in hand.
            ColumnLayout {
                objectName: "maskDetails"
                Layout.fillWidth: true
                spacing: 8
                visible: root.currentMask !== null

                Basic.TextField {
                    id: nameField
                    objectName: "maskName"
                    Layout.fillWidth: true
                    text: root.currentMask ? root.currentMask.name : ""
                    color: root.textColor
                    selectByMouse: true
                    Accessible.name: qsTr("Nom du masque")
                    onEditingFinished: root.editor.setLocalMaskName(root.selectedMask, text)
                    background: Rectangle {
                        radius: 3
                        color: nameField.activeFocus ? nameField.palette.base : "transparent"
                        border.width: 1
                        border.color: nameField.activeFocus ? nameField.palette.text : nameField.palette.mid
                    }
                }
                PreferenceCheckBox {
                    id: zoneCheck
                    objectName: "maskShowZone"
                    Layout.fillWidth: true
                    text: qsTr("Afficher la zone")
                    textColor: root.textColor
                }
                AdjustmentControl {
                    id: opacityControl
                    objectName: "maskOpacity"
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    label: qsTr("Opacité (%)")
                    valueText: Math.round(value)
                    maximumValue: 100
                    resetValue: 100
                    adjustmentEnabled: root.currentMask !== null
                    onValueEdited: if (!root.syncing) root.editor.setLocalMaskOpacity(root.selectedMask, value / 100)
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Zone")
                    font.bold: true
                    color: root.secondaryTextColor
                    padding: 0
                    background: Item { }
                }
                Repeater {
                    model: root.currentMask ? root.currentMask.shapes.length : 0
                    RowLayout {
                        id: shapeRow
                        required property int index
                        readonly property var modelData: (root.currentMask && root.currentMask.shapes[index]) || ({})
                        Layout.fillWidth: true
                        spacing: 4
                        PreferenceButton {
                            objectName: "maskShape_" + shapeRow.index
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            font.bold: shapeRow.index === root.selectedShape
                            text: {
                                const kind = root.kinds.find(k => k.key === shapeRow.modelData.kind)
                                const combine = root.combinations.find(c => c.key === shapeRow.modelData.combine)
                                return (shapeRow.index > 0 && combine ? combine.label + " : " : "") + (kind ? kind.label : "")
                            }
                            onClicked: root.selectedShape = shapeRow.index
                        }
                        PreferenceButton {
                            objectName: "maskShapeRemove_" + shapeRow.index
                            text: "✕"
                            Layout.preferredWidth: 30
                            enabled: root.currentMask && root.currentMask.shapes.length > 1
                            Accessible.name: qsTr("Retirer cette forme")
                            onClicked: root.editor.removeLocalMaskShape(root.selectedMask, shapeRow.index)
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    PreferenceComboBox {
                        id: combineSelector
                        objectName: "maskCombine"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        model: root.combinations
                        textRole: "label"
                    }
                    PreferenceComboBox {
                        id: kindSelector
                        objectName: "maskKind"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        model: root.kinds
                        textRole: "label"
                    }
                }
                PreferenceButton {
                    objectName: "maskAddShape"
                    Layout.fillWidth: true
                    text: qsTr("Ajouter à la zone")
                    onClicked: root.addShape(root.kinds[kindSelector.currentIndex].key,
                                             root.combinations[combineSelector.currentIndex].key)
                }

                // The shape in hand: a brush paints on the image, the gradients have handles there.
                SectionNote {
                    objectName: "maskToolHint"
                    text: root.tool === "brush" ? qsTr("Peignez sur l'image.")
                        : root.tool === "linear" ? qsTr("Déplacez les deux poignées sur l'image : effet entier avant la première, aucun après la seconde.")
                        : root.tool === "radial" ? qsTr("Déplacez le centre et les bords sur l'image.")
                        : root.tool === "triangle" || root.tool === "trapezoid" ? qsTr("Déplacez les extrémités pour orienter le cône et les poignées latérales pour l’élargir. Les largeurs sont relatives au grand côté de l’image.")
                        : root.tool === "luminanceRange" ? qsTr("Choisit les tons de l'image avant toute retouche.") : ""
                }
                AdjustmentControl {
                    objectName: "maskBrushSize"
                    visible: root.tool === "brush"
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    label: qsTr("Taille (%)")
                    valueText: value.toFixed(1)
                    minimumValue: 0.2
                    maximumValue: 30
                    resetValue: 5
                    increment: 0.1
                    value: root.brushSize * 100
                    adjustmentEnabled: true
                    onValueEdited: root.brushSize = value / 100
                }
                AdjustmentControl {
                    objectName: "maskBrushFeather"
                    visible: root.tool === "brush"
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    label: qsTr("Contour progressif (%)")
                    valueText: Math.round(value)
                    maximumValue: 100
                    resetValue: 50
                    value: root.brushFeather * 100
                    adjustmentEnabled: true
                    onValueEdited: root.brushFeather = value / 100
                }
                AdjustmentControl {
                    objectName: "maskBrushFlow"
                    visible: root.tool === "brush"
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    label: qsTr("Débit (%)")
                    valueText: Math.round(value)
                    minimumValue: 1
                    maximumValue: 100
                    resetValue: 100
                    value: root.brushFlow * 100
                    adjustmentEnabled: true
                    onValueEdited: root.brushFlow = value / 100
                }
                AdjustmentControl {
                    id: startWidthControl
                    objectName: "maskStartWidth"
                    visible: root.tool === "trapezoid"
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    label: qsTr("Largeur au départ (%)")
                    minimumValue: 0; maximumValue: 800; resetValue: 12
                    valueText: value.toFixed(1)
                    adjustmentEnabled: root.currentShape !== null
                    onValueEdited: root.sendShapeField("startWidth", value / 100)
                }
                AdjustmentControl {
                    id: endWidthControl
                    objectName: "maskEndWidth"
                    visible: root.tool === "triangle" || root.tool === "trapezoid"
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    label: qsTr("Largeur à l’arrivée (%)")
                    minimumValue: 0; maximumValue: 800; resetValue: 40
                    valueText: value.toFixed(1)
                    adjustmentEnabled: root.currentShape !== null
                    onValueEdited: root.sendShapeField("endWidth", value / 100)
                }
                AdjustmentControl {
                    id: diffusionControl
                    objectName: "maskDiffusion"
                    visible: root.tool === "triangle" || root.tool === "trapezoid"
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    label: qsTr("Diffusion du spot (%)")
                    minimumValue: 0; maximumValue: 100; resetValue: 70
                    valueText: Math.round(value)
                    adjustmentEnabled: root.currentShape !== null
                    onValueEdited: root.sendShapeField("diffusion", value / 100)
                }
                AdjustmentControl {
                    id: featherControl
                    objectName: "maskFeather"
                    visible: root.tool === "radial" || root.tool === "triangle" || root.tool === "trapezoid"
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    label: qsTr("Contour progressif (%)")
                    valueText: Math.round(value)
                    maximumValue: 100
                    resetValue: 50
                    adjustmentEnabled: true
                    onValueEdited: root.sendShapeField("feather", value / 100)
                }
                AdjustmentControl {
                    id: angleControl
                    objectName: "maskAngle"
                    visible: root.tool === "radial"
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    label: qsTr("Angle")
                    valueText: Math.round(value) + "°"
                    minimumValue: -90
                    maximumValue: 90
                    adjustmentEnabled: true
                    onValueEdited: root.sendShapeField("angle", value)
                }
                AdjustmentControl {
                    id: rangeLowControl
                    objectName: "maskRangeLow"
                    visible: root.tool === "luminanceRange"
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    label: qsTr("Tons sombres (%)")
                    valueText: Math.round(value)
                    maximumValue: 100
                    adjustmentEnabled: true
                    onValueEdited: root.sendShapeField("rangeLow", value / 100)
                }
                AdjustmentControl {
                    id: rangeHighControl
                    objectName: "maskRangeHigh"
                    visible: root.tool === "luminanceRange"
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    label: qsTr("Tons clairs (%)")
                    valueText: Math.round(value)
                    maximumValue: 100
                    resetValue: 100
                    adjustmentEnabled: true
                    onValueEdited: root.sendShapeField("rangeHigh", value / 100)
                }
                AdjustmentControl {
                    id: rangeFeatherControl
                    objectName: "maskRangeFeather"
                    visible: root.tool === "luminanceRange"
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    label: qsTr("Lissage (%)")
                    valueText: Math.round(value)
                    maximumValue: 100
                    resetValue: 10
                    adjustmentEnabled: true
                    onValueEdited: root.sendShapeField("rangeFeather", value / 100)
                }
                PreferenceCheckBox {
                    id: invertedCheck
                    objectName: "maskInverted"
                    Layout.fillWidth: true
                    text: qsTr("Inverser")
                    textColor: root.textColor
                    onToggled: root.sendShapeField("inverted", checked)
                }

                // Its values, added to the global settings in the zone.
                Repeater {
                    id: groupRepeater
                    model: root.groups
                    ColumnLayout {
                        id: group
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 6
                        function reload() {
                            for (let i = 0; i < settingRepeater.count; ++i) {
                                const control = settingRepeater.itemAt(i)
                                if (control)
                                    control.value = root.currentMask ? root.currentMask.values[control.modelData.key] : 0
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: group.modelData.title
                            font.bold: true
                            color: root.secondaryTextColor
                            padding: 0
                            background: Item { }
                        }
                        Repeater {
                            id: settingRepeater
                            model: group.modelData.settings
                            onItemAdded: root.syncValues()
                            AdjustmentControl {
                                required property var modelData
                                objectName: "maskValue_" + modelData.key
                                textColor: root.textColor
                                secondaryTextColor: root.secondaryTextColor
                                label: modelData.label
                                valueText: value.toFixed(modelData.digits)
                                minimumValue: modelData.low
                                maximumValue: modelData.high
                                increment: modelData.step
                                // The same colours as the Settings tab's sliders.
                                trackColors: modelData.track || []
                                adjustmentEnabled: root.currentMask !== null
                                onValueEdited: if (!root.syncing) root.editor.setLocalMaskValue(root.selectedMask, modelData.key, value)
                            }
                        }
                    }
                }
            }
        }
    }
}
