import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts

Basic.Dialog {
    id: root
    required property var editor
    required property var preferences
    title: qsTr("Préférences")
    anchors.centerIn: Overlay.overlay
    width: Math.min(parent ? parent.width - 40 : 560, 560)
    height: Math.min(parent ? parent.height - 40 : 620, 620)
    padding: 14
    modal: true
    standardButtons: Dialog.NoButton

    // Overlay popups need an explicit palette. Basic controls prevent the
    // platform's native style from substituting white, unthemed widgets.
    palette.window: preferences.theme.panel
    palette.windowText: preferences.theme.text
    palette.text: preferences.theme.text
    palette.buttonText: preferences.theme.text
    palette.base: Qt.darker(preferences.theme.panel, 1.10)
    palette.button: Qt.darker(preferences.theme.panel, 1.10)
    palette.mid: Qt.tint(preferences.theme.panel, Qt.rgba(
        preferences.theme.text.r, preferences.theme.text.g, preferences.theme.text.b, 0.25))
    palette.highlight: Qt.tint(preferences.theme.panel, Qt.rgba(
        preferences.theme.text.r, preferences.theme.text.g, preferences.theme.text.b, 0.25))
    palette.highlightedText: preferences.theme.text

    header: Label {
        text: root.title
        padding: 14
        font.bold: true
        font.pixelSize: 16
        color: root.preferences.theme.text
        background: Rectangle { color: root.preferences.theme.panel }
    }
    footer: RowLayout {
        spacing: 12
        Label {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.leftMargin: 14
            text: root.preferences.filePath
            color: root.preferences.theme.secondaryText
            font.pixelSize: 10
            elide: Text.ElideMiddle
            ToolTip.visible: pathHover.hovered
            ToolTip.text: text
            HoverHandler { id: pathHover }
        }
        PreferenceButton {
            objectName: "closePreferencesButton"
            Layout.rightMargin: 14
            Layout.bottomMargin: 12
            Layout.preferredWidth: 90
            text: qsTr("Fermer")
            onClicked: root.close()
        }
    }
    contentItem: ColumnLayout {
        spacing: 12
        Basic.TabBar {
            id: navigation
            objectName: "preferencesTabs"
            Layout.fillWidth: true
            background: Item { }
            component PreferenceTab: Basic.TabButton {
                id: tab
                implicitHeight: 32
                contentItem: Text {
                    text: tab.text
                    font: tab.font
                    color: root.preferences.theme.text
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 4
                    color: tab.checked || tab.hovered ? root.palette.mid : root.palette.button
                    border.color: tab.activeFocus || tab.checked ? root.palette.text : root.palette.mid
                }
            }
            PreferenceTab { text: qsTr("Interface") }
            PreferenceTab { text: qsTr("Apparence") }
            PreferenceTab { text: qsTr("Avancé · RAW") }
        }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: navigation.currentIndex
            PreferenceScrollView {
                id: interfaceScroll
                objectName: "interfacePreferencesPage"
                InterfacePreferencesPane {
                    width: interfaceScroll.availableWidth
                    preferences: root.preferences
                }
            }
            PreferenceScrollView {
                id: appearanceScroll
                objectName: "appearancePreferencesPage"
                AppearancePreferencesPane {
                    width: appearanceScroll.availableWidth
                    preferences: root.preferences
                }
            }
            PreferenceScrollView {
                id: rawScroll
                objectName: "advancedRawPreferencesPage"
                AdvancedRawPreferencesPane {
                    width: rawScroll.availableWidth
                    preferences: root.preferences
                    editor: root.editor
                }
            }
        }
        Label {
            Layout.fillWidth: true
            visible: !root.preferences.writable
            text: qsTr("Préférences en lecture seule : format invalide, plus récent ou fichier inaccessible.")
            wrapMode: Text.WordWrap
            color: root.preferences.theme.text
        }
    }
    background: Rectangle {
        color: root.preferences.theme.panel
        border.color: root.palette.mid
        radius: 6
    }
}
