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
            "toneCurves": qsTr("Courbe de tonalité")
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
            text: qsTr("Annuler")
            enabled: root.editor.canUndo
            onClicked: root.editor.undo()
        }
        PreferenceButton {
            objectName: "historyRedo"
            Layout.fillWidth: true
            text: qsTr("Rétablir")
            enabled: root.editor.canRedo
            onClicked: root.editor.redo()
        }
    }

    // Newest step on top; the steps above the current one can still be redone.
    ListView {
        id: stepList
        objectName: "historyStepList"
        Layout.fillWidth: true
        Layout.preferredHeight: 150
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        verticalLayoutDirection: ListView.BottomToTop
        model: root.editor.historySteps
        ScrollBar.vertical: ScrollBar {}

        delegate: Rectangle {
            required property int index
            objectName: "historyStepRow"
            width: ListView.view.width
            height: 24
            radius: 3
            color: index === root.editor.historyPosition ? palette.mid : "transparent"

            Text {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                text: root.stepLabel(parent.index)
                color: root.textColor
                opacity: parent.index > root.editor.historyPosition ? 0.5 : 1
                font.pixelSize: 12
            }
            MouseArea {
                anchors.fill: parent
                onClicked: root.editor.goToHistoryStep(parent.index)
            }
        }
    }

    Label {
        Layout.fillWidth: true
        text: qsTr("Instantanés")
        color: root.secondaryTextColor
        font.pixelSize: 12
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
            placeholderText: qsTr("Nom de l'instantané")
            onAccepted: createSnapshot.clicked()
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

    Repeater {
        model: root.editor.snapshots

        RowLayout {
            required property int index
            required property var modelData
            objectName: "snapshotRow"
            Layout.fillWidth: true
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
