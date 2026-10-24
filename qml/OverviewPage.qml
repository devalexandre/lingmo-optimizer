import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

Item {
    id: page

    signal openPage(string name)
    signal cleanRequested()

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: LingmoUI.Units.largeSpacing * 2.5
        anchors.rightMargin: LingmoUI.Units.largeSpacing * 2.5
        anchors.bottomMargin: LingmoUI.Units.largeSpacing * 2.5
        spacing: LingmoUI.Units.largeSpacing

        Label {
            text: qsTr("Visão Geral do Sistema")
            font.pointSize: LingmoUI.Units.fontTitle
            font.bold: true
            color: LingmoUI.Theme.textColor
            Layout.alignment: Qt.AlignHCenter
        }

        Label {
            text: qsTr("Clique em um item para ver os detalhes, ou libere espaço e memória com um clique.")
            color: LingmoUI.Theme.secondaryTextColor
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: LingmoUI.Units.largeSpacing * 1.5

            Item { Layout.fillWidth: true }
            Donut {
                objectName: "cpuCard"
                title: qsTr("CPU")
                subtitle: systemInfo.cpuSubtitle
                tintColor: LingmoUI.Theme.greenColor
                value: systemInfo.cpuUsage
                onClicked: page.openPage("cpu")
            }
            Donut {
                objectName: "memoryCard"
                title: qsTr("Memória")
                subtitle: systemInfo.memSubtitle
                tintColor: LingmoUI.Theme.blueColor
                value: systemInfo.ramUsage
                onClicked: page.openPage("memory")
            }
            Donut {
                objectName: "storageCard"
                title: qsTr("Armazenamento")
                subtitle: systemInfo.diskSubtitle
                tintColor: LingmoUI.Theme.orangeColor
                value: systemInfo.diskUsage
                onClicked: page.openPage("storage")
            }
            Item { Layout.fillWidth: true }
        }

        LingmoUI.ActionButton {
            id: cleanBtn
            objectName: "cleanButton"
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 220
            primary: true
            text: cleaner.running ? qsTr("Otimizando...") : qsTr("Otimizar e Limpar")
            enabled: !cleaner.running
            busy: cleaner.running
            font.bold: true
            onClicked: page.cleanRequested()
        }
    }
}
