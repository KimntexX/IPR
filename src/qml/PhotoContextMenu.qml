import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic

Basic.Menu {
    id: root
    popupType: Popup.Item
    required property var actions
    required property var editor
    property string targetName: ""
    property url targetUrl
    property string targetKey: ""
    property string currentImageKey: ""
    readonly property bool currentPhoto: editor.hasImage && targetKey === currentImageKey
    readonly property bool hasTarget: targetUrl.toString().length > 0
    function openFor(item, x, y) { popup(item, x, y) }
    Basic.MenuItem {
        text: root.targetName
        enabled: false
        visible: text.length > 0
    }
    Basic.MenuItem {
        objectName: "contextOpenTarget"
        text: qsTr("Ouvrir cette image")
        visible: root.hasTarget
        enabled: !root.editor.busy && !root.actions.blocked
        onTriggered: root.editor.loadImage(root.targetUrl)
    }
    Basic.MenuItem { action: root.actions.openImageAction }
    Basic.MenuSeparator { }
    Basic.MenuItem {
        objectName: "contextSaveAdjustments"
        action: root.actions.saveAction
        enabled: root.currentPhoto && root.actions.saveAction.enabled
    }
    Basic.MenuItem {
        objectName: "contextExportImage"
        action: root.actions.exportAction
        enabled: root.currentPhoto && root.actions.exportAction.enabled
    }
    Basic.MenuItem {
        objectName: "contextDuplicateImage"
        action: root.actions.duplicateAction
        enabled: root.currentPhoto && root.actions.duplicateAction.enabled
    }
    Basic.MenuItem {
        objectName: "contextResetAdjustments"
        action: root.actions.resetAction
        enabled: root.currentPhoto && root.actions.resetAction.enabled
    }
    Basic.MenuItem {
        text: root.currentPhoto
              ? qsTr("Enregistrer et dupliquer : raccordement moteur requis")
              : qsTr("Ouvrir cette image pour agir sur ses retouches")
        enabled: false
        visible: !root.currentPhoto || !root.actions.manualSaveAvailable || !root.actions.duplicationAvailable
        font.pixelSize: 10
    }
}
