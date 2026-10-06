import QtQuick
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// Horizontal usage bar: a rounded track with a coloured fill (0..1)
Rectangle {
    id: bar

    property real value: 0
    property color fillColor: LingmoUI.Theme.highlightColor

    implicitHeight: LingmoUI.Units.smallSpacing
    radius: height / 2
    // Translucent so it shows on cards and on their hover colour alike
    color: Qt.rgba(LingmoUI.Theme.textColor.r, LingmoUI.Theme.textColor.g, LingmoUI.Theme.textColor.b,
                   LingmoUI.Theme.darkMode ? 0.12 : 0.08)

    Rectangle {
        height: parent.height
        width: Math.max(parent.value > 0 ? parent.height : 0, parent.width * Math.min(1, Math.max(0, bar.value)))
        radius: parent.radius
        color: bar.fillColor

        Behavior on width { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
        Behavior on color { ColorAnimation { duration: 200 } }
    }
}
