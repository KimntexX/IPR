import QtQuick
import QtQuick.Controls
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
            color: profileSelector.palette.base
            border.color: profileSelector.activeFocus ? profileSelector.palette.text : profileSelector.palette.mid
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
}
