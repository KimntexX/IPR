import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var preferences
    Layout.fillWidth: true
    readonly property var palettes: [
        { name: qsTr("Bordeaux"), background: "#291a20", panel: "#42252e", text: "#faeef1", secondaryText: "#e5c9d2" },
        { name: qsTr("Blanc cassé"), background: "#e6e1d7", panel: "#f4f0e6", text: "#292823", secondaryText: "#55534b" },
        { name: qsTr("Noir mat"), background: "#111214", panel: "#1c1e21", text: "#ececee", secondaryText: "#c1c4ca" },
        { name: qsTr("Bleu marine"), background: "#111c2b", panel: "#1b2d45", text: "#eaf1fc", secondaryText: "#c4d3e8" },
        { name: qsTr("Gris actuel"), background: "#20242b", panel: "#2a3039", text: "#edf2f8", secondaryText: "#d7dfeb" }
    ]
    readonly property int selectedIndex: {
        for (let index = 0; index < palettes.length; ++index) {
            let matches = true
            for (const key of ["background", "panel", "text", "secondaryText"])
                matches = matches && String(preferences.theme[key]).toLowerCase() === palettes[index][key]
            if (matches) return index
        }
        return -1
    }
    function applyPalette(index) {
        if (!preferences.writable || !Number.isInteger(index) || index < 0 || index >= palettes.length)
            return false
        // The neutral photo canvas is deliberately independent of interface colours.
        for (const key of ["background", "panel", "text", "secondaryText"])
            if (!preferences.setValue("theme/" + key, palettes[index][key])) return false
        return true
    }
    Label {
        text: qsTr("Palette de l’interface")
        color: root.preferences.theme.text
    }
    Flow {
        Layout.fillWidth: true
        Layout.preferredHeight: childrenRect.height
        spacing: 6
        Repeater {
            model: root.palettes
            ToolButton {
                id: swatch
                required property var modelData
                required property int index
                objectName: "interfacePalette" + index
                width: 92
                height: 64
                padding: 5
                enabled: root.preferences.writable
                Accessible.name: modelData.name
                Accessible.role: Accessible.RadioButton
                Accessible.checked: root.selectedIndex === index
                ToolTip.visible: hovered
                ToolTip.text: modelData.name
                onClicked: root.applyPalette(index)
                contentItem: ColumnLayout {
                    spacing: 4
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 24
                        radius: 4
                        color: swatch.modelData.panel
                        border.color: root.preferences.theme.text
                        border.width: root.selectedIndex === swatch.index ? 3 : 1
                    }
                    Label {
                        Layout.fillWidth: true
                        text: swatch.modelData.name
                        color: root.preferences.theme.text
                        horizontalAlignment: Text.AlignHCenter
                        font.pixelSize: 11
                    }
                }
                background: Rectangle {
                    color: "transparent"
                    radius: 4
                    border.color: swatch.activeFocus ? root.preferences.theme.text : "transparent"
                }
            }
        }
    }
}
