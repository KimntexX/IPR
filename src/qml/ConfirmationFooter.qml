import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property string confirmText
    property bool confirmEnabled: true
    property string confirmObjectName: ""
    signal confirmed()
    signal cancelled()
    implicitHeight: actions.implicitHeight + 28
    RowLayout {
        id: actions
        anchors.fill: parent
        anchors.margins: 14
        spacing: 12
        Item { Layout.fillWidth: true }
        PreferenceButton {
            objectName: "cancelConfirmationButton"
            text: qsTr("Annuler")
            Layout.minimumWidth: 100
            Layout.preferredHeight: 36
            onClicked: root.cancelled()
        }
        PreferenceButton {
            objectName: root.confirmObjectName
            text: root.confirmText
            enabled: root.confirmEnabled
            Layout.minimumWidth: 100
            Layout.preferredHeight: 36
            onClicked: root.confirmed()
        }
    }
}
