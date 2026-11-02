import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic

// The interface's menu, for the menu bar and every context menu; its entries are
// ActionMenuItem. A popup does not follow its parent's colours by itself: without
// this binding, a menu falls back on Qt's light palette under the light text of
// the theme. A derived menu only adds its entries and, if it needs one, its width.
Basic.Menu {
    id: root
    popupType: Popup.Item

    Binding {
        target: root
        property: "palette"
        value: root.parent ? root.parent.palette : null
        when: root.parent !== null
    }

    background: Rectangle {
        implicitWidth: 200
        implicitHeight: 32
        radius: 4
        color: root.palette.window
        border.color: root.palette.mid
    }
}
