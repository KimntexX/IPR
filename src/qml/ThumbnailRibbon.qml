import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: ribbon

    required property var editor
    property int thumbnailPrefetchCount: 4
    property int thumbnailDelegateWidth: 126
    property int thumbnailDelegateHeight: 76
    property int activeImageIndex: -1
    onVisibleChanged: thumbnailWindowTimer.restart()
    onActiveImageIndexChanged: {
        thumbnailList.currentIndex = activeImageIndex
        if (activeImageIndex >= 0)
            thumbnailList.positionViewAtIndex(activeImageIndex, ListView.Contain)
    }

    implicitHeight: 120
    color: "#2a3039"
    radius: 8
    border.color: "#3d4654"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 5

        RowLayout {
            Layout.fillWidth: true

            Label {
                text: qsTr("Images du dossier")
                color: palette.windowText
                font.bold: true
                font.pixelSize: 13
            }
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                text: qsTr("%n image(s)", "", thumbnailList.count)
                color: palette.windowText
                opacity: 0.7
                font.pixelSize: 11
            }
            PreferenceButton {
                visible: ribbon.editor.selectedImageCount > 0
                text: qsTr("Appliquer à %1").arg(ribbon.editor.selectedImageCount)
                enabled: ribbon.editor.hasImage
                onClicked: ribbon.editor.applyAdjustmentsToSelection()
            }
            PreferenceButton {
                implicitWidth: 26
                visible: ribbon.editor.selectedImageCount > 0
                text: "×"
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Désélectionner toutes les images")
                onClicked: ribbon.editor.clearImageSelection()
            }
        }

        ListView {
            id: thumbnailList

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            orientation: ListView.Horizontal
            spacing: 5
            cacheBuffer: ribbon.thumbnailPrefetchCount *
                         (ribbon.thumbnailDelegateWidth + spacing)
            model: ribbon.editor.imageModel
            property int thumbnailFirst: 0
            property int thumbnailLast: -1

            function updateThumbnailWindow() {
                if (!ribbon.visible || count === 0 || width <= 0) {
                    thumbnailFirst = 0
                    thumbnailLast = -1
                    ribbon.editor.setThumbnailWindow(0, -1, 0)
                    return
                }

                const viewportLeft = Math.max(0, contentX)
                const itemExtent = ribbon.thumbnailDelegateWidth + spacing
                const firstVisible = Math.max(0, Math.floor(viewportLeft / itemExtent))
                const lastVisible = Math.min(count - 1, Math.ceil(
                    (viewportLeft + width) / itemExtent) - 1)
                thumbnailFirst = Math.max(0, firstVisible - ribbon.thumbnailPrefetchCount)
                thumbnailLast = Math.min(count - 1,
                                         lastVisible + ribbon.thumbnailPrefetchCount)
                ribbon.editor.setThumbnailWindow(firstVisible, lastVisible,
                                                 ribbon.thumbnailPrefetchCount)
            }

            Timer {
                id: thumbnailWindowTimer

                interval: 40
                repeat: false
                onTriggered: thumbnailList.updateThumbnailWindow()
            }

            onContentXChanged: thumbnailWindowTimer.restart()
            onWidthChanged: thumbnailWindowTimer.restart()
            onCountChanged: thumbnailWindowTimer.restart()
            Component.onCompleted: thumbnailWindowTimer.restart()

            delegate: ItemDelegate {
                id: imageDelegate

                required property string fileName
                required property url fileUrl
                required property int index

                width: ribbon.thumbnailDelegateWidth
                height: ribbon.thumbnailDelegateHeight
                highlighted: ListView.isCurrentItem
                onClicked: {
                    thumbnailList.currentIndex = index
                    ribbon.editor.loadImage(fileUrl)
                }

                contentItem: ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 4
                    spacing: 2

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 51
                        color: "#20262f"
                        radius: 3
                        border.width: 1
                        border.color: "#465364"
                        clip: true

                        Image {
                            id: thumbnailImage

                            anchors.fill: parent
                            anchors.margins: 1
                            source: index >= thumbnailList.thumbnailFirst &&
                                    index <= thumbnailList.thumbnailLast
                                    ? ribbon.editor.thumbnailSource(fileUrl) : ""
                            sourceSize.width: 116
                            sourceSize.height: 49
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            cache: false
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: thumbnailImage.status !== Image.Ready
                            text: fileName.split(".").pop().toUpperCase()
                            color: "#8c9caf"
                            font.bold: true
                            font.pixelSize: 10
                        }

                        PreferenceCheckBox {
                            id: selectionCheck
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.margins: 1
                            checked: ribbon.editor.selectedImageUrls.indexOf(
                                         imageDelegate.fileUrl.toString()) >= 0
                            onToggled: ribbon.editor.setImageSelected(
                                           imageDelegate.fileUrl, checked)
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: fileName
                        elide: Text.ElideRight
                        color: imageDelegate.highlighted ? "#ffffff" : palette.windowText
                        font.pixelSize: 11
                    }
                }

                background: Rectangle {
                    radius: 4
                    color: selectionCheck.checked ? "#38586c"
                          : (imageDelegate.highlighted ? "#465a71"
                          : (imageDelegate.hovered ? "#343d49" : "transparent")
                             )
                }
            }

            ScrollBar.horizontal: ScrollBar {
                id: thumbnailScrollBar

                policy: ScrollBar.AsNeeded
                orientation: Qt.Horizontal
                height: 6
                padding: 0
                hoverEnabled: true

                contentItem: Rectangle {
                    implicitHeight: 6
                    radius: height / 2
                    color: "#8c9caf"
                    opacity: thumbnailScrollBar.pressed || thumbnailScrollBar.hovered
                             ? 0.58 : 0.24

                    Behavior on opacity {
                        NumberAnimation { duration: 140 }
                    }
                }

                background: Item { }
            }

            Label {
                anchors.centerIn: parent
                visible: thumbnailList.count === 0
                text: qsTr("Sélectionnez un dossier pour afficher ses images.")
                color: palette.windowText
                font.pixelSize: 12
            }
        }
    }
}
