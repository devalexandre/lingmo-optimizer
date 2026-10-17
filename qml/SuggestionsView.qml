import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// "Sugestões": what usually eats disk space, with the right action for each
ColumnLayout {
    id: view

    property bool active: false
    property var removed: ({})       // paths/models handled since the last refresh
    property var showAll: ({})

    signal trashRequested(var paths, real size, var done)

    spacing: LingmoUI.Units.smallSpacing

    onActiveChanged: {
        if (active && !suggestions.loaded && !suggestions.busy)
            suggestions.refresh()
    }

    function markRemoved(keys) {
        var r = Object.assign({}, removed)
        for (var i = 0; i < keys.length; ++i)
            r[keys[i]] = true
        removed = r
    }

    function visibleChildren(item) {
        var list = []
        for (var i = 0; i < item.children.length; ++i) {
            var c = item.children[i]
            var key = c.ollamaName.length > 0 ? "ollama:" + c.ollamaName : c.path
            if (!removed[key] && !(c.trashPaths.length > 0 && removed[c.trashPaths[0]]))
                list.push(c)
        }
        return list
    }

    Connections {
        target: suggestions
        function onItemsChanged() { view.removed = ({}) }
        function onOllamaRemoved(name, ok, message) {
            if (!ok)
                messageDialog.show(qsTr("Não foi possível remover %1").arg(name), message)
        }
    }
    Connections {
        target: cleaner
        function onFinished() {
            if (suggestions.loaded)
                suggestions.refresh()
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.rightMargin: LingmoUI.Units.largeSpacing
        spacing: LingmoUI.Units.smallSpacing

        LingmoUI.Spinner {
            visible: suggestions.busy
            running: visible
            size: LingmoUI.Units.iconSizes.small
        }
        Label {
            text: suggestions.busy ? qsTr("Procurando o que ocupa espaço na sua pasta pessoal e no sistema...")
                                   : qsTr("O que costuma ocupar espaço, do maior para o menor.")
            color: LingmoUI.Theme.secondaryTextColor
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        LingmoUI.ActionButton {
            text: qsTr("Atualizar")
            enabled: !suggestions.busy
            onClicked: suggestions.refresh()
        }
    }

    ListView {
        id: list
        objectName: "suggestionList"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        spacing: LingmoUI.Units.smallSpacing
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        model: suggestions.items

        LingmoUI.WheelHandler { target: list }

        delegate: LingmoUI.Card {
            id: card

            required property var modelData
            readonly property var item: modelData
            readonly property var kids: view.visibleChildren(item)
            readonly property bool expandedAll: !!view.showAll[item.id]

            width: ListView.view.width - LingmoUI.Units.largeSpacing
            implicitHeight: body.implicitHeight + LingmoUI.Units.largeSpacing * 2

            ColumnLayout {
                id: body
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: LingmoUI.Units.largeSpacing
                anchors.leftMargin: LingmoUI.Units.largeSpacing * 1.5
                anchors.rightMargin: LingmoUI.Units.largeSpacing * 1.5
                spacing: LingmoUI.Units.smallSpacing

                RowLayout {
                    Layout.fillWidth: true
                    spacing: LingmoUI.Units.largeSpacing

                    LingmoUI.IconItem {
                        source: card.item.icon
                        Layout.preferredWidth: LingmoUI.Units.iconSizes.medium
                        Layout.preferredHeight: LingmoUI.Units.iconSizes.medium
                        Layout.alignment: Qt.AlignTop
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Label {
                            text: card.item.title
                            font.bold: true
                            color: LingmoUI.Theme.textColor
                            Layout.fillWidth: true
                        }
                        Label {
                            text: card.item.description
                            color: LingmoUI.Theme.secondaryTextColor
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        Rectangle {
                            visible: card.item.command.length > 0
                            Layout.topMargin: 4
                            implicitWidth: commandLabel.implicitWidth + LingmoUI.Units.largeSpacing
                            implicitHeight: commandLabel.implicitHeight + LingmoUI.Units.smallSpacing
                            radius: LingmoUI.Theme.smallRadius
                            color: LingmoUI.Theme.alternateBackgroundColor
                            Label {
                                id: commandLabel
                                anchors.centerIn: parent
                                text: card.item.command
                                font.family: "Monospace"
                                color: LingmoUI.Theme.textColor
                            }
                        }
                    }

                    Label {
                        text: files.formatBytes(card.item.size)
                        font.pointSize: LingmoUI.Units.fontSubheading
                        font.bold: true
                        color: LingmoUI.Theme.textColor
                        Layout.alignment: Qt.AlignTop
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: LingmoUI.Units.iconSizes.medium + LingmoUI.Units.largeSpacing
                    spacing: LingmoUI.Units.smallSpacing
                    visible: cleanButton.visible || trashButton.visible || openButton.visible

                    LingmoUI.ActionButton {
                        id: cleanButton
                        visible: ["pacman-cache", "journal", "flatpak", "user-cache"].indexOf(card.item.action) >= 0
                        primary: true
                        enabled: !cleaner.running
                        text: card.item.action === "user-cache" ? qsTr("Limpar caches seguros") : qsTr("Limpar")
                        onClicked: cleaner.run([card.item.action])
                    }
                    LingmoUI.ActionButton {
                        id: trashButton
                        visible: card.item.action === "empty-trash"
                        destructive: true
                        subtle: true
                        text: qsTr("Esvaziar lixeira")
                        onClicked: {
                            emptyTrashDialog.payload = card.item.size
                            emptyTrashDialog.open()
                        }
                    }
                    LingmoUI.ActionButton {
                        id: openButton
                        visible: card.item.path.length > 0 && card.item.action !== "none"
                                 || (card.item.action === "none" && card.item.path.length > 0 && card.kids.length === 0)
                        text: qsTr("Abrir no gerenciador de arquivos")
                        onClicked: files.openInFileManager(card.item.path)
                    }
                }

                // Itemised list (models, big files, projects...)
                Repeater {
                    model: card.expandedAll ? card.kids : card.kids.slice(0, 5)

                    delegate: RowLayout {
                        required property var modelData
                        readonly property var entry: modelData
                        Layout.fillWidth: true
                        Layout.leftMargin: LingmoUI.Units.iconSizes.medium + LingmoUI.Units.largeSpacing
                        spacing: LingmoUI.Units.smallSpacing

                        ColumnLayout {
                            spacing: 0
                            Layout.fillWidth: true
                            Label {
                                text: entry.name
                                color: LingmoUI.Theme.textColor
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                            Label {
                                visible: entry.detail.length > 0
                                text: entry.detail
                                color: LingmoUI.Theme.secondaryTextColor
                                font.pointSize: LingmoUI.Units.fontCaption
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                        }
                        Label {
                            text: files.formatBytes(entry.size)
                            color: LingmoUI.Theme.textColor
                            horizontalAlignment: Text.AlignRight
                            Layout.preferredWidth: 72
                        }
                        LingmoUI.IconButton {
                            iconName: "document-open-folder"
                            tipText: qsTr("Abrir no gerenciador de arquivos")
                            visible: entry.path.length > 0
                            onClicked: files.openInFileManager(entry.path)
                        }
                        LingmoUI.IconButton {
                            iconName: "edit-delete"
                            tipText: qsTr("Mover para a lixeira")
                            visible: entry.canTrash
                            onClicked: view.trashRequested(entry.trashPaths, entry.size, function(moved) {
                                view.markRemoved(moved)
                            })
                        }
                        LingmoUI.ActionButton {
                            visible: entry.ollamaName.length > 0
                            destructive: true
                            subtle: true
                            text: qsTr("Remover")
                            onClicked: {
                                ollamaDialog.title = qsTr("Remover o modelo %1?").arg(entry.ollamaName)
                                ollamaDialog.message = qsTr("O modelo será apagado com \"ollama rm\" (%1). Para usá-lo de novo será preciso baixá-lo outra vez.")
                                                       .arg(files.formatBytes(entry.size))
                                ollamaDialog.payload = entry.ollamaName
                                ollamaDialog.open()
                            }
                        }
                    }
                }

                Label {
                    visible: card.kids.length > 5
                    text: card.expandedAll ? qsTr("Mostrar menos") : qsTr("Mostrar todos (%1)").arg(card.kids.length)
                    color: LingmoUI.Theme.highlightColor
                    Layout.leftMargin: LingmoUI.Units.iconSizes.medium + LingmoUI.Units.largeSpacing
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            var s = Object.assign({}, view.showAll)
                            s[card.item.id] = !s[card.item.id]
                            view.showAll = s
                        }
                    }
                }
            }
        }

        LingmoUI.EmptyState {
            anchors.centerIn: parent
            width: Math.min(implicitWidth, list.width - LingmoUI.Units.largeSpacing * 2)
            visible: list.count === 0 && suggestions.loaded && !suggestions.busy
            iconName: "checkmark"
            title: qsTr("Nada que valha a pena limpar foi encontrado.")
        }
    }

    ConfirmDialog {
        id: emptyTrashDialog
        parent: Overlay.overlay
        title: qsTr("Esvaziar a lixeira?")
        message: qsTr("Todos os itens da lixeira serão apagados para sempre. Isso não pode ser desfeito.")
        confirmText: qsTr("Esvaziar lixeira")
        onConfirmed: {
            var freed = files.emptyTrash()
            messageDialog.show(qsTr("Lixeira esvaziada"), qsTr("Espaço liberado: %1").arg(files.formatBytes(freed)))
            storage.refreshMounts()
            systemInfo.refresh()
            suggestions.refresh()
        }
    }

    ConfirmDialog {
        id: ollamaDialog
        parent: Overlay.overlay
        confirmText: qsTr("Remover")
        onConfirmed: function(name) {
            view.markRemoved(["ollama:" + name])
            suggestions.removeOllamaModel(name)
        }
    }

    MessageDialog {
        id: messageDialog
        parent: Overlay.overlay
    }
}
