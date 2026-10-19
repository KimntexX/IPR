import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var editor
    required property var preferences
    spacing: 12
    Label {
        text: qsTr("Calibration RAW — image ouverte")
        font.bold: true
        color: root.preferences.theme.text
    }
    Label {
        Layout.fillWidth: true
        text: root.editor.hasImage ? root.editor.imageDetails.name : qsTr("Aucune image ouverte")
        color: root.preferences.theme.secondaryText
        wrapMode: Text.WrapAnywhere
    }
    RawCalibrationModule {
        editor: root.editor
        textColor: root.preferences.theme.text
        secondaryTextColor: root.preferences.theme.secondaryText
    }
    PreferenceButton {
        objectName: "resetRawDevelopmentButton"
        text: qsTr("Réinitialiser profil / calibration")
        Layout.fillWidth: true
        enabled: root.editor.rawImage && !root.editor.busy
        onClicked: root.editor.resetRawDevelopment()
    }
    Label {
        Layout.fillWidth: true
        text: qsTr("La calibration concerne uniquement l’image ouverte et son fichier .ipr. " +
                   "Le moteur active déjà la calibration par défaut sur les boîtiers compatibles. " +
                   "Ce contrôle ne change pas la valeur par défaut des prochaines images.")
        wrapMode: Text.WordWrap
        color: root.preferences.theme.secondaryText
        font.pixelSize: 11
    }
}
