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

    BackendCheckBox {
        objectName: "lensCorrectionToggle"
        text: qsTr("Activer la correction de l’objectif")
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        backendChecked: root.editor.lensCorrectionEnabled
        enabled: root.editor.rawImage && !root.editor.busy
        onChangeRequested: (checked) => root.editor.setLensCorrectionEnabled(checked)
        contentItem: Text {
            text: parent.text
            color: root.textColor
            wrapMode: Text.WordWrap
            leftPadding: parent.indicator.width + parent.spacing
        }
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Corrige la distorsion et le vignettage lorsque le profil LCP contient ces données.")
    }
    BackendCheckBox {
        objectName: "chromaticAberrationToggle"
        text: qsTr("Supprimer l’aberration chromatique")
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        backendChecked: root.editor.chromaticAberrationEnabled
        enabled: root.editor.rawImage && !root.editor.busy
        onChangeRequested: (checked) => root.editor.setChromaticAberrationEnabled(checked)
        contentItem: Text {
            text: parent.text
            color: root.textColor
            wrapMode: Text.WordWrap
            leftPadding: parent.indicator.width + parent.spacing
        }
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Recalage chromatique latéral rouge/bleu selon le LCP. Ne traite pas les franges longitudinales.")
    }
    Label {
        objectName: "opticalDevelopmentStatusLabel"
        text: root.editor.opticalDevelopmentStatus
        color: root.secondaryTextColor
        font.pixelSize: 11
        wrapMode: Text.WrapAnywhere
        Layout.fillWidth: true
        Layout.minimumWidth: 0
    }
}
