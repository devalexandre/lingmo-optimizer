import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// Applications grouped from their processes, by memory or by CPU. Click a
// row to see its processes. "Encerrar" only for the user's own, non-critical
// apps (decided in C++, re-checked before signalling).
ListView {
    id: list

    property string metric: "memory"   // memory | cpu
    property var expanded: ({})

    clip: true
    spacing: LingmoUI.Units.smallSpacing
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {}

    LingmoUI.WheelHandler { target: list }

    // The header (cards above the list) can grow after the first layout, which
    // moves the list's origin up: stay at the very top if we were there
    property real lastOriginY: 0
    onOriginYChanged: {
        if (Math.abs(contentY - lastOriginY) < 2)
            contentY = originY
        lastOriginY = originY
    }

    function valueText(memory, cpu) {
        return metric === "memory" ? files.formatBytes(memory) : files.formatPercent(cpu)
    }
    function fraction(memory, cpu) {
        return metric === "memory" ? (systemInfo.memTotal > 0 ? memory / systemInfo.memTotal : 0) : cpu
    }

    delegate: Card {
        id: row

        required property int index
        required property string key
        required property string name
        required property string iconName
        required property real memory
        required property real cpu
        required property int processCount
        required property bool killable
        required property var processes
        readonly property bool isOpen: !!list.expanded[key]

        width: ListView.view.width - LingmoUI.Units.largeSpacing
        implicitHeight: content.implicitHeight + LingmoUI.Units.largeSpacing * 1.4
        hoverEnabled: true

        ColumnLayout {
            id: content
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: LingmoUI.Units.largeSpacing * 0.7
            anchors.leftMargin: LingmoUI.Units.largeSpacing
            anchors.rightMargin: LingmoUI.Units.largeSpacing * 1.5
            spacing: LingmoUI.Units.smallSpacing

            RowLayout {
                Layout.fillWidth: true
                spacing: LingmoUI.Units.largeSpacing

                LingmoUI.IconItem {
                    source: row.isOpen ? "go-down" : "go-next"
                    Layout.preferredWidth: LingmoUI.Units.iconSizes.small
                    Layout.preferredHeight: LingmoUI.Units.iconSizes.small
                    opacity: 0.7
                }

                LingmoUI.IconItem {
                    source: row.iconName
                    Layout.preferredWidth: LingmoUI.Units.iconSizes.smallMedium
                    Layout.preferredHeight: LingmoUI.Units.iconSizes.smallMedium
                }

                ColumnLayout {
                    spacing: 0
                    Layout.preferredWidth: 220
                    Layout.fillWidth: true

                    Label {
                        text: row.name
                        color: LingmoUI.Theme.textColor
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        text: row.processCount === 1 ? qsTr("1 processo") : qsTr("%1 processos").arg(row.processCount)
                        color: LingmoUI.Theme.disabledTextColor
                        font.pointSize: LingmoUI.Theme.fontSize * 0.85
                    }
                }

                UsageBar {
                    value: list.fraction(row.memory, row.cpu)
                    fillColor: list.metric === "memory" ? LingmoUI.Theme.blueColor : LingmoUI.Theme.greenColor
                    Layout.preferredWidth: 140
                }

                Label {
                    text: list.valueText(row.memory, row.cpu)
                    color: LingmoUI.Theme.textColor
                    horizontalAlignment: Text.AlignRight
                    Layout.preferredWidth: 70
                }

                Item {
                    Layout.preferredWidth: 90
                    implicitHeight: endButton.implicitHeight

                    ActionButton {
                        id: endButton
                        anchors.right: parent.right
                        visible: row.killable
                        destructive: true
                        text: qsTr("Encerrar")
                        onClicked: {
                            confirmEnd.title = qsTr("Encerrar %1?").arg(row.name)
                            confirmEnd.message = qsTr("O aplicativo será fechado (%1). Trabalho não salvo nele pode ser perdido.")
                                                 .arg(row.processCount === 1 ? qsTr("1 processo") : qsTr("%1 processos").arg(row.processCount))
                            confirmEnd.payload = { key: row.key, pid: 0 }
                            confirmEnd.open()
                        }
                    }
                }
            }

            // Processes of the app
            Repeater {
                model: row.isOpen ? row.processes : []

                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.leftMargin: LingmoUI.Units.iconSizes.small + LingmoUI.Units.largeSpacing
                    spacing: LingmoUI.Units.largeSpacing

                    Label {
                        text: modelData.pid
                        color: LingmoUI.Theme.disabledTextColor
                        font.pointSize: LingmoUI.Theme.fontSize * 0.9
                        Layout.preferredWidth: 60
                    }
                    Label {
                        text: modelData.command
                        color: LingmoUI.Theme.textColor
                        font.pointSize: LingmoUI.Theme.fontSize * 0.9
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    Label {
                        text: list.valueText(modelData.memory, modelData.cpu)
                        color: LingmoUI.Theme.disabledTextColor
                        font.pointSize: LingmoUI.Theme.fontSize * 0.9
                        horizontalAlignment: Text.AlignRight
                        Layout.preferredWidth: 70
                    }
                    Item {
                        Layout.preferredWidth: 90
                        implicitHeight: LingmoUI.Units.iconSizes.medium
                        IconButton {
                            anchors.right: parent.right
                            visible: modelData.killable
                            iconName: "process-stop"
                            tipText: qsTr("Encerrar este processo")
                            onClicked: {
                                confirmEnd.title = qsTr("Encerrar o processo %1?").arg(modelData.pid)
                                confirmEnd.message = modelData.command
                                confirmEnd.payload = { key: "", pid: modelData.pid }
                                confirmEnd.open()
                            }
                        }
                    }
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            z: -1
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                var e = Object.assign({}, list.expanded)
                e[row.key] = !e[row.key]
                list.expanded = e
            }
        }
    }

    ConfirmDialog {
        id: confirmEnd
        parent: Overlay.overlay
        confirmText: qsTr("Encerrar")
        onConfirmed: function(payload) {
            if (payload.pid > 0)
                processes.terminateProcess(payload.pid)
            else
                processes.terminateApp(payload.key)
        }
    }

    ConfirmDialog {
        id: confirmForce
        parent: Overlay.overlay
        confirmText: qsTr("Forçar")
        onConfirmed: function(payload) {
            if (payload.pid > 0)
                processes.forceKillProcess(payload.pid)
            else
                processes.forceKillApp(payload.key)
        }
    }

    Connections {
        target: processes
        enabled: list.visible
        function onAppStillRunning(key, name, remaining) {
            confirmForce.title = qsTr("%1 não fechou").arg(name)
            confirmForce.message = qsTr("O aplicativo não respondeu ao pedido para fechar. Forçar o encerramento? Dados não salvos serão perdidos.")
            confirmForce.payload = { key: key, pid: 0 }
            confirmForce.open()
        }
        function onProcessStillRunning(pid, name) {
            confirmForce.title = qsTr("O processo %1 não fechou").arg(pid)
            confirmForce.message = qsTr("%1 não respondeu ao pedido para fechar. Forçar o encerramento?").arg(name)
            confirmForce.payload = { key: "", pid: pid }
            confirmForce.open()
        }
    }
}
