import QtQuick
import QtQuick.Controls

AbstractButton {
    id: channelButton

    required property int channel
    required property int activeCurve
    required property var curveName
    required property var curveColor
    readonly property bool channelSelected: activeCurve === channel
    signal channelRequested(int channel)

    implicitWidth: 26
    implicitHeight: 26
    hoverEnabled: true
    ToolTip.visible: hovered
    ToolTip.text: curveName(channel)
    onClicked: channelRequested(channel)

    background: Rectangle {
        radius: width / 2
        color: channelButton.curveColor(channelButton.channel)
        border.width: channelButton.channelSelected ? 3 : 1
        border.color: channelButton.channelSelected ? channelButton.palette.text : channelButton.palette.mid
    }
}
