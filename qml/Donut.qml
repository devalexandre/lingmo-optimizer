import QtQuick
import QtQuick.Controls
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// One usage ring (CPU, memory or storage). The whole card is a button that
// opens the matching detail page.
Card {
    id: donut

    property string title: ""
    property string subtitle: ""
    property real value: 0.0
    property color tintColor: LingmoUI.Theme.highlightColor

    signal clicked()

    implicitWidth: Math.max(ring.width, subtitleLabel.implicitWidth) + LingmoUI.Units.largeSpacing * 3
    implicitHeight: ring.height + titleLabel.implicitHeight + subtitleLabel.implicitHeight + LingmoUI.Units.largeSpacing * 4.5
    hoverEnabled: true

    onValueChanged: canvas.requestPaint()
    onTintColorChanged: canvas.requestPaint()

    Connections {
        target: LingmoUI.Theme
        function onDarkModeChanged() { canvas.requestPaint() }
    }

    Item {
        id: ring
        width: 132
        height: width
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: LingmoUI.Units.largeSpacing * 1.5

        Canvas {
            id: canvas
            anchors.fill: parent
            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();
                var centerX = width / 2;
                var centerY = height / 2;
                var lineWidth = LingmoUI.Units.largeSpacing;
                var radius = width / 2 - lineWidth;

                ctx.beginPath();
                ctx.arc(centerX, centerY, radius, 0, 2 * Math.PI, false);
                ctx.lineWidth = lineWidth;
                ctx.strokeStyle = LingmoUI.Theme.alternateBackgroundColor;
                ctx.stroke();

                if (donut.value <= 0)
                    return;
                ctx.beginPath();
                ctx.arc(centerX, centerY, radius, -Math.PI / 2, (2 * Math.PI * Math.min(1, donut.value)) - Math.PI / 2, false);
                ctx.lineWidth = lineWidth;
                ctx.strokeStyle = donut.tintColor;
                ctx.lineCap = "round";
                ctx.stroke();
            }
        }

        Label {
            text: Math.round(donut.value * 100) + "%"
            font.pointSize: LingmoUI.Theme.fontSize * 2
            font.bold: true
            color: LingmoUI.Theme.textColor
            anchors.centerIn: parent
        }
    }

    Label {
        id: titleLabel
        text: donut.title
        color: LingmoUI.Theme.textColor
        font.bold: true
        anchors.top: ring.bottom
        anchors.topMargin: LingmoUI.Units.largeSpacing
        anchors.horizontalCenter: parent.horizontalCenter
    }

    Label {
        id: subtitleLabel
        text: donut.subtitle
        color: LingmoUI.Theme.disabledTextColor
        font.pointSize: LingmoUI.Theme.fontSize * 0.9
        anchors.top: titleLabel.bottom
        anchors.topMargin: 2
        anchors.horizontalCenter: parent.horizontalCenter
    }

    // Hint that the card opens details
    LingmoUI.IconItem {
        width: LingmoUI.Units.iconSizes.small
        height: width
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: LingmoUI.Units.largeSpacing
        source: "go-next"
        opacity: donut.hovered ? 0.9 : 0.0
        Behavior on opacity { NumberAnimation { duration: 120 } }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: donut.clicked()
    }

    Accessible.role: Accessible.Button
    Accessible.name: donut.title
}
