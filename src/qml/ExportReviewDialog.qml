import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts

Basic.Dialog {
    id: root
    property string format: "TIFF"
    property int quality: 92
    property string dimensions: ""
    property url target
    property bool exportEnabled: false
    property string unavailableReason: ""
    readonly property string encoding: format === "DNG" ? qsTr("Rendu linéaire, 16 bits — primaires sRGB")
        : (format === "JPEG" ? qsTr("sRGB, 8 bits — qualité %1").arg(quality) : qsTr("sRGB, 16 bits"))
    signal exportConfirmed()
    function confirm() {
        if (!exportEnabled) return
        exportConfirmed()
        close()
    }
    anchors.centerIn: Overlay.overlay
    title: qsTr("Vérifier l’export")
    width: Math.min(Overlay.overlay ? Overlay.overlay.width - 24 : 520, 520)
    height: Math.min(Overlay.overlay ? Overlay.overlay.height - 24 : 420, 420)
    modal: true
    padding: 14
    standardButtons: Dialog.NoButton
    header: Label {
        text: root.title
        padding: 14
        font.bold: true
        font.pixelSize: 16
        elide: Text.ElideRight
        color: root.palette.text
        background: Rectangle { color: root.palette.window }
    }
    background: Rectangle { color: root.palette.window; border.color: root.palette.mid; radius: 6 }
    contentItem: PreferenceScrollView {
        ColumnLayout {
            width: parent.width
            spacing: 12
            Repeater {
                model: [qsTr("Format : %1").arg(root.format),
                        root.encoding,
                        qsTr("Dimensions : %1").arg(root.dimensions || qsTr("indisponibles")),
                        qsTr("Destination demandée :\n%1").arg(root.target)]
                Label {
                    required property string modelData
                    Layout.fillWidth: true
                    text: modelData
                    color: root.palette.text
                    wrapMode: Text.WrapAnywhere
                    lineHeight: 1.5
                    textFormat: Text.PlainText
                }
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("L’extension sera adaptée au format. Un numéro sera ajouté si le nom est pris ; aucun fichier existant ne sera remplacé.")
                color: root.palette.windowText
                wrapMode: Text.WordWrap
                lineHeight: 1.5
            }
            Label {
                Layout.fillWidth: true
                visible: root.format === "DNG"
                text: qsTr("Ce DNG contient le rendu développé, pas la mosaïque RAW d’origine.")
                color: root.palette.windowText
                wrapMode: Text.WordWrap
                lineHeight: 1.5
            }
            Label {
                Layout.fillWidth: true
                visible: !root.exportEnabled
                text: root.unavailableReason
                color: root.palette.windowText
                wrapMode: Text.WordWrap
                lineHeight: 1.5
            }
        }
    }
    footer: ConfirmationFooter {
        confirmObjectName: "confirmExportButton"
        confirmText: qsTr("Exporter")
        confirmEnabled: root.exportEnabled
        onCancelled: root.close()
        onConfirmed: root.confirm()
    }
}
