import QtQuick
import QtQuick.Controls

Item {
    id: root
    required property var editor
    required property var settings
    property bool blocked: false
    // Qt maps Ctrl to the physical Command key on macOS; Meta would mean Control.
    readonly property string commandModifier: "Ctrl"
    readonly property bool photoReady: editor.hasImage && !editor.busy && !editor.embeddedPreview && !blocked
    // These methods are deliberately not emulated in QML. See IHM-CONN-007/008.
    readonly property bool manualSaveAvailable: typeof editor.saveCurrentAdjustments === "function"
    readonly property bool duplicationAvailable: typeof editor.duplicateCurrentImage === "function"
    property alias openImageAction: openImage
    property alias openFolderAction: openFolder
    property alias saveAction: save
    property alias exportAction: exportPhoto
    property alias duplicateAction: duplicate
    property alias resetAction: reset
    property alias preferencesAction: preferences
    property alias quitAction: quit
    signal openImageRequested()
    signal openFolderRequested()
    signal preferencesRequested()
    signal resetRequested()

    Action {
        id: openImage
        objectName: "openImageAction"
        text: qsTr("Ouvrir une image…")
        shortcut: StandardKey.Open
        enabled: !root.blocked
        onTriggered: root.openImageRequested()
    }
    Action {
        id: openFolder
        objectName: "openFolderAction"
        text: qsTr("Ouvrir un dossier…")
        shortcut: root.commandModifier + "+Shift+O"
        enabled: !root.blocked
        onTriggered: root.openFolderRequested()
    }
    Action {
        id: save
        objectName: "saveAdjustmentsAction"
        text: qsTr("Enregistrer les réglages")
        shortcut: StandardKey.Save
        enabled: root.photoReady && root.manualSaveAvailable
        onTriggered: root.editor.saveCurrentAdjustments()
    }
    Action {
        id: exportPhoto
        objectName: "exportImageAction"
        text: qsTr("Exporter…")
        shortcut: root.commandModifier + "+E"
        enabled: root.photoReady
        onTriggered: root.settings.openExportDialog()
    }
    Action {
        id: duplicate
        objectName: "duplicateImageAction"
        text: qsTr("Dupliquer le fichier et ses réglages")
        shortcut: root.commandModifier + "+D"
        enabled: root.photoReady && root.duplicationAvailable
        onTriggered: root.editor.duplicateCurrentImage()
    }
    Action {
        id: reset
        objectName: "resetAdjustmentsAction"
        text: qsTr("Réinitialiser les retouches…")
        shortcut: root.commandModifier + "+Shift+R"
        enabled: root.photoReady
        onTriggered: root.resetRequested()
    }
    Action {
        id: preferences
        objectName: "preferencesAction"
        text: qsTr("Interface et développement RAW…")
        shortcut: root.commandModifier + "+,"
        enabled: !root.blocked
        onTriggered: root.preferencesRequested()
    }
    Action {
        id: quit
        objectName: "quitAction"
        text: qsTr("Quitter")
        shortcut: root.commandModifier + "+Q"
        enabled: !root.blocked
        onTriggered: Qt.quit()
    }
}
