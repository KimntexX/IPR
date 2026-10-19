import QtQuick

// A checkbox that always shows the engine's state. A click only requests a
// change; the display then follows the engine again. With a plain
// `checked: binding`, the click replaces the binding by a fixed value, and the
// box stops following the engine (next image, refused change).
PreferenceCheckBox {
    id: box
    property bool backendChecked: false
    signal changeRequested(bool checked)
    checked: backendChecked
    onClicked: {
        changeRequested(checked)
        checked = Qt.binding(function() { return box.backendChecked })
    }
}
