import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var editor
    property color textColor: "#edf2f8"
    property color secondaryTextColor: "#d7dfeb"
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    implicitWidth: 0
    spacing: 8

    function selectedIndex() {
        const profiles = editor.cameraProfiles
        for (let i = 0; i < profiles.length; ++i)
            if (profiles[i].path === editor.selectedCameraProfile)
                return i
        return 0
    }

    Label {
        text: qsTr("Profil couleur DCP")
        color: root.textColor
        Layout.fillWidth: true
    }
    ComboBox {
        id: profileSelector
        objectName: "cameraProfileSelector"
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        implicitWidth: 0
        model: root.editor.cameraProfiles
        textRole: "name"
        currentIndex: root.selectedIndex()
        enabled: root.editor.rawImage && !root.editor.busy && count > 1
        onActivated: function(index) {
            root.editor.setCameraProfile(model[index].path)
        }
        contentItem: Text {
            text: profileSelector.displayText
            color: root.textColor
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            rightPadding: profileSelector.indicator.width + 6
        }
        background: Rectangle {
            radius: 4
            color: "#20252c"
            border.color: profileSelector.activeFocus ? "#71849a" : "#424b58"
        }
    }
    CheckBox {
        objectName: "rawAutomaticWhiteBalanceToggle"
        text: qsTr("Balance des blancs automatique")
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        checked: root.editor.automaticWhiteBalance
        enabled: root.editor.rawImage && !root.editor.busy
        onClicked: root.editor.setAutomaticWhiteBalance(checked)
        contentItem: Text {
            text: parent.text
            color: root.textColor
            wrapMode: Text.WordWrap
            leftPadding: parent.indicator.width + parent.spacing
        }
    }
    CheckBox {
        objectName: "rawNormalizationToggle"
        text: qsTr("Calibration RAW — rendu du boîtier")
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        checked: root.editor.rawNormalizationEnabled
        enabled: root.editor.rawImage && !root.editor.busy
        onClicked: root.editor.setRawNormalizationEnabled(checked)
        contentItem: Text {
            text: parent.text
            color: root.textColor
            wrapMode: Text.WordWrap
            leftPadding: parent.indicator.width + parent.spacing
            verticalAlignment: Text.AlignVCenter
        }
    }
    Button {
        objectName: "rawNormalizationManifestButton"
        text: qsTr("Choisir le manifeste…")
        Layout.fillWidth: true
        enabled: root.editor.rawImage && !root.editor.busy
        onClicked: manifestDialog.open()
    }
    Label {
        objectName: "rawNormalizationManifestLabel"
        text: root.editor.rawNormalizationManifest.length > 0
              ? root.editor.rawNormalizationManifest : qsTr("Aucun manifeste sélectionné")
        color: root.secondaryTextColor
        font.pixelSize: 10
        wrapMode: Text.WrapAnywhere
        Layout.fillWidth: true
        Layout.minimumWidth: 0
    }
    Label {
        objectName: "rawDevelopmentStatusLabel"
        text: root.editor.busy ? qsTr("Traitement en cours — image et retouches conservées…") : root.editor.rawDevelopmentStatus
        color: root.textColor
        wrapMode: Text.WrapAnywhere
        Layout.fillWidth: true
        Layout.minimumWidth: 0
    }
    Label {
        text: qsTr("Profils DCP/LCP et réglages enregistrés par image dans le fichier .ipr. Camera Neutral et calibration actifs par défaut pour les boîtiers et modes compatibles. Configuration de calibration mise à jour automatiquement. Nouveaux ISO et hautes lumières à vérifier visuellement ; un boîtier ou mode inconnu ne reçoit pas une règle d'un autre appareil.")
        color: root.secondaryTextColor
        font.pixelSize: 11
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
        Layout.minimumWidth: 0
    }
    Button {
        objectName: "resetRawDevelopmentButton"
        text: qsTr("Réinitialiser profil / calibration")
        Layout.fillWidth: true
        enabled: root.editor.rawImage && !root.editor.busy
        onClicked: root.editor.resetRawDevelopment()
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Revient au profil automatique et désactive la calibration sans effacer les retouches ni changer la balance des blancs.")
    }
    Label {
        objectName: "rawProfileProcessingLabel"
        text: root.editor.rawProfileFastPathAvailable
              ? qsTr("Changement de DCP sans redécodage RAW disponible.")
              : qsTr("Un redécodage peut être nécessaire (balance des blancs, calibration ou tampon indisponible).")
        visible: root.editor.rawImage
        color: root.secondaryTextColor
        font.pixelSize: 10
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
        Layout.minimumWidth: 0
    }
    FileDialog {
        id: manifestDialog
        title: qsTr("Manifeste de normalisation RAW")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Calibration JSON (*.json)")]
        onAccepted: root.editor.setRawNormalizationManifest(selectedFile)
    }
}
