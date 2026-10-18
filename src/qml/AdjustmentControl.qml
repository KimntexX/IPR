import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts

ColumnLayout {
    id: control

    property string label: ""
    property string valueText: ""
    property real minimumValue: 0
    property real maximumValue: 100
    property real increment: 1
    property real resetValue: 0
    property color textColor: controlSlider.palette.text
    property color secondaryTextColor: controlSlider.palette.windowText
    property alias value: controlSlider.value
    property bool adjustmentEnabled: false
    readonly property bool modified: Math.abs(value - resetValue) > Math.max(0.000001, increment / 10)

    function resetToDefault() {
        if (adjustmentEnabled) value = resetValue
    }
    // UI hints only: these colours do not describe an output colour transform.
    property var trackColors: []
    readonly property bool hasColorTrack: trackColors.length >= 2
    // Only bipolar controls have a neutral centre. Positive-only quantities
    // (quality, noise, radius...) keep their minimum as the visual origin.
    readonly property bool bipolar: minimumValue < 0 && maximumValue > 0
    readonly property real originPosition: bipolar
        ? (0 - minimumValue) / (maximumValue - minimumValue) : 0
    signal valueEdited(real value)

    Layout.fillWidth: true
    spacing: 2

    RowLayout {
        Layout.fillWidth: true

        Label {
            id: parameterLabel
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: control.label
            elide: Text.ElideRight
            color: control.secondaryTextColor
            font.bold: control.modified
            TapHandler { onDoubleTapped: control.resetToDefault() }
            HoverHandler { id: labelHover }
            ToolTip.visible: labelHover.hovered
            ToolTip.text: qsTr("Double-clic pour réinitialiser à %1").arg(control.resetValue)
        }

        Basic.TextField {
            id: valueEditor
            objectName: "adjustmentValueEditor"

            Layout.preferredWidth: 50
            Layout.minimumWidth: 50
            Layout.maximumWidth: 50
            implicitHeight: Math.max(22, contentHeight + topPadding + bottomPadding)
            topPadding: 2
            bottomPadding: 2
            leftPadding: 3
            rightPadding: 3
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignVCenter
            selectByMouse: true
            enabled: control.adjustmentEnabled
            color: control.textColor
            font.pixelSize: 12
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            validator: DoubleValidator {
                bottom: control.minimumValue
                top: control.maximumValue
                decimals: 2
                notation: DoubleValidator.StandardNotation
            }
            text: control.valueText

            function parsedValue() {
                // The displayed locale can use a comma while users often type
                // a decimal point. These compact fields never accept grouping,
                // so accepting both forms keeps the text field and slider in
                // the same numeric domain.
                const normalized = text.trim().replace(/\s/g, "").replace(",", ".")
                return normalized.length > 0 ? Number(normalized) : NaN
            }

            function applyValue(revertInvalid) {
                const parsed = parsedValue()
                if (Number.isFinite(parsed)) {
                    controlSlider.value = Math.max(control.minimumValue,
                                                   Math.min(control.maximumValue,
                                                            parsed))
                    if (!activeFocus)
                        text = control.valueText
                } else if (revertInvalid) {
                    text = control.valueText
                }
            }

            // Update the Slider as soon as the entered number is valid. This
            // also emits valueEdited, so the C++ setting follows the field
            // without requiring the user to press Enter or change focus.
            onTextEdited: {
                if (acceptableInput)
                    applyValue(false)
            }
            onAccepted: applyValue(true)
            onEditingFinished: applyValue(true)
            onActiveFocusChanged: {
                if (!activeFocus)
                    applyValue(true)
            }

            Connections {
                target: control
                function onValueTextChanged() {
                    if (!valueEditor.activeFocus)
                        valueEditor.text = control.valueText
                }
            }

            background: Rectangle {
                radius: 3
                color: valueEditor.activeFocus ? valueEditor.palette.base : "transparent"
                border.width: 1
                border.color: valueEditor.activeFocus ? valueEditor.palette.text : valueEditor.palette.mid
            }
        }

        PreferenceButton {
            id: resetButton

            Layout.preferredWidth: 21
            Layout.minimumWidth: 21
            Layout.maximumWidth: 21
            Layout.preferredHeight: 21
            Layout.minimumHeight: 21
            Layout.maximumHeight: 21
            text: "↺"
            font.pixelSize: 14
            padding: 0
            hoverEnabled: true
            enabled: control.adjustmentEnabled
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Réinitialiser")
            onClicked: control.resetToDefault()

            background: Rectangle {
                radius: 3
                color: resetButton.hovered ? resetButton.palette.mid : "transparent"
            }
        }
    }

    Basic.Slider {
        id: controlSlider
        objectName: "adjustmentSlider"
        focusPolicy: Qt.StrongFocus
        Accessible.name: control.label
        Accessible.description: qsTr("Flèches : ajuster. Double-clic : réinitialiser.")
        TapHandler { onDoubleTapped: control.resetToDefault() }

        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredHeight: 14
        implicitHeight: 14
        leftPadding: 5
        rightPadding: 5
        from: control.minimumValue
        to: control.maximumValue
        stepSize: control.increment
        enabled: control.adjustmentEnabled
        onValueChanged: control.valueEdited(value)
        onMirroredChanged: colorTrack.requestPaint()

        background: Rectangle {
            objectName: "adjustmentTrack"
            x: controlSlider.leftPadding + controlSlider.handle.width / 2
            y: controlSlider.topPadding + controlSlider.availableHeight / 2 - height / 2
            width: Math.max(0, controlSlider.availableWidth - controlSlider.handle.width)
            height: 3
            radius: height / 2
            color: controlSlider.palette.mid

            Canvas {
                id: colorTrack
                objectName: "adjustmentColorTrack"
                anchors.fill: parent
                visible: control.hasColorTrack
                opacity: controlSlider.enabled ? 0.85 : 0.3
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                Connections {
                    target: control
                    function onTrackColorsChanged() { colorTrack.requestPaint() }
                }
                onPaint: {
                    const context = getContext("2d")
                    context.clearRect(0, 0, width, height)
                    if (!control.hasColorTrack || width <= 0 || height <= 0)
                        return
                    const gradient = context.createLinearGradient(
                        controlSlider.mirrored ? width : 0, 0,
                        controlSlider.mirrored ? 0 : width, 0)
                    for (let index = 0; index < control.trackColors.length; ++index)
                        gradient.addColorStop(index / (control.trackColors.length - 1),
                                              String(control.trackColors[index]))
                    context.fillStyle = gradient
                    context.fillRect(0, 0, width, height)
                }
            }

            Rectangle {
                objectName: "adjustmentFill"
                readonly property real origin: controlSlider.mirrored
                    ? 1 - control.originPosition : control.originPosition
                x: Math.min(origin, controlSlider.visualPosition) * parent.width
                width: Math.abs(controlSlider.visualPosition - origin) * parent.width
                height: parent.height
                radius: height / 2
                color: control.textColor
                opacity: control.hasColorTrack ? 0.25 : (controlSlider.enabled ? 0.65 : 0.3)
            }
            Rectangle {
                visible: control.bipolar
                x: (controlSlider.mirrored ? 1 - control.originPosition
                                          : control.originPosition) * parent.width - 0.5
                y: -1
                width: 1
                height: parent.height + 2
                color: control.textColor
                opacity: 0.55
            }
        }

        handle: Rectangle {
            x: controlSlider.leftPadding + controlSlider.visualPosition *
               (controlSlider.availableWidth - width)
            y: controlSlider.topPadding + controlSlider.availableHeight / 2 - height / 2
            width: 10
            height: 10
            radius: width / 2
            color: control.textColor
            opacity: controlSlider.enabled ? 1 : 0.4
            border.width: 1
            border.color: controlSlider.activeFocus ? control.textColor : "#566474"
        }
    }
}
