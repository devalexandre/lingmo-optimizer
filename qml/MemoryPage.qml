import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

Item {
    id: page

    signal back()

    // Sample processes only while this page is on screen
    Binding {
        target: processes
        property: "active"
        value: page.StackView.status === StackView.Active
        restoreMode: Binding.RestoreValue
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
            title: qsTr("Memória")
            Layout.fillWidth: true
            Layout.rightMargin: LingmoUI.Units.largeSpacing
            onBack: page.back()
        }

        AppList {
            id: apps
            metric: "memory"
            model: processes.memoryModel
            Layout.fillWidth: true
            Layout.fillHeight: true

            header: ColumnLayout {
                width: apps.width - LingmoUI.Units.largeSpacing
                spacing: LingmoUI.Units.largeSpacing

                LingmoUI.Card {
                    Layout.fillWidth: true
                    implicitHeight: breakdown.implicitHeight + LingmoUI.Units.largeSpacing * 2

                    ColumnLayout {
                        id: breakdown
                        anchors.fill: parent
                        anchors.margins: LingmoUI.Units.largeSpacing
                        anchors.leftMargin: LingmoUI.Units.largeSpacing * 1.5
                        anchors.rightMargin: LingmoUI.Units.largeSpacing * 1.5
                        spacing: LingmoUI.Units.smallSpacing

                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: qsTr("%1 em uso de %2").arg(files.formatBytes(systemInfo.memUsed)).arg(files.formatBytes(systemInfo.memTotal))
                                font.pointSize: LingmoUI.Units.fontSubheading
                                font.bold: true
                                color: LingmoUI.Theme.textColor
                                Layout.fillWidth: true
                            }
                            Label {
                                text: files.formatPercent(systemInfo.ramUsage)
                                color: LingmoUI.Theme.secondaryTextColor
                            }
                        }

                        // used | cache | free, in one bar
                        Rectangle {
                            id: stacked
                            Layout.fillWidth: true
                            implicitHeight: LingmoUI.Units.largeSpacing
                            radius: height / 2
                            color: LingmoUI.Theme.alternateBackgroundColor
                            clip: true
                            readonly property real total: Math.max(1, systemInfo.memTotal)

                            Row {
                                anchors.fill: parent
                                Rectangle {
                                    height: parent.height
                                    width: stacked.width * systemInfo.memUsed / stacked.total
                                    color: LingmoUI.Theme.blueColor
                                    Behavior on width { NumberAnimation { duration: LingmoUI.Units.longDuration; easing.type: LingmoUI.Units.easing } }
                                }
                                Rectangle {
                                    height: parent.height
                                    width: stacked.width * Math.min(systemInfo.memCache, systemInfo.memAvailable) / stacked.total
                                    color: LingmoUI.Theme.purpleColor
                                    opacity: 0.55
                                    Behavior on width { NumberAnimation { duration: LingmoUI.Units.longDuration; easing.type: LingmoUI.Units.easing } }
                                }
                            }
                        }

                        GridLayout {
                            columns: 3
                            columnSpacing: LingmoUI.Units.largeSpacing * 2
                            rowSpacing: 2
                            Layout.fillWidth: true
                            Layout.topMargin: LingmoUI.Units.smallSpacing

                            Repeater {
                                model: [
                                    { color: LingmoUI.Theme.blueColor, name: qsTr("Em uso"), value: systemInfo.memUsed,
                                      hint: qsTr("Programas abertos e o sistema") },
                                    { color: Qt.rgba(LingmoUI.Theme.purpleColor.r, LingmoUI.Theme.purpleColor.g, LingmoUI.Theme.purpleColor.b, 0.55),
                                      name: qsTr("Cache e buffers"), value: systemInfo.memCache,
                                      hint: qsTr("Arquivos guardados para abrir rápido; liberado sozinho quando falta") },
                                    { color: LingmoUI.Theme.alternateBackgroundColor, name: qsTr("Disponível"), value: systemInfo.memAvailable,
                                      hint: qsTr("O que os programas ainda podem usar") }
                                ]

                                delegate: RowLayout {
                                    required property var modelData
                                    spacing: LingmoUI.Units.smallSpacing
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignTop

                                    Rectangle {
                                        width: LingmoUI.Units.smallSpacing * 2
                                        height: width
                                        radius: 3
                                        color: modelData.color
                                        border.width: 1
                                        border.color: LingmoUI.Theme.borderColor
                                        Layout.alignment: Qt.AlignTop
                                        Layout.topMargin: 4
                                    }
                                    ColumnLayout {
                                        spacing: 0
                                        Layout.fillWidth: true
                                        Label {
                                            text: modelData.name + ": " + files.formatBytes(modelData.value)
                                            color: LingmoUI.Theme.textColor
                                        }
                                        Label {
                                            text: modelData.hint
                                            color: LingmoUI.Theme.secondaryTextColor
                                            font.pointSize: LingmoUI.Units.fontCaption
                                            wrapMode: Text.WordWrap
                                            Layout.fillWidth: true
                                        }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.topMargin: LingmoUI.Units.smallSpacing
                            implicitHeight: 1
                            color: LingmoUI.Theme.dividerColor
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: LingmoUI.Units.largeSpacing

                            Label {
                                text: qsTr("Swap")
                                color: LingmoUI.Theme.textColor
                                Layout.preferredWidth: 90
                            }
                            UsageBar {
                                visible: systemInfo.swapTotal > 0
                                value: systemInfo.swapTotal > 0 ? systemInfo.swapUsed / systemInfo.swapTotal : 0
                                fillColor: LingmoUI.Theme.orangeColor
                                Layout.fillWidth: true
                            }
                            Label {
                                text: systemInfo.swapTotal > 0
                                      ? qsTr("%1 de %2").arg(files.formatBytes(systemInfo.swapUsed)).arg(files.formatBytes(systemInfo.swapTotal))
                                      : qsTr("Sem swap configurado")
                                color: LingmoUI.Theme.secondaryTextColor
                                Layout.fillWidth: systemInfo.swapTotal <= 0
                            }
                        }

                        RowLayout {
                            visible: systemInfo.zramOriginal > 0
                            Layout.fillWidth: true
                            spacing: LingmoUI.Units.largeSpacing

                            Label {
                                text: qsTr("zram")
                                color: LingmoUI.Theme.textColor
                                Layout.preferredWidth: 90
                            }
                            Label {
                                text: qsTr("%1 de dados comprimidos em %2 de RAM (%3×)")
                                      .arg(files.formatBytes(systemInfo.zramOriginal))
                                      .arg(files.formatBytes(systemInfo.zramMemUsed))
                                      .arg(systemInfo.zramMemUsed > 0 ? (systemInfo.zramOriginal / systemInfo.zramMemUsed).toFixed(1).replace(".", ",") : "0")
                                color: LingmoUI.Theme.secondaryTextColor
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }
                    }
                }

                Label {
                    text: qsTr("Aplicativos por uso de memória")
                    font.bold: true
                    color: LingmoUI.Theme.textColor
                    Layout.topMargin: LingmoUI.Units.smallSpacing
                }
                Label {
                    text: qsTr("Memória proporcional (PSS): a parte compartilhada entre processos é dividida entre eles.")
                    color: LingmoUI.Theme.secondaryTextColor
                    font.pointSize: LingmoUI.Units.fontCaption
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Layout.bottomMargin: LingmoUI.Units.smallSpacing
                }
            }

            footer: Item {
                width: apps.width
                height: processes.ready || apps.count > 0 ? LingmoUI.Units.largeSpacing : 80
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
