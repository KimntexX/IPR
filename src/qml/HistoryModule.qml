import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts

// Edit history and named snapshots of the open image. The engine keeps them in the
// photo's .ipr (EditHistory); this panel only shows them and sends the commands.
ColumnLayout {
    id: root

    required property var editor
    property color textColor: palette.text
    property color secondaryTextColor: palette.windowText
    property int fontSize: 12

    Layout.fillWidth: true
    Layout.minimumWidth: 0
    implicitWidth: 0
    spacing: 6

    // The name of a recipe field as the panels show it; a family of fields shares one.
    function fieldLabel(field) {
        if (field.startsWith("mixer")) return qsTr("Mélangeur de couleurs")
        if (field.startsWith("grading")) return qsTr("Color grading")
        const labels = {
            "brightness": qsTr("Exposition"), "contrast": qsTr("Contraste"), "blur": qsTr("Flou gaussien"),
            "grayscale": qsTr("Noir et blanc"), "shadows": qsTr("Ombres"), "blacks": qsTr("Noirs"),
            "highlights": qsTr("Hautes lumières"), "whites": qsTr("Blancs"), "temperature": qsTr("Température"),
            "tint": qsTr("Nuance"), "vibrance": qsTr("Vibrance"), "saturation": qsTr("Saturation"),
            "texture": qsTr("Texture"), "clarity": qsTr("Clarté"), "dehaze": qsTr("Correction du voile"),
            "grain": qsTr("Grain"), "denoiseLuminance": qsTr("Bruit de luminance"),
            "denoiseChrominance": qsTr("Bruit de couleur"), "sharpeningAmount": qsTr("Gain de netteté"),
            "sharpeningRadius": qsTr("Rayon de netteté"), "sharpeningMasking": qsTr("Masquage de netteté"),
            "pointColor": qsTr("Couleur du point"), "automaticWhiteBalance": qsTr("Balance automatique"),
            "toneCurves": qsTr("Courbe de tonalité"), "rawDevelopment": qsTr("Développement RAW")
        }
        return labels[field] || field
    }

    function stepLabel(step) {
        if (step === 0) return qsTr("Ouverture")
        const steps = editor.historySteps
        const names = []
        for (const field of steps[step].changes) {
            const name = fieldLabel(field)
            if (!names.includes(name)) names.push(name)
        }
        if (names.length === 0) return qsTr("Réglages")
        return names.length <= 2 ? names.join(", ") : qsTr("%1 réglages").arg(names.length)
    }

    Connections {
        target: root.editor
        function onHistoryChanged() {
            Qt.callLater(() => stepList.positionViewAtIndex(root.editor.historyPosition, ListView.Contain))
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        PreferenceButton {
            objectName: "historyUndo"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: qsTr("Annuler")
            enabled: root.editor.canUndo
            onClicked: root.editor.undo()
        }
        PreferenceButton {
            objectName: "historyRedo"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: qsTr("Rétablir")
            enabled: root.editor.canRedo
            onClicked: root.editor.redo()
        }
    }

    // Newest step on top; the steps above the current one can still be redone.
    // A click goes to a step. Delete or the context menu takes its edit out.
    ListView {
        id: stepList
        objectName: "historyStepList"
        Layout.fillWidth: true
        // Six steps show at once, the others scroll.
        Layout.preferredHeight: Math.min(count, 6) * 24
        clip: true
        activeFocusOnTab: true
        boundsBehavior: Flickable.StopAtBounds
        verticalLayoutDirection: ListView.BottomToTop
        model: root.editor.historySteps
        Accessible.name: qsTr("Étapes de l'historique")
        Accessible.description: qsTr("Clic : revenir à l'étape. Suppr ou clic droit : retirer sa retouche.")
        ScrollBar.vertical: PanelScrollBar {}

        Keys.onDeletePressed: root.editor.removeHistoryStep(root.editor.historyPosition)

        delegate: Rectangle {
            required property int index
            objectName: "historyStepRow"
            width: ListView.view.width
            height: 24
            radius: 3
            color: index === root.editor.historyPosition ? palette.mid : "transparent"
            border.width: stepList.activeFocus && index === root.editor.historyPosition ? 1 : 0
            border.color: root.textColor

            Text {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 10   // clear of the scroll bar
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                text: root.stepLabel(parent.index)
                color: root.textColor
                opacity: parent.index > root.editor.historyPosition ? 0.5 : 1
                font.pixelSize: root.fontSize
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: (mouse) => {
                    if (mouse.button === Qt.RightButton) {
                        stepMenu.step = parent.index
                        stepMenu.popup(parent, mouse.x, mouse.y)
                    } else {
                        root.editor.goToHistoryStep(parent.index)
                        stepList.forceActiveFocus()
                    }
                }
            }
        }
    }

    AppMenu {
        id: stepMenu
        objectName: "historyStepMenu"
        property int step: -1

        ActionMenuItem {
            objectName: "historyStepRemove"
            text: qsTr("Supprimer cette étape")
            enabled: stepMenu.step > 0
            onTriggered: root.editor.removeHistoryStep(stepMenu.step)
        }
    }

    Label {
        Layout.fillWidth: true
        text: qsTr("Instantanés")
        color: root.secondaryTextColor
        font.pixelSize: root.fontSize - 1
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        Basic.TextField {
            id: snapshotName
            objectName: "snapshotName"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            implicitHeight: 30
            enabled: root.editor.hasImage
            selectByMouse: true
            color: root.textColor
            selectedTextColor: palette.highlightedText
            selectionColor: palette.highlight
            font.pixelSize: root.fontSize
            placeholderText: qsTr("Nom de l'instantané")
            placeholderTextColor: Qt.rgba(root.secondaryTextColor.r, root.secondaryTextColor.g,
                                          root.secondaryTextColor.b, 0.6)
            onAccepted: createSnapshot.clicked()
            background: Rectangle {
                radius: 3
                color: snapshotName.activeFocus ? snapshotName.palette.base : "transparent"
                border.width: 1
                border.color: snapshotName.activeFocus ? snapshotName.palette.text : snapshotName.palette.mid
            }
        }
        PreferenceButton {
            id: createSnapshot
            objectName: "snapshotCreate"
            text: qsTr("Créer")
            enabled: root.editor.hasImage
            onClicked: {
                root.editor.addSnapshot(snapshotName.text)
                snapshotName.clear()
            }
        }
    }

    // Three snapshots show at once; the others scroll.
    ListView {
        objectName: "snapshotList"
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(count, 3) * 36
        clip: true
        spacing: 6
        boundsBehavior: Flickable.StopAtBounds
        model: root.editor.snapshots
        ScrollBar.vertical: PanelScrollBar {}

        delegate: RowLayout {
            required property int index
            required property var modelData
            objectName: "snapshotRow"
            width: ListView.view.width - 10   // clear of the scroll bar
            height: 30
            spacing: 6

            PreferenceButton {
                objectName: "snapshotRestore"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: parent.modelData.name
                Accessible.name: qsTr("Restaurer l'instantané %1").arg(parent.modelData.name)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Restaurer (créé le %1)").arg(
                    Qt.formatDateTime(parent.modelData.created, "dd/MM/yyyy HH:mm"))
                onClicked: root.editor.restoreSnapshot(parent.index)
            }
            PreferenceButton {
                objectName: "snapshotRemove"
                Layout.preferredWidth: 30
                text: "−"
                padding: 0
                Accessible.name: qsTr("Supprimer l'instantané %1").arg(parent.modelData.name)
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onClicked: root.editor.removeSnapshot(parent.index)
            }
        }
    }
}
