import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: panel

    required property var editor
    property var preferences: null
    readonly property int informationFontSize: preferences ? preferences.layout.imageInfoFontSize : 12
    readonly property var details: editor ? editor.imageDetails : ({})

    color: "#2a3039"
    radius: 8
    border.color: "#3d4654"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        Label {
            text: qsTr("Informations")
            font.bold: true
            font.pixelSize: panel.informationFontSize + 4
            color: "#edf2f8"
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: "#3d4654"
        }

        ScrollView {
            id: infoScrollView

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical: ScrollBar {
                id: infoScrollBar

                policy: ScrollBar.AsNeeded
                orientation: Qt.Vertical
                width: 6
                padding: 0
                hoverEnabled: true

                contentItem: Rectangle {
                    implicitWidth: 6
                    radius: width / 2
                    color: "#8c9caf"
                    opacity: infoScrollBar.pressed || infoScrollBar.hovered
                             ? 0.58 : 0.24

                    Behavior on opacity {
                        NumberAnimation { duration: 140 }
                    }
                }

                background: Item { }
            }

            ColumnLayout {
                width: infoScrollView.availableWidth
                spacing: 9

                Label {
                    Layout.fillWidth: true
                    visible: !panel.editor.hasImage
                    text: qsTr("Ouvrez une image pour afficher ses informations.")
                    font.pixelSize: panel.informationFontSize
                    wrapMode: Text.WordWrap
                    color: "#aeb8c8"
                }

                Repeater {
                    model: [
                        { "label": qsTr("Nom"), "value": panel.details.name || "—" },
                        { "label": qsTr("Date"), "value": panel.details.date || "—" },
                        { "label": qsTr("Lieu"), "value": panel.details.location || "—" },
                        { "label": qsTr("Zone de stockage"), "value": panel.details.storage || "—" },
                        { "label": qsTr("Notation"), "value": panel.details.rating || "—" },
                        { "label": qsTr("Libellé couleur"), "value": panel.details.colorLabel || "—" },
                        { "label": qsTr("Marque / boîtier"), "value": panel.details.brand || "—" },
                        { "label": qsTr("Objectif"), "value": panel.details.lens || "—" },
                        { "label": qsTr("Format"), "value": panel.details.format || "—" },
                        { "label": qsTr("Dimensions"), "value": panel.details.dimensions || "—" },
                        { "label": qsTr("Taille"), "value": panel.details.fileSize || "—" }
                    ]

                    delegate: ColumnLayout {
                        required property var modelData

                        Layout.fillWidth: true
                        visible: panel.editor.hasImage
                        spacing: 2

                        Label {
                            text: modelData.label
                            color: "#8c9caf"
                            font.pixelSize: panel.informationFontSize - 1
                        }
                        Label {
                            Layout.fillWidth: true
                            text: modelData.value
                            color: "#d7dfeb"
                            font.pixelSize: panel.informationFontSize
                            wrapMode: Text.WordWrap
                            maximumLineCount: 3
                            elide: Text.ElideRight
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    visible: panel.editor.hasImage
                    color: "#3d4654"
                }

                Label {
                    visible: panel.editor.hasImage
                    text: qsTr("Classement")
                    color: "#8c9caf"
                    font.pixelSize: panel.informationFontSize - 1
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: panel.editor.hasImage
                    spacing: 2

                    Repeater {
                        model: 5

                        delegate: ToolButton {
                            id: ratingButton
                            required property int index
                            objectName: "imageRatingStar" + index

                            Layout.preferredWidth: 23.1
                            Layout.preferredHeight: 23.1
                            text: index < panel.editor.imageRating ? "★" : "☆"
                            font.pixelSize: 18
                            padding: 0
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("%1 étoile(s)").arg(index + 1)
                            onClicked: panel.editor.setImageRating(
                                           panel.editor.imageRating === index + 1
                                           ? 0 : index + 1)

                            contentItem: Text {
                                objectName: "imageRatingGlyph" + ratingButton.index
                                font: ratingButton.font
                                text: parent.text
                                color: parent.hovered ? "#f3c969" : "#d7b35e"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Item { }
                        }
                    }

                    Item { Layout.fillWidth: true }

                    ToolButton {
                        id: flagButton

                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 22
                        text: panel.editor.imageFlagged ? "⚑" : "⚐"
                        font.pixelSize: 17
                        padding: 0
                        ToolTip.visible: hovered
                        ToolTip.text: panel.editor.imageFlagged
                                      ? qsTr("Retirer le drapeau")
                                      : qsTr("Marquer avec un drapeau")
                        onClicked: panel.editor.setImageFlagged(!panel.editor.imageFlagged)

                        contentItem: Text {
                            text: parent.text
                            color: panel.editor.imageFlagged ? "#edf2f8" : "#8c9caf"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 3
                            color: flagButton.hovered ? "#3d4654" : "transparent"
                        }
                    }
                }

                Label {
                    visible: panel.editor.hasImage
                    text: qsTr("Libellé couleur")
                    color: "#8c9caf"
                    font.pixelSize: panel.informationFontSize - 1
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: panel.editor.hasImage
                    spacing: 6

                    Repeater {
                        model: [
                            { "name": qsTr("Rouge"), "color": "#dc5a63" },
                            { "name": qsTr("Jaune"), "color": "#e7c95b" },
                            { "name": qsTr("Vert"), "color": "#72b879" },
                            { "name": qsTr("Bleu"), "color": "#649edc" },
                            { "name": qsTr("Violet"), "color": "#a77ad3" }
                        ]

                        delegate: ToolButton {
                            required property var modelData

                            Layout.preferredWidth: 20
                            Layout.preferredHeight: 20
                            padding: 0
                            ToolTip.visible: hovered
                            ToolTip.text: modelData.name
                            onClicked: panel.editor.setImageColorLabel(
                                           panel.editor.imageColorLabel === modelData.name
                                           ? "" : modelData.name)

                            background: Rectangle {
                                radius: width / 2
                                color: modelData.color
                                border.width: panel.editor.imageColorLabel === modelData.name
                                              ? 3 : 1
                                border.color: panel.editor.imageColorLabel === modelData.name
                                              ? "#ffffff" : "#3d4654"
                            }
                        }
                    }
                    Item { Layout.fillWidth: true }
                }
            }
        }
    }
}
