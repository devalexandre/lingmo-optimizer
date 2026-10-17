import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

Item {
    id: page

    signal back()

    property alias currentTab: tabs.currentIndex

    // Trash confirmation shared by the analyzer and the suggestions
    function confirmTrash(paths, size, onDone) {
        trashDialog.title = paths.length === 1 ? qsTr("Mover para a lixeira?") : qsTr("Mover %1 itens para a lixeira?").arg(paths.length)
        trashDialog.message = qsTr("%1 serão movidos para a lixeira. Você pode restaurá-los de lá; o espaço só é liberado ao esvaziar a lixeira.")
                              .arg(files.formatBytes(size))
        trashDialog.details = paths.join("\n")
        trashDialog.payload = { paths: paths, done: onDone }
        trashDialog.open()
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
            title: qsTr("Armazenamento")
            Layout.fillWidth: true
            Layout.rightMargin: LingmoUI.Units.largeSpacing
            onBack: page.back()

            LingmoUI.SegmentedControl {
                id: tabs
                objectName: "storageTabs"
                model: [qsTr("Discos"), qsTr("Onde está o espaço"), qsTr("Sugestões")]
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            // ---- mounted filesystems
            ListView {
                id: mountList
                clip: true
                spacing: LingmoUI.Units.smallSpacing
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                model: storage.mounts

                LingmoUI.WheelHandler { target: mountList }

                delegate: LingmoUI.Card {
                    required property var modelData
                    width: mountList.width - LingmoUI.Units.largeSpacing
                    implicitHeight: mountRow.implicitHeight + LingmoUI.Units.largeSpacing * 2
                    hoverEnabled: true

                    RowLayout {
                        id: mountRow
                        anchors.fill: parent
                        anchors.margins: LingmoUI.Units.largeSpacing
                        anchors.leftMargin: LingmoUI.Units.largeSpacing * 1.5
                        anchors.rightMargin: LingmoUI.Units.largeSpacing * 1.5
                        spacing: LingmoUI.Units.largeSpacing

                        LingmoUI.IconItem {
                            source: modelData.mountPoint === "/" ? "drive-harddisk-system" : "drive-harddisk"
                            Layout.preferredWidth: LingmoUI.Units.iconSizes.medium
                            Layout.preferredHeight: LingmoUI.Units.iconSizes.medium
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    text: modelData.mountPoint
                                    font.bold: true
                                    color: LingmoUI.Theme.textColor
                                }
                                Label {
                                    text: modelData.device + " · " + modelData.fsType + (modelData.readOnly ? " · " + qsTr("somente leitura") : "")
                                          + (modelData.alsoMounted.length > 0 ? " · " + qsTr("também em %1").arg(modelData.alsoMounted.join(", ")) : "")
                                    color: LingmoUI.Theme.secondaryTextColor
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                            UsageBar {
                                value: modelData.fraction
                                fillColor: modelData.fraction > 0.9 ? LingmoUI.Theme.redColor : LingmoUI.Theme.orangeColor
                                Layout.fillWidth: true
                            }
                            Label {
                                text: qsTr("%1 usados de %2 · %3 livres · %4")
                                      .arg(files.formatBytes(modelData.used)).arg(files.formatBytes(modelData.total))
                                      .arg(files.formatBytes(modelData.free)).arg(files.formatPercent(modelData.fraction))
                                color: LingmoUI.Theme.secondaryTextColor
                                font.pointSize: LingmoUI.Units.fontCaption
                            }
                        }

                        LingmoUI.ActionButton {
                            text: qsTr("Analisar")
                            onClicked: {
                                storage.analyze(modelData.mountPoint)
                                tabs.currentIndex = 1
                            }
                        }
                    }
                }
            }

            AnalyzerView {
                id: analyzer
                active: tabs.currentIndex === 1
                onTrashRequested: function(path, size) {
                    page.confirmTrash([path], size, function() { storage.forget(path) })
                }
            }

            SuggestionsView {
                active: tabs.currentIndex === 2
                onTrashRequested: function(paths, size, done) { page.confirmTrash(paths, size, done) }
            }
        }
    }

    Component.onCompleted: storage.refreshMounts()

    ConfirmDialog {
        id: trashDialog
        parent: Overlay.overlay
        confirmText: qsTr("Mover para a lixeira")
        onConfirmed: function(payload) {
            var moved = []
            for (var i = 0; i < payload.paths.length; ++i) {
                if (files.moveToTrash(payload.paths[i]))
                    moved.push(payload.paths[i])
            }
            if (moved.length > 0) {
                for (var j = 0; j < moved.length; ++j)
                    storage.forget(moved[j])
                if (payload.done)
                    payload.done(moved)
                storage.refreshMounts()
                systemInfo.refresh()
            }
        }
    }
}
