import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

Item {
    id: page

    signal back()

    Binding {
        target: processes
        property: "active"
        value: page.StackView.status === StackView.Active
        restoreMode: Binding.RestoreValue
    }

    function loadText() {
        var l = systemInfo.loadAverage
        if (!l || l.length < 3)
            return "—"
        return l.map(function(v) { return v.toFixed(2).replace(".", ",") }).join(" · ")
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: LingmoUI.Units.largeSpacing * 2
        anchors.rightMargin: LingmoUI.Units.largeSpacing
        anchors.bottomMargin: LingmoUI.Units.largeSpacing
        spacing: LingmoUI.Units.largeSpacing

        LingmoUI.PageHeader {
            backVisible: true
            backTipText: qsTr("Voltar")
            title: qsTr("CPU")
            Layout.fillWidth: true
            Layout.rightMargin: LingmoUI.Units.largeSpacing
            onBack: page.back()
        }

        AppList {
            id: apps
            metric: "cpu"
            model: processes.cpuModel
            Layout.fillWidth: true
            Layout.fillHeight: true

            header: ColumnLayout {
                width: apps.width - LingmoUI.Units.largeSpacing
                spacing: LingmoUI.Units.largeSpacing

                LingmoUI.Card {
                    Layout.fillWidth: true
                    implicitHeight: info.implicitHeight + LingmoUI.Units.largeSpacing * 2

                    ColumnLayout {
                        id: info
                        anchors.fill: parent
                        anchors.margins: LingmoUI.Units.largeSpacing
                        anchors.leftMargin: LingmoUI.Units.largeSpacing * 1.5
                        anchors.rightMargin: LingmoUI.Units.largeSpacing * 1.5
                        spacing: LingmoUI.Units.largeSpacing

                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: systemInfo.cpuModel
                                font.pointSize: LingmoUI.Units.fontSubheading
                                font.bold: true
                                color: LingmoUI.Theme.textColor
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                text: files.formatPercent(systemInfo.cpuUsage)
                                font.pointSize: LingmoUI.Units.fontSubheading
                                font.bold: true
                                color: LingmoUI.Theme.greenColor
                            }
                        }

                        GridLayout {
                            columns: 3
                            columnSpacing: LingmoUI.Units.largeSpacing * 2
                            rowSpacing: LingmoUI.Units.smallSpacing
                            Layout.fillWidth: true

                            Repeater {
                                model: [
                                    { name: qsTr("Núcleos / threads"), value: systemInfo.physicalCores + " / " + systemInfo.threads },
                                    { name: qsTr("Frequência atual"), value: systemInfo.frequencyText.length > 0 ? systemInfo.frequencyText : "—" },
                                    { name: qsTr("Temperatura"), value: systemInfo.temperature >= 0
                                                                       ? Math.round(systemInfo.temperature) + " °C" : qsTr("indisponível") },
                                    { name: qsTr("Carga média (1, 5, 15 min)"), value: page.loadText() },
                                    { name: qsTr("Ligado há"), value: systemInfo.uptime },
                                    { name: qsTr("Processos"), value: processes.processCount > 0 ? processes.processCount : "—" }
                                ]

                                delegate: ColumnLayout {
                                    required property var modelData
                                    spacing: 0
                                    Layout.fillWidth: true
                                    Label {
                                        text: modelData.name
                                        color: LingmoUI.Theme.secondaryTextColor
                                        font.pointSize: LingmoUI.Units.fontCaption
                                    }
                                    Label {
                                        text: modelData.value
                                        color: LingmoUI.Theme.textColor
                                    }
                                }
                            }
                        }
                    }
                }

                LingmoUI.Card {
                    Layout.fillWidth: true
                    implicitHeight: cores.implicitHeight + LingmoUI.Units.largeSpacing * 2

                    GridLayout {
                        id: cores
                        anchors.fill: parent
                        anchors.margins: LingmoUI.Units.largeSpacing
                        anchors.leftMargin: LingmoUI.Units.largeSpacing * 1.5
                        anchors.rightMargin: LingmoUI.Units.largeSpacing * 1.5
                        columns: width > 560 ? 3 : 2
                        columnSpacing: LingmoUI.Units.largeSpacing * 2
                        rowSpacing: LingmoUI.Units.smallSpacing

                        Repeater {
                            model: systemInfo.coreUsages

                            delegate: RowLayout {
                                required property int index
                                required property var modelData
                                spacing: LingmoUI.Units.smallSpacing
                                Layout.fillWidth: true

                                Label {
                                    text: qsTr("CPU %1").arg(index + 1)
                                    color: LingmoUI.Theme.secondaryTextColor
                                    font.pointSize: LingmoUI.Units.fontCaption
                                    Layout.preferredWidth: 52
                                }
                                UsageBar {
                                    value: modelData
                                    fillColor: modelData > 0.85 ? LingmoUI.Theme.orangeColor : LingmoUI.Theme.greenColor
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: Math.round(modelData * 100) + "%"
                                    color: LingmoUI.Theme.textColor
                                    font.pointSize: LingmoUI.Units.fontCaption
                                    horizontalAlignment: Text.AlignRight
                                    Layout.preferredWidth: 36
                                }
                            }
                        }
                    }
                }

                Label {
                    text: qsTr("Aplicativos por uso de CPU")
                    font.bold: true
                    color: LingmoUI.Theme.textColor
                    Layout.topMargin: LingmoUI.Units.smallSpacing
                }
                Label {
                    text: qsTr("Os 30 que mais usaram o processador nos últimos segundos (100% = todos os núcleos).")
                    color: LingmoUI.Theme.secondaryTextColor
                    font.pointSize: LingmoUI.Units.fontCaption
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Layout.bottomMargin: LingmoUI.Units.smallSpacing
                }
            }

            footer: Item {
                width: apps.width
                height: apps.count > 0 ? LingmoUI.Units.largeSpacing : 80
                LingmoUI.Spinner {
                    anchors.centerIn: parent
                    visible: apps.count === 0
                    running: visible
                    size: LingmoUI.Units.iconSizes.medium
                }
            }
        }
    }
}
