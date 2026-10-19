import QtQuick

Rectangle {
    id: root
    property color baseColor: "#15181d"
    readonly property color endColor: options.endColor(baseColor)
    ImageBackgroundOptions { id: options }
    color: baseColor
    gradient: Gradient {
        GradientStop { position: 0; color: root.baseColor }
        GradientStop { position: 1; color: root.endColor }
    }
}
