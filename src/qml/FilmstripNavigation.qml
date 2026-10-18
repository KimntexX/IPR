import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml.Models

RowLayout {
    id: root
    required property var editor
    property int currentIndex: -1
    readonly property int imageCount: folderItems.items.count
    readonly property string previousName: nameAt(currentIndex - 1)
    readonly property string nextName: nameAt(currentIndex + 1)

    // DelegateModel exposes role data without constructing image delegates or
    // loading thumbnails. Navigation therefore remains cheap with a hidden ribbon.
    DelegateModel {
        id: folderItems
        model: root.editor.imageModel
        delegate: Item { }
    }

    function nameAt(index) {
        return index >= 0 && index < imageCount
                ? folderItems.items.get(index).model.fileName : ""
    }
    function synchronize() {
        currentIndex = -1
        if (!editor.hasImage)
            return
        const name = editor.imageDetails.name
        for (let index = 0; index < imageCount; ++index) {
            if (nameAt(index) === name) {
                currentIndex = index
                return
            }
        }
    }
    function navigate(direction) {
        const target = currentIndex + direction
        if (editor.busy || currentIndex < 0 || target < 0 || target >= imageCount)
            return
        editor.loadImage(folderItems.items.get(target).model.fileUrl)
    }
    onImageCountChanged: synchronize()
    Component.onCompleted: synchronize()
    Connections {
        target: root.editor
        function onImageInfoChanged() { root.synchronize() }
        function onHasImageChanged() { root.synchronize() }
        function onFolderChanged() { root.synchronize() }
    }

    Repeater {
        model: [-1, 1]
        ToolButton {
            id: arrow
            required property int modelData
            objectName: modelData < 0 ? "previousImageButton" : "nextImageButton"
            readonly property string targetName: modelData < 0 ? root.previousName : root.nextName
            text: modelData < 0 ? "‹" : "›"
            font.pixelSize: 22
            Layout.preferredWidth: 30
            Layout.preferredHeight: 28
            padding: 0
            enabled: !root.editor.busy && root.currentIndex >= 0 && targetName.length > 0
            hoverEnabled: true
            Accessible.name: modelData < 0 ? qsTr("Image précédente") : qsTr("Image suivante")
            Accessible.description: targetName
            ToolTip.visible: hovered && enabled
            ToolTip.delay: 250
            ToolTip.text: targetName
            onClicked: root.navigate(modelData)
            contentItem: Text {
                text: arrow.text
                font: arrow.font
                color: arrow.enabled ? "#d7dfeb" : "#57616e"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: 4
                color: arrow.hovered ? "#3a4655" : "#252c35"
                border.color: "#424c59"
            }
        }
    }
}
