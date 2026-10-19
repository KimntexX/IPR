import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts

Basic.Pane {
    id: root
    property bool busy: false
    property bool embeddedPreview: false
    property string detail: ""
    property bool delayed: false
    readonly property string caption: embeddedPreview
        ? qsTr("Aperçu boîtier — développement en cours…") : qsTr("Traitement en cours…")
    visible: embeddedPreview || (busy && delayed)
    implicitWidth: Math.min(360, statusRow.implicitWidth + 20)
    implicitHeight: statusRow.implicitHeight + 12
    padding: 0
    background: Rectangle { radius: 4; color: root.palette.window; border.color: root.palette.mid }
    // Suppress flashes from short operations; never invent a percentage.
    onBusyChanged: {
        delayed = false
        if (busy) delay.restart()
        else delay.stop()
    }
    Component.onCompleted: if (busy) delay.start()
    Timer { id: delay; interval: 180; onTriggered: root.delayed = true }
    RowLayout {
        id: statusRow
        anchors.fill: parent
        anchors.margins: 6
        spacing: 6
        BusyIndicator {
            running: root.visible && root.busy
            Layout.preferredWidth: 20
            Layout.preferredHeight: 20
        }
        Label {
            Layout.fillWidth: true
            text: root.caption
            color: palette.windowText
            font.pixelSize: 12
            elide: Text.ElideRight
        }
    }
    HoverHandler { id: hover }
    ToolTip.visible: hover.hovered && root.detail.length > 0
    ToolTip.text: root.detail
    Accessible.name: caption
    Accessible.description: detail
}
