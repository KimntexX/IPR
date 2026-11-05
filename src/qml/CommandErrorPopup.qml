import QtQuick
import QtQuick.Controls.Basic as Basic

// What the engine reports as failed, shown where the user just acted: on the button released a
// moment ago when there is one, otherwise above the footer. It stays two seconds, then fades.
Basic.Popup {
    id: root
    objectName: "commandErrorPopup"

    property var editor: null
    property real restingGap: 0
    property string detail: ""
    property Item shownOn: null
    property Item releasedButton: null
    property real releasedAt: 0
    property real buttonX: 0
    property real buttonY: 0
    readonly property int shownMilliseconds: 2000
    // A command that fails later than this is no longer the answer to that click.
    readonly property int answerMilliseconds: 1000
    readonly property color errorColor: "#e55353"

    function noteRelease(button) {
        releasedButton = button
        releasedAt = Date.now()
    }

    function show(message) {
        const target = releasedButton && releasedButton.visible && Date.now() - releasedAt <= answerMilliseconds
                       ? releasedButton : null
        if (shownOn && shownOn !== target)
            shownOn.errorShown = false
        shownOn = target
        if (target && parent) {
            // Read before the button grows.
            const below = target.mapToItem(parent, target.width / 2, target.height)
            buttonX = below.x
            buttonY = below.y
            target.errorShown = true
        }
        detail = message
        open()
        shownTimer.restart()
    }

    x: Math.round((shownOn ? buttonX : (parent ? parent.width / 2 : 0)) - width / 2)
    y: Math.round(shownOn ? buttonY + 6 : (parent ? parent.height - height - restingGap : 0))
    width: 280
    margins: 8
    padding: 8
    closePolicy: Basic.Popup.CloseOnEscape | Basic.Popup.CloseOnPressOutside

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 80 } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 500; easing.type: Easing.InOutQuad } }

    onAboutToHide: {
        shownTimer.stop()
        if (shownOn)
            shownOn.errorShown = false
    }
    onClosed: shownOn = null

    Timer {
        id: shownTimer
        interval: root.shownMilliseconds
        onTriggered: root.close()
    }

    Connections {
        target: root.editor
        function onErrorReported(message) { root.show(message) }
    }

    background: Rectangle {
        radius: 4
        color: root.palette.window
        border.color: root.errorColor
        border.width: 2
    }

    contentItem: Column {
        spacing: 4
        Accessible.role: Accessible.AlertMessage
        Accessible.name: qsTr("Erreur") + " : " + root.detail
        Basic.Label {
            width: parent.width
            text: qsTr("Erreur")
            color: root.errorColor
            font.bold: true
            font.pointSize: 12
        }
        Basic.Label {
            width: parent.width
            text: root.detail
            color: root.palette.windowText
            wrapMode: Text.WordWrap
            font.pointSize: 12
        }
    }
}
