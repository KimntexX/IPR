import QtQuick

QtObject {
    readonly property var choices: [
        { label: qsTr("Anthracite"), value: "#15181d", end: "#15181d" },
        { label: qsTr("Gris moyen"), value: "#777777", end: "#777777" },
        { label: qsTr("Noir"), value: "#000000", end: "#000000" },
        { label: qsTr("Blanc"), value: "#ffffff", end: "#ffffff" },
        { label: qsTr("Bleu marine — dégradé"), value: "#1b2d45", end: "#111c2b" },
        { label: qsTr("Rouge bordeaux — dégradé"), value: "#42252e", end: "#291a20" }
    ]

    function endColor(color) {
        const key = String(color).toLowerCase()
        for (const choice of choices)
            if (choice.value === key) return choice.end
        return color
    }
}
