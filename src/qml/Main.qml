pragma ComponentBehavior: Bound

import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import IPR

ApplicationWindow {
    id: root

    width: 1440
    height: 900
    minimumWidth: 1000
    minimumHeight: 650
    visible: false
    title: qsTr("IPR — Image Processing Room")
    color: uiPreferences.theme.background
    palette.window: uiPreferences.theme.panel
    palette.windowText: uiPreferences.theme.text
    palette.text: uiPreferences.theme.text
    palette.buttonText: uiPreferences.theme.text
    palette.base: Qt.darker(uiPreferences.theme.panel, 1.10)
    palette.button: Qt.darker(uiPreferences.theme.panel, 1.10)
    palette.mid: Qt.tint(uiPreferences.theme.panel, Qt.rgba(
        uiPreferences.theme.text.r, uiPreferences.theme.text.g, uiPreferences.theme.text.b, 0.25))
    palette.highlight: Qt.tint(uiPreferences.theme.panel, Qt.rgba(
        uiPreferences.theme.text.r, uiPreferences.theme.text.g, uiPreferences.theme.text.b, 0.18))
    palette.highlightedText: uiPreferences.theme.text
    readonly property bool folderOpen: imageEditor.folder.toString().length > 0

    Component.onCompleted: uiPreferences.restoreWindow(root)
    onClosing: uiPreferences.saveWindow(root)

    menuBar: MenuBar {
        id: applicationMenuBar
        background: Rectangle { color: uiPreferences.theme.panel }
        delegate: MenuBarItem {
            id: menuEntry
            contentItem: Text {
                text: menuEntry.text
                font: menuEntry.font
                color: uiPreferences.theme.text
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }

        Menu {
            title: qsTr("Fichier")

            MenuItem {
                text: qsTr("Ouvrir un dossier…")
                onTriggered: folderDialog.open()
            }

            MenuItem {
                text: qsTr("Ouvrir une image…")
                onTriggered: imageFileDialog.open()
            }

            MenuItem {
                text: qsTr("Quitter")
                onTriggered: Qt.quit()
            }
        }

        Menu {
            title: qsTr("Affichage")
            MenuItem {
                text: qsTr("Informations de l'image")
                checkable: true
                checked: uiPreferences.layout.showImageInfo
                enabled: uiPreferences.writable
                onTriggered: uiPreferences.setValue("layout/showImageInfo", checked)
            }
            MenuItem {
                text: qsTr("Histogramme")
                checkable: true
                checked: uiPreferences.layout.showHistogram
                enabled: uiPreferences.writable
                onTriggered: uiPreferences.setValue("layout/showHistogram", checked)
            }
            MenuItem {
                text: qsTr("Ruban des vignettes")
                checkable: true
                checked: uiPreferences.layout.showFilmstrip
                enabled: uiPreferences.writable
                onTriggered: uiPreferences.setValue("layout/showFilmstrip", checked)
            }
        }
        Menu {
            title: qsTr("Préférences")
            MenuItem {
                action: Action {
                    text: qsTr("Interface et développement RAW…")
                    shortcut: "Ctrl+,"
                    onTriggered: preferencesDialog.open()
                }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 12
        anchors.bottomMargin: uiPreferences.layout.showFilmstrip
                              ? uiPreferences.layout.filmstripHeight + 16 : 12
        spacing: 12

        ImageInfoPanel {
            preferences: uiPreferences
            objectName: "imageInfoPanel"
            Layout.preferredWidth: uiPreferences.layout.imageInfoWidth
            Layout.minimumWidth: 210
            Layout.maximumWidth: 300
            Layout.fillHeight: true
            visible: uiPreferences.layout.showImageInfo
            color: uiPreferences.theme.panel
            editor: imageEditor
        }

        ImageCanvasBackground {
            objectName: "imageCanvasBackground"
            Layout.fillWidth: true
            Layout.fillHeight: true
            baseColor: uiPreferences.theme.canvas
            radius: 8
            border.color: "#3d4654"
            clip: true

            ImageViewport {
                id: canvas

                anchors.fill: parent
                anchors.margins: 12
                source: imageEditor.hasImage ? imageEditor.imageSource : ""
                originalSource: imageEditor.hasImage ? imageEditor.originalImageSource : ""
                imageKey: (imageEditor.imageDetails.storage || "") + "/"
                          + (imageEditor.imageDetails.name || "")
                hasImage: imageEditor.hasImage
                textColor: uiPreferences.theme.text
                panelColor: uiPreferences.theme.panel
                visible: imageEditor.hasImage
            }

            // Interim camera rendering (embedded JPEG), not the IPR development.
            Label {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.margins: 20
                visible: imageEditor.hasImage && imageEditor.embeddedPreview
                text: qsTr("Aperçu boîtier — développement en cours")
                color: uiPreferences.theme.text
                padding: 6
                background: Rectangle { color: uiPreferences.theme.panel; opacity: 0.85; radius: 4 }
            }

            ColumnLayout {
                anchors.centerIn: parent
                visible: !imageEditor.hasImage && !root.folderOpen
                spacing: 12

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    color: "#aeb8c8"
                    font.pixelSize: 18
                    text: qsTr("Ouvrez un dossier ou une image")
                }

                Button {
                    id: openButton

                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Ouvrir…")
                    implicitHeight: 30
                    leftPadding: 16
                    rightPadding: 16
                    onClicked: imageFileDialog.open()

                    contentItem: Text {
                        text: openButton.text
                        color: openButton.enabled ? "#edf2f8" : "#7a8797"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 3
                        color: openButton.down ? "#3d4654"
                               : (openButton.hovered ? "#343d49" : "#2d343e")
                        border.width: 1
                        border.color: "#536174"
                    }
                }

            }

            Label {
                anchors.centerIn: parent
                visible: !imageEditor.hasImage && root.folderOpen
                color: "#aeb8c8"
                font.pixelSize: 18
                text: qsTr("Sélectionnez une image dans le ruban")
            }

            Rectangle {
                objectName: "imageStatusOverlay"
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: 12
                radius: 4
                color: "#b820242b"
                visible: imageEditor.hasImage
                width: Math.min(infoLabel.implicitWidth + 20,
                                Math.max(0, parent.width - 24 -
                                         (uiPreferences.layout.showFilmstrip ? 0 : 80)))
                height: infoLabel.implicitHeight + 10

                Label {
                    id: infoLabel

                    anchors.centerIn: parent
                    width: Math.max(0, parent.width - 20)
                    text: imageEditor.imageInfo
                    elide: Text.ElideRight
                    color: "#edf2f8"
                    font.pixelSize: 12
                }
            }
        }
        Rectangle {
            id: settingsPanel
            objectName: "settingsPanel"

            Layout.preferredWidth: uiPreferences.layout.settingsWidth
            Layout.minimumWidth: 260
            Layout.maximumWidth: 360
            Layout.fillHeight: true
            color: uiPreferences.theme.panel
            radius: 8
            border.color: "#3d4654"
            focus: true

            Setting {
                anchors.fill: parent
                anchors.margins: 14
                editor: imageEditor
                preferences: uiPreferences
            }

        }
    }

    ThumbnailRibbon {
        id: filmstrip
        activeImageIndex: filmstripNavigation.currentIndex
        objectName: "thumbnailRibbon"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        anchors.bottomMargin: 4
        height: uiPreferences.layout.filmstripHeight
        visible: uiPreferences.layout.showFilmstrip
        color: uiPreferences.theme.panel
        editor: imageEditor
    }

    FilmstripNavigation {
        id: filmstripNavigation
        objectName: "collapsedFilmstripNavigation"
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: uiPreferences.layout.settingsWidth + 28
        anchors.bottomMargin: 14
        visible: !uiPreferences.layout.showFilmstrip
        editor: imageEditor
    }

    footer: Label {
        id: applicationFooter

        leftPadding: 12
        rightPadding: width / 2 + 28
        height: Math.max(22, implicitHeight)
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        color: uiPreferences.theme.secondaryText
        text: imageEditor.statusMessage + (imageEditor.memoryCacheStatus && imageEditor.memoryCacheStatus.available
            ? qsTr(" | Cache : %1 image(s), %2 Mio%3")
                .arg(imageEditor.decodedImageCacheEntryCount)
                .arg(Math.round(imageEditor.decodedImageCacheSizeBytes / 1048576))
                .arg(imageEditor.memoryCacheStatus.pressure ? qsTr(" — pression mémoire") : "") : "")
        background: Rectangle { color: uiPreferences.theme.panel }
        ToolButton {
            objectName: "filmstripToggleButton"
            anchors.centerIn: parent
            text: uiPreferences.layout.showFilmstrip ? "▾" : "▴"
            implicitWidth: 40
            implicitHeight: 18
            padding: 0
            hoverEnabled: true
            enabled: uiPreferences.writable
            Accessible.name: uiPreferences.layout.showFilmstrip
                ? qsTr("Replier le ruban des images") : qsTr("Afficher le ruban des images")
            ToolTip.visible: hovered
            ToolTip.text: Accessible.name
            onClicked: uiPreferences.setValue("layout/showFilmstrip", !uiPreferences.layout.showFilmstrip)
            contentItem: Text {
                text: parent.text
                color: uiPreferences.theme.secondaryText
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle { radius: 3; color: uiPreferences.theme.panel }
        }
    }

    PreferencesDialog {
        id: preferencesDialog
        objectName: "preferencesDialog"
        editor: imageEditor
        preferences: uiPreferences
    }

    FolderDialog {
        id: folderDialog

        title: qsTr("Choisir le dossier d'images")
        currentFolder: StandardPaths.standardLocations(
                           StandardPaths.PicturesLocation)[0]
        onAccepted: imageEditor.openFolder(selectedFolder)
    }

    FileDialog {
        id: imageFileDialog

        title: qsTr("Ouvrir une image")
        fileMode: FileDialog.OpenFile
        nameFilters: imageEditor.imageFileFilters()
        onAccepted: imageEditor.openImage(selectedFile)
    }
}
