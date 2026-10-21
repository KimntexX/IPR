import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
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
        objectName: "rawNormalizationToggle"
        text: qsTr("Calibration RAW — rendu du boîtier")
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        backendChecked: root.editor.rawNormalizationEnabled
        enabled: root.editor.rawImage && !root.editor.busy
        onChangeRequested: (checked) => root.editor.setRawNormalizationEnabled(checked)
        textColor: root.textColor
    }
    PreferenceButton {
        objectName: "rawNormalizationManifestButton"
        text: qsTr("Choisir le manifeste…")
        Layout.fillWidth: true
        enabled: root.editor.rawImage && !root.editor.busy
        onClicked: manifestDialog.open()
    }
    Label {
        objectName: "rawNormalizationManifestLabel"
        text: root.editor.rawNormalizationManifest.length > 0
              ? root.editor.rawNormalizationManifest : qsTr("Aucun manifeste sélectionné")
        color: root.secondaryTextColor
        font.pixelSize: 10
        wrapMode: Text.WrapAnywhere
        Layout.fillWidth: true
        Layout.minimumWidth: 0
    }
    FileDialog {
        id: manifestDialog
        title: qsTr("Manifeste de calibration RAW")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Calibration JSON (*.json)")]
        onAccepted: root.editor.setRawNormalizationManifest(selectedFile)
    }
}
