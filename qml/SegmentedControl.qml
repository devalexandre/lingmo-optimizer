import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// A row of mutually exclusive tabs, Lingmo Settings style
Rectangle {
    id: control

    property var model: []
    property int currentIndex: 0

    implicitHeight: LingmoUI.Units.fontMetrics.height + LingmoUI.Units.largeSpacing * 1.2
    implicitWidth: row.implicitWidth + 6
    radius: LingmoUI.Theme.smallRadius + 2
    color: LingmoUI.Theme.alternateBackgroundColor

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: 3
        spacing: 3

        Repeater {
            model: control.model

            delegate: Rectangle {
                id: segment
                required property int index
                required property var modelData
                readonly property bool selected: control.currentIndex === index

                Layout.fillWidth: true
                Layout.fillHeight: true
                implicitWidth: label.implicitWidth + LingmoUI.Units.largeSpacing * 2
                radius: LingmoUI.Theme.smallRadius
                color: selected ? LingmoUI.Theme.highlightColor
                     : mouse.containsMouse ? Qt.rgba(LingmoUI.Theme.textColor.r, LingmoUI.Theme.textColor.g,
                                                     LingmoUI.Theme.textColor.b, 0.08)
                     : "transparent"

                Behavior on color { ColorAnimation { duration: 120 } }

                Label {
                    id: label
                    anchors.centerIn: parent
                    text: segment.modelData
                    color: segment.selected ? LingmoUI.Theme.highlightedTextColor : LingmoUI.Theme.textColor
                }

                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: control.currentIndex = segment.index
                }
            }
        }
    }
}
