import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var preferences
    spacing: 12
    ImageBackgroundOptions { id: backgroundOptions }
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
        model: backgroundOptions.choices
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
