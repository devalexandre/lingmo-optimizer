import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// "Onde está o espaço": folder sizes like baobab/ncdu, largest first
ColumnLayout {
    id: view

    property bool active: false

    signal trashRequested(string path, real size)

    spacing: LingmoUI.Units.smallSpacing

    onActiveChanged: {
        if (active && !storage.hasResult && !storage.scanning)
            storage.analyze("/")
    }

    function rootChoices() {
        var list = [{ text: qsTr("Sistema (/)"), path: "/" }, { text: qsTr("Pasta pessoal"), path: files.homePath }]
        for (var i = 0; i < storage.mounts.length; ++i) {
            var m = storage.mounts[i].mountPoint
            if (m !== "/")
                list.push({ text: m, path: m })
        }
        return list
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.rightMargin: LingmoUI.Units.largeSpacing
        spacing: LingmoUI.Units.smallSpacing

        Label {
            text: qsTr("Analisar:")
            color: LingmoUI.Theme.textColor
        }

        ComboBox {
            id: rootCombo
            model: view.rootChoices()
            textRole: "text"
            Layout.preferredWidth: 220
            currentIndex: {
                var choices = view.rootChoices()
                for (var i = 0; i < choices.length; ++i) {
                    if (choices[i].path === storage.rootPath)
                        return i
                }
                return 0
            }
            onActivated: function(index) { storage.analyze(view.rootChoices()[index].path) }
        }

        Item { Layout.fillWidth: true }

        LingmoUI.ActionButton {
            text: storage.scanning ? qsTr("Parar") : qsTr("Reanalisar")
            destructive: storage.scanning
            subtle: true
            onClicked: storage.scanning ? storage.cancel() : storage.rescan()
        }
    }

    // Breadcrumbs
    Flow {
        Layout.fillWidth: true
        Layout.rightMargin: LingmoUI.Units.largeSpacing
        spacing: 2

        Repeater {
            model: storage.breadcrumbs

            delegate: Row {
                required property int index
                required property var modelData
                spacing: 2

                Label {
                    visible: index > 0
                    text: "›"
                    color: LingmoUI.Theme.secondaryTextColor
                    anchors.verticalCenter: parent.verticalCenter
                }

                Rectangle {
                    readonly property bool last: index === storage.breadcrumbs.length - 1
                    width: crumbLabel.implicitWidth + LingmoUI.Units.smallSpacing * 2
                    height: crumbLabel.implicitHeight + 6
                    radius: LingmoUI.Theme.smallRadius
                    color: crumbMouse.containsMouse && !last
                           ? LingmoUI.Theme.hoverColor
                           : "transparent"
                    Behavior on color { ColorAnimation { duration: LingmoUI.Units.veryShortDuration } }

                    Label {
                        id: crumbLabel
                        anchors.centerIn: parent
                        text: modelData.name
                        color: parent.last ? LingmoUI.Theme.textColor : LingmoUI.Theme.highlightColor
                        font.bold: parent.last
                    }
                    MouseArea {
                        id: crumbMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: !parent.last
                        cursorShape: Qt.PointingHandCursor
                        onClicked: storage.goTo(index)
                    }
                }
            }
        }
    }

    // Status
    RowLayout {
        Layout.fillWidth: true
        Layout.rightMargin: LingmoUI.Units.largeSpacing
        spacing: LingmoUI.Units.smallSpacing

        LingmoUI.Spinner {
            visible: storage.scanning
            running: visible
            size: LingmoUI.Units.iconSizes.small
        }
        Label {
            Layout.fillWidth: true
            elide: Text.ElideMiddle
            color: LingmoUI.Theme.secondaryTextColor
            font.pointSize: LingmoUI.Units.fontCaption
            text: storage.scanning
                  ? qsTr("Analisando... %1 em %2 itens · %3").arg(files.formatBytes(storage.scannedBytes))
                        .arg(Number(storage.scannedEntries).toLocaleString(Qt.locale("pt_BR"), "f", 0)).arg(storage.scanningPath)
                  : qsTr("%1 nesta pasta").arg(files.formatBytes(storage.currentSize))
                        + (storage.currentDenied ? " · " + qsTr("sem permissão para ler esta pasta")
                           : storage.currentIncomplete ? " · " + qsTr("algumas pastas não puderam ser lidas (sem permissão): o total é parcial") : "")
        }
    }

    ListView {
        id: list
        objectName: "analyzerList"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        spacing: LingmoUI.Units.smallSpacing
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        model: storage.model

        LingmoUI.WheelHandler { target: list }

        // The top-level folder still being read, with its size so far
        header: Item {
            width: list.width
            height: storage.pendingName.length > 0 ? pendingCard.height + list.spacing : 0
            visible: height > 0

            LingmoUI.Card {
                id: pendingCard
                width: parent.width - LingmoUI.Units.largeSpacing
                height: pendingRow.implicitHeight + LingmoUI.Units.largeSpacing

                RowLayout {
                    id: pendingRow
                    anchors.fill: parent
                    anchors.leftMargin: LingmoUI.Units.largeSpacing
                    anchors.rightMargin: LingmoUI.Units.largeSpacing
                    spacing: LingmoUI.Units.largeSpacing

                    LingmoUI.Spinner {}
                    Label {
                        text: storage.pendingName
                        color: LingmoUI.Theme.textColor
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    Label {
                        text: qsTr("analisando...")
                        color: LingmoUI.Theme.secondaryTextColor
                        font.pointSize: LingmoUI.Units.fontCaption
                    }
                    Label {
                        text: files.formatBytes(storage.pendingSize)
                        color: LingmoUI.Theme.textColor
                        horizontalAlignment: Text.AlignRight
                        Layout.preferredWidth: 72
                    }
                    Item {
                        Layout.preferredWidth: 44 + LingmoUI.Units.smallControlHeight * 2 + LingmoUI.Units.largeSpacing * 2
                    }
                }
            }
        }

        delegate: LingmoUI.Card {
            id: row

            required property int index
            required property string name
            required property string path
            required property real size
            required property real fraction
            required property string kind
            required property bool denied
            required property bool incomplete
            required property real entries
            required property bool canTrash
            readonly property bool openable: (kind === "dir" && entries > 0) || kind === "mount"

            width: ListView.view.width - LingmoUI.Units.largeSpacing
            implicitHeight: rowLayout.implicitHeight + LingmoUI.Units.largeSpacing
            hoverEnabled: true

            MouseArea {
                anchors.fill: parent
                cursorShape: row.openable ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: if (row.openable) storage.open(row.index)
            }

            RowLayout {
                id: rowLayout
                anchors.fill: parent
                anchors.topMargin: LingmoUI.Units.smallSpacing
                anchors.bottomMargin: LingmoUI.Units.smallSpacing
                anchors.leftMargin: LingmoUI.Units.largeSpacing
                anchors.rightMargin: LingmoUI.Units.largeSpacing
                spacing: LingmoUI.Units.largeSpacing

                LingmoUI.IconItem {
                    source: row.kind === "dir" ? "folder" : row.kind === "mount" ? "drive-harddisk"
                          : row.kind === "skipped" ? "folder-locked" : row.kind === "others" ? "document-multiple" : "text-x-generic"
                    Layout.preferredWidth: LingmoUI.Units.iconSizes.smallMedium
                    Layout.preferredHeight: LingmoUI.Units.iconSizes.smallMedium
                }

                ColumnLayout {
                    spacing: 3
                    Layout.fillWidth: true

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: LingmoUI.Units.smallSpacing
                        Label {
                            text: row.name
                            color: LingmoUI.Theme.textColor
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                        Label {
                            visible: text.length > 0
                            text: row.denied ? qsTr("sem permissão")
                                : row.kind === "mount" ? qsTr("outro sistema de arquivos")
                                : row.kind === "skipped" ? qsTr("arquivos virtuais do sistema")
                                : row.incomplete ? qsTr("parcial") : ""
                            color: row.denied ? LingmoUI.Theme.orangeColor : LingmoUI.Theme.secondaryTextColor
                            font.pointSize: LingmoUI.Units.fontCaption
                        }
                    }
                    UsageBar {
                        value: row.fraction
                        fillColor: row.kind === "dir" ? LingmoUI.Theme.orangeColor : LingmoUI.Theme.blueColor
                        Layout.fillWidth: true
                        visible: row.kind !== "mount" && row.kind !== "skipped"
                    }
                }

                Label {
                    text: row.kind === "mount" || row.kind === "skipped" ? "" : files.formatBytes(row.size)
                    color: LingmoUI.Theme.textColor
                    horizontalAlignment: Text.AlignRight
                    Layout.preferredWidth: 72
                }
                Label {
                    text: row.kind === "mount" || row.kind === "skipped" ? "" : files.formatPercent(row.fraction)
                    color: LingmoUI.Theme.secondaryTextColor
                    font.pointSize: LingmoUI.Units.fontCaption
                    horizontalAlignment: Text.AlignRight
                    Layout.preferredWidth: 44
                }

                LingmoUI.IconButton {
                    iconName: "document-open-folder"
                    tipText: qsTr("Abrir no gerenciador de arquivos")
                    enabled: row.kind !== "skipped"
                    onClicked: files.openInFileManager(row.path)
                }
                LingmoUI.IconButton {
                    iconName: "edit-delete"
                    tipText: qsTr("Mover para a lixeira")
                    visible: row.canTrash
                    onClicked: view.trashRequested(row.path, row.size)
                }
                Item {
                    visible: !row.canTrash
                    implicitWidth: LingmoUI.Units.smallControlHeight
                    implicitHeight: LingmoUI.Units.smallControlHeight
                }
            }
        }

        LingmoUI.EmptyState {
            anchors.centerIn: parent
            width: Math.min(implicitWidth, list.width - LingmoUI.Units.largeSpacing * 2)
            visible: list.count === 0 && !storage.scanning && storage.hasResult
            iconName: storage.currentDenied ? "folder-locked" : "folder-open"
            title: storage.currentDenied ? qsTr("Sem permissão para ler esta pasta") : qsTr("Pasta vazia")
        }
    }
}
