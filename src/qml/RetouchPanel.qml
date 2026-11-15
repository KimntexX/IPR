import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The Retouch tab: removes what should not be in the photo. Every value goes through the engine
// (editor.*Retouch*), which answers through editor.retouches; nothing is computed here. A retouch
// never writes to the photo: it is worked out at each render, and can be changed or removed.
Item {
    id: root
    objectName: "retouchPanel"

    required property var editor
    property var preferences: null
    readonly property color textColor: preferences ? preferences.theme.text : "#edf2f8"
    readonly property color secondaryTextColor: preferences ? preferences.theme.secondaryText : "#d7dee8"

    readonly property var retouches: editor.retouches
    property int selected: -1
    readonly property var current: selected >= 0 && selected < retouches.length ? retouches[selected] : null
    readonly property bool hasSource: current !== null && current.kind !== "remove"
    // The next stroke's brush: a tool, not part of the recipe.
    property real brushSize: 0.03
    property real brushFeather: 0.3
    property alias showZone: zoneCheck.checked
    property alias placingSource: sourceCheck.checked
    property bool syncing: false

    readonly property var kinds: [
        { key: "remove", label: qsTr("Effacer"),
          note: qsTr("Peignez ce qui doit disparaître : la zone est inventée à partir de ce qui l'entoure. Convaincant sur un ciel ou une texture régulière, à vérifier ailleurs.") },
        { key: "heal", label: qsTr("Corriger"),
          note: qsTr("Peignez la zone : elle prend la texture d'un autre endroit, à la lumière de ses alentours. Idéal pour une poussière ou un petit défaut.") },
        { key: "clone", label: qsTr("Tampon"),
          note: qsTr("Peignez la zone : elle reçoit telle quelle une copie d'un autre endroit, que vous pouvez déplacer.") }
    ]
    function labelOf(kind) {
        for (const entry of kinds) {
            if (entry.key === kind)
                return entry.label
        }
        return ""
    }
    function noteOf(kind) {
        for (const entry of kinds) {
            if (entry.key === kind)
                return entry.note
        }
        return ""
    }

    // What the viewport shows and paints: the brush, or the source's handle. The handle's model
    // depends on the kind alone, so that moving it does not recreate it under the hand.
    readonly property var currentShape: {
        if (current === null)
            return null
        if (placingSource && hasSource)
            return { kind: "point", center: [current.centerX + current.sourceX, current.centerY + current.sourceY] }
        return { kind: "brush" }
    }

    function addRetouch(kind) {
        const index = editor.addRetouch(kind)
        if (index >= 0) {
            selected = index
            placingSource = false
        }
    }

    function removeRetouch(index) {
        editor.removeRetouch(index)
        if (selected >= retouches.length)
            selected = retouches.length - 1
    }

    // What the viewport draws, already on the normalized canvas.
    function applyStroke(points) {
        if (current !== null)
            editor.addRetouchStroke(selected, points, brushSize, brushFeather)
    }

    function applyShape(values) {
        if (current !== null && values.center !== undefined)
            editor.setRetouchSource(selected, values.center[0] - current.centerX, values.center[1] - current.centerY)
    }

    // An undo can take away the retouch in hand.
    onRetouchesChanged: Qt.callLater(function () {
        if (root.selected >= root.retouches.length)
            root.selected = root.retouches.length - 1
        root.syncing = true
        opacityControl.value = root.current ? root.current.opacity * 100 : 100
        root.syncing = false
    })
    onSelectedChanged: Qt.callLater(function () {
        root.syncing = true
        opacityControl.value = root.current ? root.current.opacity * 100 : 100
        root.syncing = false
    })

    // The zone shows only while this tab is open, and on demand.
    Binding {
        target: root.editor
        property: "retouchOverlay"
        value: root.visible && zoneCheck.checked ? root.selected : -1
    }

    Label {
        id: title
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        text: qsTr("Retouche")
        font.bold: true
        font.pixelSize: 18
        color: root.textColor
        padding: 0
        background: Item { }
    }

    ScrollView {
        id: scroll
        objectName: "retouchScrollView"
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
                text: qsTr("Retire ce qui ne doit pas être dans la photo. Ce qui remplace la zone est reconstruit, pas retrouvé. La photo d'origine ne change pas, et chaque retouche peut être modifiée ou retirée.")
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Nouvelle retouche")
                font.bold: true
                color: root.secondaryTextColor
                padding: 0
                background: Item { }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: root.kinds
                    PreferenceButton {
                        required property var modelData
                        objectName: "retouchNew_" + modelData.key
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        text: modelData.label
                        enabled: root.editor.hasImage
                        onClicked: root.addRetouch(modelData.key)
                    }
                }
            }

            // A row follows the count, not the values: a slider must not rebuild the list, nor a
            // click remove the button it runs in.
            Repeater {
                model: root.retouches.length
                RowLayout {
                    id: retouchRow
                    required property int index
                    readonly property var modelData: root.retouches[index] || ({})
                    objectName: "retouchRow_" + index
                    Layout.fillWidth: true
                    spacing: 4
                    PreferenceCheckBox {
                        objectName: "retouchVisible_" + retouchRow.index
                        textColor: root.textColor
                        checked: retouchRow.modelData.visible
                        onToggled: root.editor.setRetouchVisible(retouchRow.index, checked)
                        Accessible.name: qsTr("Afficher la retouche %1").arg(retouchRow.index + 1)
                    }
                    PreferenceButton {
                        objectName: "retouchSelect_" + retouchRow.index
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: qsTr("%1 %2").arg(root.labelOf(retouchRow.modelData.kind)).arg(retouchRow.index + 1)
                        font.bold: retouchRow.index === root.selected
                        onClicked: root.selected = retouchRow.index
                    }
                    PreferenceButton {
                        objectName: "retouchRemove_" + retouchRow.index
                        text: "✕"
                        Layout.preferredWidth: 30
                        Accessible.name: qsTr("Supprimer la retouche %1").arg(retouchRow.index + 1)
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Supprimer")
                        hoverEnabled: true
                        onClicked: root.removeRetouch(retouchRow.index)
                    }
                }
            }

            // The retouch in hand.
            ColumnLayout {
                objectName: "retouchDetails"
                Layout.fillWidth: true
                spacing: 8
                visible: root.current !== null

                SectionNote {
                    objectName: "retouchHint"
                    text: root.current ? root.noteOf(root.current.kind) : ""
                }
                PreferenceCheckBox {
                    id: zoneCheck
                    objectName: "retouchShowZone"
                    Layout.fillWidth: true
                    text: qsTr("Afficher la zone")
                    textColor: root.textColor
                }
                PreferenceCheckBox {
                    id: sourceCheck
                    objectName: "retouchPlaceSource"
                    Layout.fillWidth: true
                    visible: root.hasSource
                    text: qsTr("Déplacer la source")
                    textColor: root.textColor
                }
                AdjustmentControl {
                    objectName: "retouchBrushSize"
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    label: qsTr("Taille (%)")
                    valueText: value.toFixed(1)
                    minimumValue: 0.1
                    maximumValue: 20
                    resetValue: 3
                    increment: 0.1
                    value: root.brushSize * 100
                    adjustmentEnabled: true
                    onValueEdited: root.brushSize = value / 100
                }
                AdjustmentControl {
                    objectName: "retouchBrushFeather"
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    label: qsTr("Contour progressif (%)")
                    valueText: Math.round(value)
                    maximumValue: 100
                    resetValue: 30
                    value: root.brushFeather * 100
                    adjustmentEnabled: true
                    onValueEdited: root.brushFeather = value / 100
                }
                AdjustmentControl {
                    id: opacityControl
                    objectName: "retouchOpacity"
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    label: qsTr("Opacité (%)")
                    valueText: Math.round(value)
                    maximumValue: 100
                    resetValue: 100
                    adjustmentEnabled: root.current !== null
                    onValueEdited: if (!root.syncing) root.editor.setRetouchOpacity(root.selected, value / 100)
                }
            }
        }
    }
}
