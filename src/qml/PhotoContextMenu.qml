import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic

Basic.Menu {
    id: root
    popupType: Popup.Item
    width: Math.min(440, parent && parent.Window.window ? parent.Window.window.width - 24 : 440)
    required property var actions
    required property var editor
    property string targetName: ""
    property url targetUrl
    property string targetKey: ""
    property string currentImageKey: ""
    readonly property bool currentPhoto: editor.hasImage && targetKey === currentImageKey
    readonly property bool hasTarget: targetUrl.toString().length > 0
    function openFor(item, x, y) { popup(item, x, y) }
    ActionMenuItem {
        text: root.targetName
        enabled: false
        visible: text.length > 0
    }
    ActionMenuItem {
        objectName: "contextOpenTarget"
        text: qsTr("Ouvrir cette image")
        visible: root.hasTarget
        implicitHeight: root.hasTarget ? 32 : 0
        height: implicitHeight
        enabled: !root.editor.busy && !root.actions.blocked
        onTriggered: root.editor.loadImage(root.targetUrl)
    }
    ActionMenuItem { action: root.actions.openImageAction }
    Basic.MenuSeparator { }
    ActionMenuItem {
        action: root.actions.copyAction
        enabled: root.currentPhoto && root.actions.copyAction.enabled
    }
    ActionMenuItem {
        action: root.actions.pasteAction
        enabled: root.currentPhoto && root.actions.pasteAction.enabled
    }
    Basic.MenuSeparator { }
    ActionMenuItem {
        objectName: "contextSaveAdjustments"
        action: root.actions.saveAction
        enabled: root.currentPhoto && root.actions.saveAction.enabled
    }
    ActionMenuItem {
        objectName: "contextExportImage"
        action: root.actions.exportAction
        enabled: root.currentPhoto && root.actions.exportAction.enabled
    }
    ActionMenuItem {
        objectName: "contextDuplicateImage"
        action: root.actions.duplicateAction
        enabled: root.currentPhoto && root.actions.duplicateAction.enabled
    }
    ActionMenuItem {
        objectName: "contextResetAdjustments"
        action: root.actions.resetAction
        enabled: root.currentPhoto && root.actions.resetAction.enabled
    }
    ActionMenuItem {
        text: root.currentPhoto
              ? qsTr("Enregistrer et dupliquer : raccordement moteur requis")
              : qsTr("Ouvrir cette image pour agir sur ses retouches")
        enabled: false
        visible: !root.currentPhoto || !root.actions.manualSaveAvailable || !root.actions.duplicationAvailable
        font.pixelSize: 10
    }
}
