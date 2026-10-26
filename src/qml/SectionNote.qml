import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Help or status text under a group of settings: the same size, colour and
// wrapping in every section.
Label {
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    color: palette.windowText
    opacity: 0.72
    font.pixelSize: 11
    wrapMode: Text.WordWrap
}
