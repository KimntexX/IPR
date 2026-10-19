import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts

Basic.MenuItem {
    id: control
    implicitHeight: 32
    padding: 8
    readonly property string shortcutHint: {
        if (!action) return ""
        const key = action.shortcut
        const command = Qt.platform.os === "osx" ? "Cmd" : "Ctrl"
        if (key === StandardKey.Open) return command + "+O"
        if (key === StandardKey.Save) return command + "+S"
        const label = key ? String(key).replace("Shift", "Maj") : ""
        return Qt.platform.os === "osx" ? label.replace("Ctrl", "Cmd") : label
    }
    contentItem: RowLayout {
        spacing: 16
        opacity: control.enabled ? 1 : 0.45
        Text {
            text: control.text
            textFormat: Text.PlainText
            color: control.palette.text
            font: control.font
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            leftPadding: control.checkable ? 20 : 0
            elide: Text.ElideRight
        }
        Text {
            text: control.shortcutHint
            color: control.palette.windowText
            font.pixelSize: 11
            visible: text.length > 0
        }
    }
    background: Rectangle {
        color: control.down || control.highlighted ? control.palette.mid : "transparent"
        border.width: control.activeFocus ? 1 : 0
        border.color: control.palette.text
    }
}
