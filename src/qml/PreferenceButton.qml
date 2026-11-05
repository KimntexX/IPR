import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.Button {
    id: control
    // Set by the window's CommandErrorPopup when what this button asked for failed.
    property bool errorShown: false
    implicitHeight: 30
    z: errorShown ? 1 : 0
    scale: errorShown ? 1.08 : 1
    Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutQuad } }
    onReleased: {
        const window = control.Window.window
        if (window && window.commandButtonReleased)
            window.commandButtonReleased(control)
    }
    padding: 7
    hoverEnabled: true
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.palette.buttonText
        opacity: control.enabled ? 1 : 0.6
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 4
        color: control.down || control.hovered ? control.palette.mid : control.palette.button
        border.color: control.errorShown ? "#e55353" : control.activeFocus ? control.palette.text : control.palette.mid
        border.width: control.errorShown || control.activeFocus ? 2 : 1
        opacity: control.enabled ? 1 : 0.6
    }
}
