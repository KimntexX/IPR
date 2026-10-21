import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var preferences
    spacing: 12
    Label {
        text: qsTr("Affichage des panneaux")
        font.bold: true
        color: root.preferences.theme.text
    }
    Repeater {
        model: [{ key: "showImageInfo", label: qsTr("Afficher les informations de l’image") },
                { key: "showHistogram", label: qsTr("Afficher l’histogramme") },
                { key: "showFilmstrip", label: qsTr("Afficher le ruban des images") }]
        PreferenceCheckBox {
            required property var modelData
            Layout.fillWidth: true
            objectName: "preference_" + modelData.key
            text: modelData.label
            checked: root.preferences.layout[modelData.key]
            enabled: root.preferences.writable
            onClicked: root.preferences.setValue("layout/" + modelData.key, checked)
        }
    }
    Label {
        text: qsTr("Disposition et lisibilité")
        font.bold: true
        color: root.preferences.theme.text
    }
    Label {
        text: qsTr("Largeur du panneau Réglages (px)")
        color: root.preferences.theme.secondaryText
    }
    PreferenceSpinBox {
        objectName: "preference_settingsWidth"
        from: 260
        to: 360
        stepSize: 10
        value: root.preferences.layout.settingsWidth
        enabled: root.preferences.writable
        onValueModified: root.preferences.setValue("layout/settingsWidth", value)
    }
    Label {
        text: qsTr("Taille du texte des informations (px)")
        color: root.preferences.theme.secondaryText
    }
    PreferenceSpinBox {
        objectName: "preference_imageInfoFontSize"
        from: 10
        to: 20
        value: root.preferences.layout.imageInfoFontSize
        enabled: root.preferences.writable
        onValueModified: root.preferences.setValue("layout/imageInfoFontSize", value)
    }
}
