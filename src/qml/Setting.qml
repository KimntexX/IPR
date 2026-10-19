import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root

    required property var editor
    property var preferences: null
    property var resetAction: null
    readonly property bool exportDialogVisible: exportDialog.visible || exportReview.visible
    property bool preferencesReady: false
    property bool restoringPreferences: false
    property int activeCurve: 0
    property var curvePoints: []
    property var toneCurveModuleInstance: null
    property var histogramModuleInstance: null
    property real referenceTemperatureKelvin: 6500
    // Session-local destination, independent of the source photo directory.
    // Do not use a newly prepared currentFile to forget the accepted folder.
    property url lastExportTarget: ""
    property string reviewedImageKey: ""
    readonly property string currentImageKey: (editor.imageDetails.storage || "")
                                             + "/" + (editor.imageDetails.name || "")
    readonly property color textColor: preferences ? preferences.theme.text : "#edf2f8"
    readonly property color secondaryTextColor: preferences ? preferences.theme.secondaryText : "#d7dfeb"

    implicitWidth: 320
    implicitHeight: 720

    function curveName(channel) {
        return [qsTr("Luminance"), qsTr("Rouge"), qsTr("Vert"), qsTr("Bleu")][channel]
    }

    function curveColor(channel) {
        return ["#edf2f8", "#f07178", "#98c379", "#61afef"][channel]
    }

    function evaluateCurve(points, sample) {
        if (points.length === 0)
            return sample

        const weights = []
        for (let index = 0; index < points.length; ++index) {
            let weight = 1
            for (let other = 0; other < points.length; ++other) {
                if (index !== other)
                    weight /= points[index].x - points[other].x
            }
            weights.push(weight)
        }

        if (sample <= 1) {
            let numerator = 0
            let denominator = 0
            for (let index = 0; index < points.length; ++index) {
                const distance = sample - points[index].x
                if (Math.abs(distance) < 0.000000001)
                    return points[index].y
                const term = weights[index] / distance
                numerator += term * points[index].y
                denominator += term
            }
            return numerator / denominator
        }

        const endpoint = points.length - 1
        let derivative = 0
        for (let index = 0; index < endpoint; ++index) {
            derivative += weights[index] / weights[endpoint] *
                    (points[index].y - points[endpoint].y) /
                    (points[endpoint].x - points[index].x)
        }
        return points[endpoint].y +
                (sample - points[endpoint].x) * derivative
    }

    function reloadCurve() {
        curvePoints = editor.toneCurve(activeCurve)
        if (toneCurveModuleInstance)
            toneCurveModuleInstance.requestPaint()
    }

    function commitCurve() {
        editor.setToneCurve(activeCurve, curvePoints)
    }

    function prepareExportDialog() {
        // Reset a filter remembered by the native dialog to the panel's format.
        exportDialog.selectedNameFilter.index = formatBox.currentIndex
        const defaultFile = editor.defaultExportFile(formatBox.currentText, lastExportTarget)
        if (defaultFile.toString().length > 0)
            exportDialog.currentFile = defaultFile
    }

    function openExportDialog() {
        prepareExportDialog()
        exportDialog.open()
    }

    function restorePreferences() {
        if (!preferences)
            return
        restoringPreferences = true
        activeCurve = preferences.value("selection/toneCurveChannel")
        colorMixerModule.selectBand(preferences.value("selection/mixerBand"))
        colorGradingModule.selectZone(preferences.value("selection/gradingZone"))
        formatBox.currentIndex = ["JPEG", "PNG", "TIFF", "DNG"].indexOf(
            preferences.value("export/format"))
        qualitySlider.value = preferences.value("export/quality")
        restoringPreferences = false
    }

    function rememberPreference(key, value) {
        if (preferencesReady && !restoringPreferences && preferences)
            preferences.setValue(key, value)
    }

    Component.onCompleted: {
        restorePreferences()
        preferencesReady = true
        reloadCurve()
    }
    onPreferencesChanged: if (preferencesReady) restorePreferences()
    onActiveCurveChanged: rememberPreference("selection/toneCurveChannel", activeCurve)

    Connections {
        target: colorMixerModule
        function onBandChanged() {
            root.rememberPreference("selection/mixerBand", colorMixerModule.band)
        }
    }
    Connections {
        target: colorGradingModule
        function onZoneChanged() {
            root.rememberPreference("selection/gradingZone", colorGradingModule.zone)
        }
    }

    Connections {
        target: editor

        function onToneCurveChanged(channel) {
            if (channel === root.activeCurve)
                root.reloadCurve()
        }

        function onToneCurvesReset() {
            root.reloadCurve()
        }

        function onHistogramChanged() {
            if (root.histogramModuleInstance)
                root.histogramModuleInstance.requestPaint()
        }

        function onAdjustmentsReset(temperatureKelvin, tint) {
            grayscaleCheck.checked = editor.adjustmentValue("grayscale") > 0
            brightnessSlider.value = editor.adjustmentValue("brightness")
            contrastSlider.value = editor.adjustmentValue("contrast")
            shadowSlider.value = editor.adjustmentValue("shadows")
            blacksSlider.value = editor.adjustmentValue("blacks")
            highlightsSlider.value = editor.adjustmentValue("highlights")
            whitesSlider.value = editor.adjustmentValue("whites")
            root.referenceTemperatureKelvin = temperatureKelvin
            temperatureSlider.value = editor.adjustmentValue("temperature")
            tintSlider.value = editor.adjustmentValue("tint")
            vibranceSlider.value = editor.adjustmentValue("vibrance")
            saturationSlider.value = editor.adjustmentValue("saturation")
            textureSlider.value = editor.adjustmentValue("texture")
            claritySlider.value = editor.adjustmentValue("clarity")
            dehazeSlider.value = editor.adjustmentValue("dehaze")
            grainSlider.value = editor.adjustmentValue("grain")
            denoiseLuminanceSlider.value = editor.adjustmentValue("denoiseLuminance")
            denoiseChrominanceSlider.value = editor.adjustmentValue("denoiseChrominance")
            sharpeningAmountSlider.value = editor.adjustmentValue("sharpeningAmount")
            sharpeningRadiusSlider.value = editor.adjustmentValue("sharpeningRadius")
            sharpeningMaskingSlider.value = editor.adjustmentValue("sharpeningMasking")
            blurSlider.value = editor.adjustmentValue("blur")
            colorMixerModule.reload()
            colorGradingModule.reload()
            root.reloadCurve()
        }
    }

    ColumnLayout {
        id: fixedSettingsHeader
        objectName: "fixedSettingsHeader"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.rightMargin: 8
        spacing: 8

        Label {
            Layout.fillWidth: true
            text: qsTr("Réglages")
            font.bold: true
            font.pixelSize: 18
            color: root.textColor
            padding: 0
            background: Item { }
        }
        HistogramModule {
            id: histogramModule
            objectName: "histogramModule"
            visible: !root.preferences || root.preferences.layout.showHistogram
            Layout.fillWidth: true
            histogram: root.editor.histogram
            exposureEV: brightnessSlider.value
            imageDetails: root.editor.imageDetails
            textColor: root.secondaryTextColor
            Component.onCompleted: {
                root.histogramModuleInstance = histogramModule
                histogramModule.requestPaint()
            }
            Component.onDestruction: {
                if (root.histogramModuleInstance === histogramModule)
                    root.histogramModuleInstance = null
            }
        }
        PreferenceButton {
            objectName: "resetAllAdjustmentsButton"
            text: qsTr("Réinitialiser les réglages")
            Layout.fillWidth: true
            enabled: root.resetAction ? root.resetAction.enabled : editor.hasImage
            onClicked: root.resetAction ? root.resetAction.trigger() : editor.resetAdjustments()
        }
    }

    ScrollView {
        id: settingsScrollView
        objectName: "settingsScrollView"

        anchors.top: fixedSettingsHeader.bottom
        anchors.topMargin: 8
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        rightPadding: 8
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical: ScrollBar {
            id: settingsScrollBar

            policy: ScrollBar.AsNeeded
            orientation: Qt.Vertical
            width: 6
            padding: 0
            hoverEnabled: true
            anchors.top: settingsScrollView.top
            anchors.right: settingsScrollView.right
            anchors.bottom: settingsScrollView.bottom
            z: 2

            contentItem: Rectangle {
                implicitWidth: 6
                radius: width / 2
                color: "#8c9caf"
                opacity: settingsScrollBar.pressed || settingsScrollBar.hovered
                         ? 0.58 : 0.24

                Behavior on opacity {
                    NumberAnimation { duration: 140 }
                }
            }

            background: Item { }
        }

        ColumnLayout {
            width: settingsScrollView.availableWidth
            implicitWidth: 0
            spacing: 8

            SettingsSection {
                objectName: "rawDevelopmentSection"
                stateKey: "rawDevelopment"
                preferences: root.preferences
                textColor: root.textColor
                title: qsTr("Développement RAW / Profil")
                expanded: true

                RawDevelopmentModule {
                    editor: root.editor
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                }
            }

            SettingsSection {
                objectName: "lightSection"
                stateKey: "light"
                preferences: root.preferences
                textColor: root.textColor
                title: qsTr("Lumière")
                expanded: true

                PreferenceCheckBox {
                    id: grayscaleCheck
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: qsTr("Niveaux de gris")
                    enabled: editor.hasImage
                    onToggled: editor.setGrayscale(checked)
                }

                AdjustmentControl {
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    id: brightnessSlider
                    label: qsTr("Exposition (EV)")
                    valueText: Number(value).toFixed(2)
                    minimumValue: -5
                    maximumValue: 5
                    increment: 0.01
                    adjustmentEnabled: editor.hasImage
                    onValueEdited: function(value) {
                        if (editor)
                            editor.setBrightness(value)
                    }
                }

                AdjustmentControl {
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    id: contrastSlider
                    label: qsTr("Contraste")
                    valueText: Math.round(value)
                    minimumValue: -100
                    maximumValue: 100
                    adjustmentEnabled: editor.hasImage
                    onValueEdited: function(value) {
                        if (editor)
                            editor.setContrast(Math.round(value))
                    }
                }

                AdjustmentControl {
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    id: highlightsSlider
                    label: qsTr("Hautes lumières")
                    valueText: Math.round(value)
                    minimumValue: -100
                    maximumValue: 100
                    adjustmentEnabled: editor.hasImage
                    onValueEdited: function(value) {
                        if (editor)
                            editor.setHighlights(Math.round(value))
                    }
                }

                AdjustmentControl {
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    id: whitesSlider
                    label: qsTr("Blancs")
                    valueText: Math.round(value)
                    minimumValue: -100
                    maximumValue: 100
                    adjustmentEnabled: editor.hasImage
                    onValueEdited: function(value) {
                        if (editor)
                            editor.setWhites(Math.round(value))
                    }
                }

                AdjustmentControl {
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    id: shadowSlider
                    label: qsTr("Ombres")
                    valueText: Math.round(value)
                    minimumValue: -100
                    maximumValue: 100
                    adjustmentEnabled: editor.hasImage
                    onValueEdited: function(value) {
                        if (editor)
                            editor.setShadows(Math.round(value))
                    }
                }

                AdjustmentControl {
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    id: blacksSlider
                    label: qsTr("Noirs")
                    valueText: Math.round(value)
                    minimumValue: -100
                    maximumValue: 100
                    adjustmentEnabled: editor.hasImage
                    onValueEdited: function(value) {
                        if (editor)
                            editor.setBlacks(Math.round(value))
                    }
                }
            }

            SettingsSection {
                objectName: "curveSection"
                stateKey: "curves"
                preferences: root.preferences
                textColor: root.textColor
                title: qsTr("Courbe de tonalité")

                ToneCurveModule {
                    id: toneCurveModule
                    objectName: "toneCurveModule"
                    histogram: root.editor.histogram
                    showTitle: false
                    Layout.fillWidth: true

                    editor: root.editor
                    activeCurve: root.activeCurve
                    curvePoints: root.curvePoints
                    curveName: root.curveName
                    curveColor: root.curveColor
                    evaluateCurve: root.evaluateCurve
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor

                    onCurvePointsEdited: function(points) {
                        root.curvePoints = points
                    }
                    onCommitRequested: root.commitCurve()
                    onActiveCurveRequested: function(channel) {
                        root.activeCurve = channel
                        root.reloadCurve()
                    }

                    Component.onCompleted: {
                        root.toneCurveModuleInstance = toneCurveModule
                        root.reloadCurve()
                    }
                    Component.onDestruction: {
                        if (root.toneCurveModuleInstance === toneCurveModule)
                            root.toneCurveModuleInstance = null
                    }
                }
            }

            SettingsSection {
                objectName: "colorSection"
                stateKey: "color"
                preferences: root.preferences
                textColor: root.textColor
                title: qsTr("Couleur")

                AdjustmentControl {
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    id: temperatureSlider
                    trackColors: ["#477dda", "#c4c6c4", "#e8ae4e"]
                    label: qsTr("Température (K)")
                    valueText: Math.round(value)
                    minimumValue: 2000
                    maximumValue: 50000
                    value: 6500
                    resetValue: root.referenceTemperatureKelvin
                    adjustmentEnabled: editor.hasImage
                    onValueEdited: function(value) {
                        if (editor)
                            editor.setTemperature(Math.round(value))
                    }
                }

                AdjustmentControl {
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    id: tintSlider
                    trackColors: ["#63b777", "#c4c6c4", "#c46eb5"]
                    label: qsTr("Nuance vert ↔ magenta")
                    valueText: Math.round(value)
                    minimumValue: -150
                    maximumValue: 150
                    adjustmentEnabled: editor.hasImage
                    onValueEdited: function(value) {
                        if (editor)
                            editor.setTint(Math.round(value))
                    }
                }

                AdjustmentControl {
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    id: vibranceSlider
                    trackColors: ["#78828b", "#719e9e", "#b4b93c", "#dc6845"]
                    label: qsTr("Vibrance")
                    valueText: Math.round(value)
                    minimumValue: -100
                    maximumValue: 100
                    adjustmentEnabled: editor.hasImage
                    onValueEdited: function(value) {
                        if (editor)
                            editor.setVibrance(Math.round(value))
                    }
                }

                AdjustmentControl {
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    id: saturationSlider
                    trackColors: ["#78828b", "#78a5a4", "#5cae6c", "#d2c247", "#db6559"]
                    label: qsTr("Saturation")
                    valueText: Math.round(value)
                    minimumValue: -100
                    maximumValue: 100
                    adjustmentEnabled: editor.hasImage
                    onValueEdited: function(value) {
                        if (editor)
                            editor.setSaturation(Math.round(value))
                    }
                }
            }

            SettingsSection {
                objectName: "mixerSection"
                stateKey: "mixer"
                preferences: root.preferences
                textColor: root.textColor
                title: qsTr("Mélangeur de couleurs")

                ColorMixerModule {
                    id: colorMixerModule
                    showTitle: false
                    Layout.fillWidth: true
                    editor: root.editor
                }
            }

            SettingsSection {
                objectName: "gradingSection"
                stateKey: "grading"
                preferences: root.preferences
                textColor: root.textColor
                title: qsTr("Color grading")

                ColorGradingModule {
                    id: colorGradingModule
                    showTitle: false
                    Layout.fillWidth: true
                    editor: root.editor
                }
            }

            SettingsSection {
                objectName: "effectsSection"
                stateKey: "effects"
                preferences: root.preferences
                textColor: root.textColor
                title: qsTr("Effets")

                AdjustmentControl {
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    id: textureSlider
                    label: qsTr("Texture")
                    valueText: Math.round(value)
                    minimumValue: -100
                    maximumValue: 100
                    adjustmentEnabled: editor.hasImage
                    onValueEdited: function(value) {
                        if (editor)
                            editor.setTexture(Math.round(value))
                    }
                }

                AdjustmentControl {
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    id: claritySlider
                    label: qsTr("Clarté")
                    valueText: Math.round(value)
                    minimumValue: -100
                    maximumValue: 100
                    adjustmentEnabled: editor.hasImage
                    onValueEdited: function(value) {
                        if (editor)
                            editor.setClarity(Math.round(value))
                    }
                }

                AdjustmentControl {
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    id: dehazeSlider
                    label: qsTr("Correction du voile")
                    valueText: Math.round(value)
                    minimumValue: -100
                    maximumValue: 100
                    adjustmentEnabled: editor.hasImage
                    onValueEdited: function(value) {
                        if (editor)
                            editor.setDehaze(Math.round(value))
                    }
                }

                AdjustmentControl {
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    id: grainSlider
                    label: qsTr("Grain")
                    valueText: Math.round(value)
                    minimumValue: 0
                    maximumValue: 100
                    adjustmentEnabled: editor.hasImage
                    onValueEdited: function(value) {
                        if (editor)
                            editor.setGrain(Math.round(value))
                    }
                }

                AdjustmentControl {
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    id: blurSlider
                    label: qsTr("Flou gaussien")
                    valueText: Math.round(value)
                    maximumValue: 20
                    adjustmentEnabled: editor.hasImage
                    onValueEdited: function(value) {
                        if (editor)
                            editor.setBlur(Math.round(value))
                    }
                }
            }

            SettingsSection {
                objectName: "denoiseSection"
                stateKey: "denoise"
                preferences: root.preferences
                textColor: root.textColor
                title: qsTr("Réduction du bruit")

                AdjustmentControl {
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    id: denoiseLuminanceSlider
                    label: qsTr("Bruit de luminance")
                    valueText: Math.round(value)
                    minimumValue: 0
                    maximumValue: 100
                    adjustmentEnabled: editor.hasImage
                    onValueEdited: function(value) {
                        if (editor)
                            editor.setDenoiseLuminance(Math.round(value))
                    }
                }

                AdjustmentControl {
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    id: denoiseChrominanceSlider
                    label: qsTr("Bruit de couleur")
                    valueText: Math.round(value)
                    minimumValue: 0
                    maximumValue: 100
                    adjustmentEnabled: editor.hasImage
                    onValueEdited: function(value) {
                        if (editor)
                            editor.setDenoiseChrominance(Math.round(value))
                    }
                }
            }

            SettingsSection {
                objectName: "sharpeningSection"
                stateKey: "sharpening"
                preferences: root.preferences
                textColor: root.textColor
                title: qsTr("Netteté")

                AdjustmentControl {
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    id: sharpeningAmountSlider
                    label: qsTr("Gain de netteté")
                    valueText: Math.round(value)
                    minimumValue: 0
                    maximumValue: 200
                    adjustmentEnabled: editor.hasImage
                    onValueEdited: function(value) {
                        if (editor)
                            editor.setSharpeningAmount(Math.round(value))
                    }
                }

                AdjustmentControl {
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    id: sharpeningRadiusSlider
                    value: 1.0
                    label: qsTr("Rayon de netteté")
                    valueText: Number(value).toFixed(1)
                    minimumValue: 0.3
                    maximumValue: 3.0
                    increment: 0.1
                    resetValue: 1.0
                    adjustmentEnabled: editor.hasImage
                    onValueEdited: function(value) {
                        if (editor)
                            editor.setSharpeningRadius(value)
                    }
                }

                AdjustmentControl {
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    id: sharpeningMaskingSlider
                    label: qsTr("Masquage de netteté")
                    valueText: Math.round(value)
                    minimumValue: 0
                    maximumValue: 100
                    resetValue: 0
                    adjustmentEnabled: editor.hasImage
                    onValueEdited: function(value) {
                        if (editor)
                            editor.setSharpeningMasking(Math.round(value))
                    }
                }
            }

            SettingsSection {
                objectName: "opticsSection"
                stateKey: "optics"
                preferences: root.preferences
                textColor: root.textColor
                title: qsTr("Corrections optiques")

                OpticsModule {
                    editor: root.editor
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                }
            }

            SettingsSection {
                objectName: "exportSection"
                stateKey: "export"
                preferences: root.preferences
                textColor: root.textColor
                title: qsTr("Export")

                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: qsTr("Format")
                        color: root.secondaryTextColor
                    }

                    PreferenceComboBox {
                        id: formatBox
                        objectName: "exportFormat"

                        Layout.preferredWidth: 92
                        Layout.minimumWidth: 92
                        Layout.maximumWidth: 92
                        implicitHeight: 26
                        model: ["JPEG", "PNG", "TIFF", "DNG"]
                        onCurrentTextChanged: root.rememberPreference("export/format", currentText)
                        enabled: editor.hasImage

                contentItem: Text {
                            leftPadding: 7
                            rightPadding: formatBox.indicator.width + 6
                            text: formatBox.displayText
                            color: root.textColor
                            opacity: formatBox.enabled ? 1 : 0.45
                            font.pixelSize: 12
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        indicator: Text {
                            text: "▾"
                            color: formatBox.palette.text
                            x: formatBox.width - width - 7
                            y: formatBox.topPadding +
                               (formatBox.availableHeight - height) / 2

                        }

                        background: Rectangle {
                            radius: 3
                            color: formatBox.pressed ? formatBox.palette.mid : formatBox.palette.base
                            border.width: 1
                            border.color: formatBox.activeFocus ? formatBox.palette.text : formatBox.palette.mid
                        }
                    }
                }

                AdjustmentControl {
                    textColor: root.textColor
                    secondaryTextColor: root.secondaryTextColor
                    id: qualitySlider
                    label: qsTr("Qualité JPEG")
                    valueText: Math.round(value)
                    minimumValue: 1
                    maximumValue: 100
                    value: 92
                    increment: 1
                    resetValue: 92
                    visible: formatBox.currentText === "JPEG"
                    adjustmentEnabled: editor.hasImage
                    onValueEdited: function(value) {
                        root.rememberPreference("export/quality", Math.round(value))
                    }
                }

                PreferenceButton {
                    id: exportButton
                    objectName: "exportDestinationButton"

                    text: qsTr("Choisir la destination…")
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    enabled: editor.hasImage && !editor.busy && !editor.embeddedPreview
                    onClicked: root.openExportDialog()

                    contentItem: Text {
                        text: exportButton.text
                        color: root.textColor
                        opacity: exportButton.enabled ? 1 : 0.45
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 3
                        color: exportButton.down || exportButton.hovered ? exportButton.palette.mid : exportButton.palette.button
                        border.width: 1
                        border.color: exportButton.palette.mid
                    }
                }
            }

        }
    }

    FileDialog {
        id: exportDialog
        objectName: "exportDialog"
        title: qsTr("Exporter l'image")
        fileMode: FileDialog.SaveFile
        // Existing names are numbered by the exporter, never intentionally
        // replaced; a native overwrite confirmation would be misleading.
        options: FileDialog.DontConfirmOverwrite
        nameFilters: [qsTr("Images JPEG (*.jpg *.jpeg)"),
                      qsTr("Images PNG (*.png)"),
                      qsTr("Images TIFF (*.tif *.tiff)"),
                      qsTr("DNG linéaire rendu IPR (*.dng)")]
        defaultSuffix: ["jpg", "png", "tiff", "dng"][Math.max(0, selectedNameFilter.index)]
        onAccepted: {
            // If the user changes Type in the dialog, write that format too.
            formatBox.currentIndex = selectedNameFilter.index
            const target = selectedFile.toString().length > 0
                           ? selectedFile : currentFile
            root.lastExportTarget = target
            root.reviewedImageKey = root.currentImageKey
            exportReview.target = target
            exportReview.format = formatBox.currentText
            exportReview.quality = Math.round(qualitySlider.value)
            exportReview.dimensions = editor.imageDetails.dimensions || ""
            exportReview.open()
        }
    }

    ExportReviewDialog {
        id: exportReview
        objectName: "exportReviewDialog"
        palette.window: root.preferences ? root.preferences.theme.panel : "#2a3039"
        palette.text: root.textColor
        palette.windowText: root.secondaryTextColor
        palette.buttonText: root.textColor
        palette.button: Qt.darker(palette.window, 1.1)
        palette.mid: Qt.tint(palette.window, Qt.rgba(palette.text.r, palette.text.g, palette.text.b, .25))
        exportEnabled: editor.hasImage && !editor.busy && !editor.embeddedPreview
                       && root.currentImageKey === root.reviewedImageKey
        unavailableReason: root.currentImageKey !== root.reviewedImageKey
                           ? qsTr("L’image a changé. Fermez ce récapitulatif et recommencez l’export.")
                           : qsTr("Attendez la fin du traitement avant d’exporter.")
        onExportConfirmed: editor.exportImage(target, format, quality)
    }
}
