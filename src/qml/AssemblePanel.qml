import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// The Assemble tab: makes a new image from the photos selected in the ribbon. The work
// stays in the engine (editor.stacking), which runs in the background and holds the result
// until it is exported or replaced.
Item {
    id: root
    objectName: "assemblePanel"

    required property var editor
    property var preferences: null
    readonly property color textColor: preferences ? preferences.theme.text : "#edf2f8"
    readonly property color secondaryTextColor: preferences ? preferences.theme.secondaryText : "#d7dee8"
    readonly property var session: editor.stacking
    readonly property var photos: {
        const urls = Array.from(editor.selectedImageUrls || [])
        urls.sort()
        return urls
    }
    readonly property bool busy: session.running || session.exporting
    readonly property var formats: [
        { label: qsTr("DNG : signal caméra, à retoucher"), value: "DNG",
          note: qsTr("Se rouvre comme un RAW : le profil, la balance et les réglages restent libres.") },
        { label: qsTr("TIFF : image rendue, 16 bits"), value: "TIFF",
          note: qsTr("Lisible partout, avec le profil de la photo de référence appliqué.") }
    ]
    readonly property string format: formats[formatSelector.currentIndex].value
    readonly property var modes: [
        { label: qsTr("Empilement"), value: "stack",
          note: qsTr("Réduit le bruit d'une série de la même scène, à la même exposition (lune, ciel nocturne, intérieur sombre).") },
        { label: qsTr("Panorama"), value: "panorama",
          note: qsTr("Assemble des photos prises du même endroit en une seule vue large : une rangée, horizontale ou verticale, ou plusieurs. Le bas de la vue reste de niveau.") }
    ]
    readonly property string mode: modes[modeSelector.currentIndex].value
    readonly property var projections: [
        { label: qsTr("Automatique"), value: "automatic" },
        { label: qsTr("Cylindrique"), value: "cylindrical" },
        { label: qsTr("Sphérique"), value: "spherical" },
        { label: qsTr("Plane"), value: "rectilinear" }
    ]
    readonly property var edgeChoices: [
        { label: qsTr("Recadrer"), value: "crop",
          note: qsTr("Garde le plus grand rectangle de pixels réels, sans inventer. Coupe plutôt par le haut.") },
        { label: qsTr("Remplir"), value: "fill",
          note: qsTr("Garde toute la surface et complète les coins avec des pixels inventés à partir du reste de l'image. Convaincant sur le ciel, à vérifier sur la roche ou la végétation.") },
        { label: qsTr("Laisser vides"), value: "keep",
          note: qsTr("Garde toute la surface, avec ses coins vides, à recadrer ensuite.") }
    ]

    function fileName(url) {
        return decodeURIComponent(String(url).split("/").pop())
    }

    function assemble() {
        if (mode === "panorama")
            session.startPanorama(photos, projections[projectionSelector.currentIndex].value, edgeChoices[edgesSelector.currentIndex].value)
        else
            session.start(photos, photos[referenceSelector.currentIndex] || "")
    }

    function exportTo(target) {
        session.exportResult(target, format)
    }

    function openExportDialog() {
        const suggested = session.suggestedExport(format)
        if (suggested.toString().length > 0)
            exportDialog.currentFile = suggested
        exportDialog.open()
    }

    // The middle photo by default: it stays the closest to the others. Later, because the
    // list goes back to its first entry when its model follows the same change.
    function chooseMiddleReference() {
        referenceSelector.currentIndex = Math.floor(photos.length / 2)
    }
    onPhotosChanged: Qt.callLater(chooseMiddleReference)

    Label {
        id: title
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        text: qsTr("Assembler")
        font.bold: true
        font.pixelSize: 18
        color: root.textColor
        padding: 0
        background: Item { }
    }

    ScrollView {
        id: scroll
        objectName: "assembleScrollView"
        anchors.top: title.bottom
        anchors.topMargin: 8
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        rightPadding: 8
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical: PanelScrollBar {
            parent: scroll
            x: scroll.width - width
            y: 0
            height: scroll.height
        }

        ColumnLayout {
            width: scroll.availableWidth
            spacing: 10

            SectionNote {
                text: qsTr("Crée une nouvelle image à partir des photos sélectionnées dans le ruban. Calcul long, en tâche de fond ; les photos d'origine ne changent pas.")
            }
            PreferenceComboBox {
                id: modeSelector
                objectName: "assembleModeSelector"
                Layout.fillWidth: true
                enabled: !root.busy
                model: root.modes
                textRole: "label"
            }
            SectionNote {
                objectName: "assembleModeNote"
                text: root.modes[modeSelector.currentIndex].note
            }
            Label {
                objectName: "assembleSelectionCount"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: root.textColor
                padding: 0
                background: Item { }
                text: root.photos.length >= 2 ? qsTr("%1 photos sélectionnées").arg(root.photos.length)
                                              : qsTr("Sélectionnez au moins deux photos dans le ruban.")
            }
            RowLayout {
                Layout.fillWidth: true
                visible: root.mode === "stack" && root.photos.length >= 2
                Label {
                    text: qsTr("Référence")
                    color: root.secondaryTextColor
                }
                PreferenceComboBox {
                    id: referenceSelector
                    objectName: "assembleReferenceSelector"
                    Layout.fillWidth: true
                    enabled: !root.busy
                    model: root.photos.map(root.fileName)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                visible: root.mode === "panorama"
                Label {
                    text: qsTr("Projection")
                    color: root.secondaryTextColor
                }
                PreferenceComboBox {
                    id: projectionSelector
                    objectName: "assembleProjectionSelector"
                    Layout.fillWidth: true
                    enabled: !root.busy
                    model: root.projections
                    textRole: "label"
                }
            }
            RowLayout {
                Layout.fillWidth: true
                visible: root.mode === "panorama"
                Label {
                    text: qsTr("Bords")
                    color: root.secondaryTextColor
                }
                PreferenceComboBox {
                    id: edgesSelector
                    objectName: "assembleEdgesSelector"
                    Layout.fillWidth: true
                    enabled: !root.busy
                    model: root.edgeChoices
                    textRole: "label"
                }
            }
            SectionNote {
                objectName: "assembleEdgesNote"
                visible: root.mode === "panorama"
                text: root.edgeChoices[edgesSelector.currentIndex].note
            }
            PreferenceButton {
                objectName: "assembleStartButton"
                Layout.fillWidth: true
                visible: !root.session.running
                text: root.session.hasResult ? qsTr("Assembler à nouveau") : qsTr("Assembler")
                enabled: root.photos.length >= 2 && !root.busy
                onClicked: root.assemble()
            }
            ProgressBar {
                objectName: "assembleProgress"
                Layout.fillWidth: true
                visible: root.session.running
                value: root.session.progress
            }
            PreferenceButton {
                objectName: "assembleCancelButton"
                Layout.fillWidth: true
                visible: root.session.running
                text: qsTr("Annuler")
                onClicked: root.session.cancel()
            }
            SectionNote {
                objectName: "assembleStatus"
                visible: text.length > 0
                text: root.session.status
            }

            RowLayout {
                Layout.fillWidth: true
                visible: root.session.hasResult
                Label {
                    text: qsTr("Format")
                    color: root.secondaryTextColor
                }
                PreferenceComboBox {
                    id: formatSelector
                    objectName: "assembleFormatSelector"
                    Layout.fillWidth: true
                    enabled: !root.busy
                    model: root.formats
                    textRole: "label"
                }
            }
            SectionNote {
                visible: root.session.hasResult
                text: root.formats[formatSelector.currentIndex].note
            }
            PreferenceButton {
                objectName: "assembleExportButton"
                Layout.fillWidth: true
                visible: root.session.hasResult
                enabled: root.session.hasResult && !root.busy
                text: qsTr("Exporter…")
                onClicked: root.openExportDialog()
            }
            PreferenceButton {
                objectName: "assembleOpenButton"
                Layout.fillWidth: true
                visible: root.session.exportedFile.toString().length > 0
                enabled: !root.busy
                text: qsTr("Ouvrir le résultat")
                onClicked: root.editor.loadImage(root.session.exportedFile)
            }
            // Below the export, which stays in view at the end of the work.
            Image {
                objectName: "assemblePreview"
                Layout.fillWidth: true
                Layout.preferredHeight: visible && implicitWidth > 0 ? width * implicitHeight / implicitWidth : 0
                visible: root.session.preview.toString().length > 0
                source: root.session.preview
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: false
            }
        }
    }

    FileDialog {
        id: exportDialog
        objectName: "assembleExportDialog"
        title: qsTr("Exporter l'image assemblée")
        fileMode: FileDialog.SaveFile
        // Existing names are numbered by the exporter, never replaced.
        options: FileDialog.DontConfirmOverwrite
        nameFilters: root.format === "DNG" ? [qsTr("DNG linéaire (*.dng)")] : [qsTr("Images TIFF (*.tif *.tiff)")]
        defaultSuffix: root.format === "DNG" ? "dng" : "tiff"
        onAccepted: root.exportTo(selectedFile)
    }
}
