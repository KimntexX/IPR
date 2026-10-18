import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    required property var editor
    required property var preferences
    title: qsTr("Préférences")
    anchors.centerIn: Overlay.overlay
    width: Math.min(parent ? parent.width - 40 : 560, 560)
    height: Math.min(parent ? parent.height - 40 : 620, 620)
    modal: true
    standardButtons: Dialog.NoButton
    header: Label {
        text: root.title
        padding: 12
        font.bold: true
        font.pixelSize: 16
        color: root.preferences.theme.text
        background: Rectangle { color: root.preferences.theme.panel }
    }
    footer: DialogButtonBox {
        background: Rectangle { color: root.preferences.theme.panel }
        Button {
            text: qsTr("Fermer")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        onRejected: root.close()
    }

    contentItem: ScrollView {
        id: preferencesScroll
        clip: true
        contentWidth: availableWidth
        rightPadding: 10
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            width: preferencesScroll.availableWidth
            spacing: 12

            Label {
                text: qsTr("Interface — préférences générales")
                font.bold: true
                color: root.preferences.theme.text
            }
            Repeater {
                model: [{ key: "showImageInfo", label: qsTr("Afficher les informations de l’image") },
                        { key: "showHistogram", label: qsTr("Afficher l’histogramme") },
                        { key: "showFilmstrip", label: qsTr("Afficher le ruban des images") }]
                CheckBox {
                    required property var modelData
                    objectName: "preference_" + modelData.key
                    text: modelData.label
                    checked: root.preferences.layout[modelData.key]
                    enabled: root.preferences.writable
                    onClicked: root.preferences.setValue("layout/" + modelData.key, checked)
                    contentItem: Text {
                        text: parent.text
                        color: root.preferences.theme.text
                        leftPadding: parent.indicator.width + parent.spacing
                    }
                }
            }
            Label {
                text: qsTr("Fond de la zone image")
                color: root.preferences.theme.text
            }
            ComboBox {
                id: canvasBackground
                objectName: "preference_canvasBackground"
                Layout.fillWidth: true
                model: [{ label: qsTr("Anthracite"), value: "#15181d" },
                        { label: qsTr("Gris moyen"), value: "#777777" },
                        { label: qsTr("Noir"), value: "#000000" },
                        { label: qsTr("Blanc"), value: "#ffffff" }]
                textRole: "label"
                currentIndex: {
                    const color = String(root.preferences.theme.canvas).toLowerCase()
                    for (let index = 0; index < model.length; ++index)
                        if (model[index].value === color) return index
                    return -1
                }
                displayText: currentIndex >= 0 ? model[currentIndex].label : qsTr("Couleur personnalisée (INI)")
                enabled: root.preferences.writable
                onActivated: function(index) {
                    root.preferences.setValue("theme/canvas", model[index].value)
                }
            }
            Label {
                text: qsTr("Largeur du panneau Réglages")
                color: root.preferences.theme.text
            }
            SpinBox {
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
                color: root.preferences.theme.text
            }
            SpinBox {
                objectName: "preference_imageInfoFontSize"
                from: 10
                to: 20
                editable: true
                value: root.preferences.layout.imageInfoFontSize
                enabled: root.preferences.writable
                onValueModified: root.preferences.setValue("layout/imageInfoFontSize", value)
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: "#424c59" }
            Label {
                text: qsTr("Options avancées RAW — image ouverte")
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
            Button {
                objectName: "resetRawDevelopmentButton"
                text: qsTr("Réinitialiser profil / calibration")
                Layout.fillWidth: true
                enabled: root.editor.rawImage && !root.editor.busy
                onClicked: root.editor.resetRawDevelopment()
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("La calibration ci-dessus concerne uniquement l’image ouverte et son fichier .ipr. " +
                           "Le moteur active déjà la calibration par défaut sur les boîtiers compatibles. " +
                           "Ce contrôle ne change pas la valeur par défaut des prochaines images.")
                wrapMode: Text.WordWrap
                color: root.preferences.theme.secondaryText
                font.pixelSize: 11
            }
            Label {
                Layout.fillWidth: true
                text: root.preferences.filePath
                wrapMode: Text.WrapAnywhere
                color: root.preferences.theme.secondaryText
                font.pixelSize: 10
            }
            Label {
                Layout.fillWidth: true
                visible: !root.preferences.writable
                text: qsTr("Préférences en lecture seule : format invalide, plus récent ou fichier inaccessible.")
                wrapMode: Text.WordWrap
                color: "#e6bb77"
            }
        }
    }
    background: Rectangle {
        color: root.preferences.theme.panel
        border.color: "#515e70"
        radius: 6
    }
}
