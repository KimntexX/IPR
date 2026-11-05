import QtQuick
import QtQuick.Controls.Basic as Basic

// A button whose command can be refused. The refusal shows where the click was: a red edge, a
// slightly larger button and a message under it. The engine's own message stays in the footer.
PreferenceButton {
    id: control

    property string errorDetail: ""

    function showError(detail) {
        errorDetail = detail
        errorShown = true
        errorPopup.open()
        errorTimer.restart()
    }

    function clearError() {
        errorTimer.stop()
        errorPopup.close()
        errorShown = false
    }

    z: errorShown ? 1 : 0
    scale: errorShown ? 1.08 : 1
    Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutQuad } }

    Timer {
        id: errorTimer
        interval: 4000
        onTriggered: control.clearError()
    }

    Basic.Popup {
        id: errorPopup
        objectName: "refusalPopup"
        x: (control.width - width) / 2
        y: control.height + 6
        width: 280
        margins: 8
        padding: 8
        closePolicy: Basic.Popup.CloseOnEscape | Basic.Popup.CloseOnPressOutside
        onClosed: control.errorShown = false
        background: Rectangle {
            radius: 4
            color: control.palette.window
            border.color: control.errorColor
            border.width: 2
        }
        contentItem: Column {
            spacing: 4
            Accessible.role: Accessible.AlertMessage
            Accessible.name: qsTr("Erreur") + " : " + control.errorDetail
            Basic.Label {
                width: parent.width
                text: qsTr("Erreur")
                color: control.errorColor
                font.bold: true
                font.pointSize: 12
            }
            Basic.Label {
                width: parent.width
                text: control.errorDetail
                color: control.palette.windowText
                wrapMode: Text.WordWrap
                font.pointSize: 12
            }
        }
    }
}
