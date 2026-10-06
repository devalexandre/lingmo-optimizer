import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

LingmoUI.Window {
    id: root
    width: 650
    height: 500
    minimumWidth: 650
    minimumHeight: 500
    title: qsTr("Otimizador Lingmo")
    visible: true

    // Same window look as Lingmo Settings
    background.opacity: LingmoUI.Theme.darkMode ? 0.88 : 0.97
    header.height: 40
    contentTopMargin: 0

    LingmoUI.WindowBlur {
        view: root
        geometry: Qt.rect(root.x, root.y, root.width, root.height)
        windowRadius: root.windowRadius
        enabled: true
    }

    // One usage ring: CPU, memory or storage
    component Donut: Card {
        id: donut

        property string title: ""
        property real value: 0.0
        property color tintColor: LingmoUI.Theme.highlightColor

        implicitWidth: ring.width + LingmoUI.Units.largeSpacing * 3
        implicitHeight: ring.height + titleLabel.implicitHeight + LingmoUI.Units.largeSpacing * 4
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

                    // Background ring
                    ctx.beginPath();
                    ctx.arc(centerX, centerY, radius, 0, 2 * Math.PI, false);
                    ctx.lineWidth = lineWidth;
                    ctx.strokeStyle = LingmoUI.Theme.alternateBackgroundColor;
                    ctx.stroke();

                    // Foreground ring
                    ctx.beginPath();
                    ctx.arc(centerX, centerY, radius, -Math.PI / 2, (2 * Math.PI * donut.value) - Math.PI / 2, false);
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
            color: LingmoUI.Theme.disabledTextColor
            anchors.top: ring.bottom
            anchors.topMargin: LingmoUI.Units.largeSpacing
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: root.header.height
        anchors.leftMargin: LingmoUI.Units.largeSpacing * 2.5
        anchors.rightMargin: LingmoUI.Units.largeSpacing * 2.5
        anchors.bottomMargin: LingmoUI.Units.largeSpacing * 2.5
        spacing: LingmoUI.Units.largeSpacing

        Label {
            text: qsTr("Visão Geral do Sistema")
            font.pointSize: LingmoUI.Theme.fontSize * 1.8
            font.bold: true
            color: LingmoUI.Theme.textColor
            Layout.alignment: Qt.AlignHCenter
        }

        Label {
            text: qsTr("Libere espaço e memória para manter seu sistema rápido.")
            color: LingmoUI.Theme.disabledTextColor
            Layout.alignment: Qt.AlignHCenter
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: LingmoUI.Units.largeSpacing * 1.5

            Item { Layout.fillWidth: true }
            Donut { title: "CPU"; tintColor: LingmoUI.Theme.greenColor; value: optimizer.cpuUsage }
            Donut { title: "Memória"; tintColor: LingmoUI.Theme.blueColor; value: optimizer.ramUsage }
            Donut { title: "Armazenamento"; tintColor: LingmoUI.Theme.orangeColor; value: optimizer.diskUsage }
            Item { Layout.fillWidth: true }
        }

        ActionButton {
            id: cleanBtn
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 200
            primary: true
            text: optimizer.isCleaning ? qsTr("Otimizando...") : qsTr("Otimizar e Limpar")
            enabled: !optimizer.isCleaning
            font.bold: true
            onClicked: optimizer.startCleanup()
        }
    }

    Connections {
        target: optimizer
        function onCleanupFinished(message) {
            msgDialog.text = message
            msgDialog.open()
        }
    }

    Dialog {
        id: msgDialog
        property alias text: msgLabel.text
        title: qsTr("Otimizador")
        standardButtons: Dialog.Ok
        anchors.centerIn: parent
        Label { id: msgLabel; color: LingmoUI.Theme.textColor }
    }
}
