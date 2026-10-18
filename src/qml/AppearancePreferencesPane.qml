import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var preferences
    spacing: 12
    InterfacePaletteSelector {
        objectName: "interfacePaletteSelector"
        preferences: root.preferences
    }
    Label {
        text: qsTr("Fond de la zone image")
        font.bold: true
        color: root.preferences.theme.text
    }
    Label {
        Layout.fillWidth: true
        text: qsTr("Le fond de la photo reste indépendant des couleurs de l’interface.")
        wrapMode: Text.WordWrap
        color: root.preferences.theme.secondaryText
    }
    PreferenceComboBox {
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
        onActivated: function(index) { root.preferences.setValue("theme/canvas", model[index].value) }
    }
}
