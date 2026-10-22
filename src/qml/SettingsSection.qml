import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    default property alias content: sectionBody.data
    property string title: ""
    property var preferences: null
    property string stateKey: ""
    property bool initialized: false
    property bool restoring: false
    property bool expanded: false
    property color textColor: "#edf2f8"
    readonly property Item body: sectionBody
    // Numeric controls only. Authoritative whole-recipe dirty state needs the engine.
    readonly property bool modified: hasModifiedControls(sectionBody)
    function hasModifiedControls(item) {
        for (let child of item.children) {
            if (child.modified === true) return true
            if (hasModifiedControls(child)) return true
        }
        return false
    }

    Layout.fillWidth: true
    Layout.minimumWidth: 0
    implicitWidth: 0
    spacing: expanded ? 8 : 0

    function restoreState() {
        if (!preferences || stateKey.length === 0)
            return
        restoring = true
        expanded = preferences.sectionExpanded(stateKey)
        restoring = false
    }

    Component.onCompleted: {
        restoreState()
        initialized = true
    }
    onPreferencesChanged: if (initialized) restoreState()
    onStateKeyChanged: if (initialized) restoreState()
    onExpandedChanged: {
        if (initialized && !restoring && preferences && stateKey.length > 0)
            preferences.setSectionExpanded(stateKey, expanded)
    }

    // Explicit data keeps the component's header outside its default content
    // property. Caller controls belong only to the collapsible body.
    data: [
        PreferenceButton {
            id: header
            objectName: "sectionToggle"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredHeight: 32
            text: root.title
            padding: 8
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            Accessible.name: root.title
            Accessible.description: root.expanded ? qsTr("Replier la section")
                                                 : qsTr("Déplier la section")
            onClicked: root.expanded = !root.expanded

            contentItem: RowLayout {
                spacing: 8

                Text {
                    text: root.expanded ? "▾" : "▸"
                    color: root.textColor
                    font.pixelSize: 12
                    Layout.preferredWidth: 10
                }

                Label {
                    text: root.title
                    color: root.textColor
                    font.bold: true
                    font.pixelSize: 13
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                }
                Label {
                    objectName: "sectionModifiedIndicator"
                    visible: root.modified
                    text: "•"
                    color: root.textColor
                    Accessible.name: qsTr("Valeurs numériques modifiées")
                    HoverHandler { id: modifiedHover }
                    ToolTip.visible: modifiedHover.hovered
                    ToolTip.text: qsTr("Au moins un réglage numérique diffère de sa valeur de réinitialisation")
                }
            }

            background: Rectangle {
                radius: 4
                color: header.down || header.hovered ? header.palette.mid : header.palette.button
                border.width: 1
                border.color: header.activeFocus ? header.palette.text : header.palette.mid
            }
        },
        ColumnLayout {
            id: sectionBody
            objectName: "sectionBody"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            implicitWidth: 0
            spacing: 10
            // Keep controls instantiated: hiding must not reset slider values,
            // export options, selected colour bands or curve editing state.
            visible: root.expanded
        }
    ]
}
