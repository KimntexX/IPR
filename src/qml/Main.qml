pragma ComponentBehavior: Bound

import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
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

    menuBar: Basic.MenuBar {
        id: applicationMenuBar
        background: Rectangle { color: uiPreferences.theme.panel }
        delegate: Basic.MenuBarItem {
            id: menuEntry
            contentItem: Text {
                text: menuEntry.text
                font: menuEntry.font
                color: uiPreferences.theme.text
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }

        Basic.Menu {
            palette: root.palette
            title: qsTr("Fichier")

            Basic.MenuItem {
                text: qsTr("Ouvrir un dossier…")
                onTriggered: folderDialog.open()
            }

            Basic.MenuItem {
                text: qsTr("Ouvrir une image…")
                onTriggered: imageFileDialog.open()
            }

            Basic.MenuItem {
                text: qsTr("Quitter")
                onTriggered: Qt.quit()
            }
        }

        Basic.Menu {
            palette: root.palette
            title: qsTr("Affichage")
            Basic.MenuItem {
                text: qsTr("Informations de l'image")
                checkable: true
                checked: uiPreferences.layout.showImageInfo
                enabled: uiPreferences.writable
                onTriggered: uiPreferences.setValue("layout/showImageInfo", checked)
            }
            Basic.MenuItem {
                text: qsTr("Histogramme")
                checkable: true
                checked: uiPreferences.layout.showHistogram
                enabled: uiPreferences.writable
                onTriggered: uiPreferences.setValue("layout/showHistogram", checked)
            }
            Basic.MenuItem {
                text: qsTr("Ruban des vignettes")
                checkable: true
                checked: uiPreferences.layout.showFilmstrip
                enabled: uiPreferences.writable
                onTriggered: uiPreferences.setValue("layout/showFilmstrip", checked)
            }
        }
        Basic.Menu {
            palette: root.palette
            title: qsTr("Préférences")
            Basic.MenuItem {
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
                              ? uiPreferences.layout.filmstripHeight + 34 : 30
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

        PanelResizeHandle {
            objectName: "infoResizeHandle"
            Layout.fillHeight: true
            Layout.preferredWidth: 8
            visible: uiPreferences.layout.showImageInfo
            currentSize: uiPreferences.layout.imageInfoWidth
            minimumSize: 210
            maximumSize: 300
            defaultSize: 250
            writable: uiPreferences.writable
            handleColor: uiPreferences.theme.secondaryText
            description: qsTr("Largeur du panneau Informations")
            onSizeEdited: (size) => uiPreferences.setValue("layout/imageInfoWidth", size)
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

            FilmstripNavigation {
                id: filmstripNavigation
                objectName: "collapsedFilmstripNavigation"
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 12
                z: 2
                visible: !uiPreferences.layout.showFilmstrip
                editor: imageEditor
            }

            // Interim camera rendering (embedded JPEG), not the IPR development.
            ProcessingStatus {
                objectName: "processingStatus"
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.margins: 20
                anchors.topMargin: canvas.toolbarHeight + 20
                width: Math.max(0, Math.min(implicitWidth, parent.width - 40))
                height: implicitHeight
                busy: imageEditor.busy
                embeddedPreview: imageEditor.hasImage && imageEditor.embeddedPreview
                detail: imageEditor.statusMessage
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

                PreferenceButton {
                    id: openButton
                    objectName: "openImageButton"

                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Ouvrir…")
                    implicitHeight: 30
                    leftPadding: 16
                    rightPadding: 16
                    onClicked: imageFileDialog.open()

                    contentItem: Text {
                        text: openButton.text
                        color: openButton.palette.buttonText
                        opacity: openButton.enabled ? 1 : 0.45
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 3
                        color: openButton.down || openButton.hovered ? openButton.palette.mid : openButton.palette.button
                        border.width: 1
                        border.color: openButton.palette.mid
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
        PanelResizeHandle {
            objectName: "settingsResizeHandle"
            Layout.fillHeight: true
            Layout.preferredWidth: 8
            currentSize: uiPreferences.layout.settingsWidth
            minimumSize: 260
            maximumSize: 360
            defaultSize: 320
            direction: -1
            writable: uiPreferences.writable
            handleColor: uiPreferences.theme.secondaryText
            description: qsTr("Largeur du panneau Réglages")
            onSizeEdited: (size) => uiPreferences.setValue("layout/settingsWidth", size)
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

    PanelResizeHandle {
        objectName: "filmstripResizeHandle"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: filmstrip.top
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        height: 8
        orientation: Qt.Vertical
        visible: filmstrip.visible
        currentSize: uiPreferences.layout.filmstripHeight
        minimumSize: 100
        maximumSize: 260
        defaultSize: 120
        direction: -1
        writable: uiPreferences.writable
        handleColor: uiPreferences.theme.secondaryText
        description: qsTr("Hauteur du ruban")
        onSizeEdited: (size) => uiPreferences.setValue("layout/filmstripHeight", size)
    }

        PreferenceButton {
            objectName: "filmstripToggleButton"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: uiPreferences.layout.showFilmstrip ? filmstrip.height + 12 : 0
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
            contentItem: ChevronIcon {
                objectName: "filmstripToggleChevron"
                direction: uiPreferences.layout.showFilmstrip ? "down" : "up"
                strokeColor: uiPreferences.theme.secondaryText
            }
            background: Rectangle { radius: 3; color: uiPreferences.theme.panel }
        }

    footer: Label {
        id: applicationFooter

        leftPadding: 12
        rightPadding: 12
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
