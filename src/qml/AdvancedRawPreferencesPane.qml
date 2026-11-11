import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var editor
    required property var preferences
    spacing: 12
    Label {
        Layout.fillWidth: true
        text: qsTr("Prototype RAW : Canon EOS R6 Mark III, balance des blancs du boîtier. Les autres réglages de bruit restent disponibles.")
        wrapMode: Text.WordWrap
        color: root.preferences.theme.secondaryText
    }
    Label {
        text: qsTr("Intelligence artificielle — débruitage RAW")
        font.bold: true
        color: root.preferences.theme.text
    }
    PreferenceCheckBox {
        objectName: "neuralDenoiseCheckBox"
        text: qsTr("Débruitage neuronal (expérimental)")
        checked: root.preferences.value("neural/enabled") === true
        enabled: root.preferences.writable && !root.editor.busy
        onToggled: root.preferences.setValue("neural/enabled", checked)
    }
    RowLayout {
        Label { text: qsTr("Force par défaut (0–100)"); color: root.preferences.theme.text }
        PreferenceSpinBox {
            objectName: "neuralDenoiseStrength"
            from: 0; to: 100
            value: Number(root.preferences.value("neural/strength"))
            enabled: root.preferences.writable && !root.editor.busy
            onValueModified: root.preferences.setValue("neural/strength", value)
        }
    }
    Label {
        Layout.fillWidth: true
        visible: root.preferences.value("neural/enabled") === true
        text: root.editor.neuralDenoiseEngine === "DirectML"
            ? qsTr("Moteur : DirectML. Les prochaines images sans recette reprennent cette force.")
            : (root.editor.neuralDenoiseEngine === "CPU"
               ? qsTr("Moteur CPU : traitement lent, environ 50 s pour 24 Mpx sur la machine de référence.")
               : qsTr("Le moteur sera identifié après le premier traitement. Sans GPU, le traitement peut être long. Le modèle doit être installé."))
        wrapMode: Text.WordWrap
        color: root.preferences.theme.secondaryText
    }
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
