import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: ribbon

    required property var editor
    property int thumbnailPrefetchCount: 4
    property int thumbnailDelegateWidth: 126
    readonly property int thumbnailDelegateHeight: Math.max(0, thumbnailList.height - 8)
    property int activeImageIndex: -1
    signal contextMenuRequested(var item, url fileUrl, string fileName, int index, real x, real y)
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
                      + (ribbon.editor.selectedImageCount > 0
                         ? qsTr(" · %1 sélectionnée(s)").arg(ribbon.editor.selectedImageCount) : "")
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
            objectName: "thumbnailList"

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
                objectName: "thumbnail_" + index
                readonly property bool opened: index === ribbon.activeImageIndex
                readonly property bool batchSelected: selectionCheck.checked

                width: ribbon.thumbnailDelegateWidth
                height: ribbon.thumbnailDelegateHeight
                highlighted: opened
                onClicked: {
                    ribbon.editor.loadImage(fileUrl)
                }
                hoverEnabled: true
                ToolTip.visible: hovered
                ToolTip.text: fileName + (opened ? qsTr(" — image ouverte") : "")
                              + (batchSelected ? qsTr(" — sélectionnée pour traitement groupé") : "")
                Accessible.name: ToolTip.text

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: (mouse) => ribbon.contextMenuRequested(imageDelegate,
                        imageDelegate.fileUrl, imageDelegate.fileName, imageDelegate.index, mouse.x, mouse.y)
                }
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && event.modifiers === Qt.ShiftModifier)) {
                        ribbon.contextMenuRequested(imageDelegate, fileUrl, fileName, index, width / 2, height / 2)
                        event.accepted = true
                    }
                }

                contentItem: ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 4
                    spacing: 2

                    Rectangle {
                        id: thumbnailBox
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 0
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
                            sourceSize.height: Math.max(1, thumbnailBox.height - 2)
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
                            Accessible.name: qsTr("Sélectionner %1 pour traitement groupé").arg(imageDelegate.fileName)
                            checked: ribbon.editor.selectedImageUrls.indexOf(
                                         imageDelegate.fileUrl.toString()) >= 0
                            onToggled: ribbon.editor.setImageSelected(
                                           imageDelegate.fileUrl, checked)
                        }
                        Rectangle {
                            objectName: "currentPhotoBadges_" + imageDelegate.index
                            anchors.left: parent.left
                            anchors.bottom: parent.bottom
                            anchors.margins: 2
                            visible: imageDelegate.opened
                            width: Math.min(parent.width - 4, badgeText.implicitWidth + 8)
                            height: badgeText.implicitHeight + 4
                            radius: 2
                            color: imageDelegate.palette.window
                            Label {
                                id: badgeText
                                anchors.fill: parent
                                anchors.margins: 2
                                text: qsTr("Ouverte")
                                    + (ribbon.editor.imageRating > 0 ? " ★" + ribbon.editor.imageRating : "")
                                    + (ribbon.editor.imageFlagged ? " ⚑" : "")
                                    + (ribbon.editor.imageColorLabel ? " · " + ribbon.editor.imageColorLabel : "")
                                color: imageDelegate.palette.text
                                font.pixelSize: 9
                                elide: Text.ElideRight
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: fileName
                        elide: Text.ElideRight
                        color: palette.windowText
                        font.bold: imageDelegate.opened
                        font.pixelSize: 11
                    }
                }

                background: Rectangle {
                    radius: 4
                    color: imageDelegate.batchSelected ? imageDelegate.palette.highlight
                          : (imageDelegate.hovered ? imageDelegate.palette.mid : "transparent")
                    border.width: imageDelegate.opened ? 2 : 1
                    border.color: imageDelegate.opened || imageDelegate.activeFocus
                                  ? imageDelegate.palette.text : "transparent"
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
