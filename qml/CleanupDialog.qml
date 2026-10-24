import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// "Otimizar e Limpar": choose the steps and the developer caches (with how
// much each frees), confirm with the total, run them, then show what each
// one actually freed
Dialog {
    id: dialog

    property var selected: ({})          // cleaner steps by id
    property var devChoice: ({})         // developer items by id (explicit choices)
    property var pendingIds: []          // what the confirmation is about ("dev:<id>" or step ids)
    property bool cameFromChoose: false
    property var results: []
    property real totalFreed: 0
    property string stage: "choose"     // choose | confirm | running | done
    // The status bar's Island shows the progress: the dialog may go away
    property bool islandAvailable: false

    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(680, parent ? parent.width - LingmoUI.Units.largeSpacing * 4 : 680)
    padding: LingmoUI.Units.largeSpacing * 1.5
    closePolicy: stage === "running" && !islandAvailable ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
    title: stage === "done" ? qsTr("Limpeza concluída")
         : stage === "confirm" ? qsTr("Confirmar limpeza")
         : stage === "running" ? qsTr("Limpando")
         : qsTr("Otimizar e Limpar")

    readonly property real maxListHeight: Math.max(160, (parent ? parent.height : 600) - 260)

    function openDialog() {
        var sel = {}
        for (var i = 0; i < cleaner.steps.length; ++i)
            sel[cleaner.steps[i].id] = true
        selected = sel
        stage = cleaner.running ? "running" : "choose"
        cleaner.estimate()
        devtools.scan()
        open()
    }

    // Straight to the confirmation (from the storage page's "Limpar")
    function confirmIds(devIds) {
        var ids = []
        for (var i = 0; i < devIds.length; ++i)
            ids.push("dev:" + devIds[i])
        pendingIds = ids
        cameFromChoose = false
        stage = cleaner.running ? "running" : "confirm"
        open()
    }

    function devSelected(item) {
        if (!item.cleanable || item.listOnly || item.busyReason.length > 0)
            return false
        return (item.id in devChoice) ? devChoice[item.id] : item.defaultSelected
    }

    function chosenIds() {
        var ids = []
        for (var i = 0; i < cleaner.steps.length; ++i) {
            if (selected[cleaner.steps[i].id])
                ids.push(cleaner.steps[i].id)
        }
        for (var j = 0; j < devtools.items.length; ++j) {
            if (devSelected(devtools.items[j]))
                ids.push("dev:" + devtools.items[j].id)
        }
        return ids
    }

    // [{id, title, size, upTo, path, reinstall, root, command}]
    function describe(ids) {
        var list = []
        for (var i = 0; i < ids.length; ++i) {
            var id = ids[i]
            if (id.indexOf("dev:") === 0) {
                var it = devtools.item(id.substring(4))
                if (it.id === undefined)
                    continue
                list.push({ id: id, title: it.title, size: it.size, upTo: it.upTo, path: it.path, reinstall: it.reinstall,
                            root: it.root, command: it.command, comesBack: it.comesBack })
            } else {
                for (var s = 0; s < cleaner.steps.length; ++s) {
                    var step = cleaner.steps[s]
                    if (step.id === id)
                        list.push({ id: id, title: step.title, size: step.size, upTo: step.upTo, path: "", reinstall: false,
                                    root: step.root, command: "", comesBack: step.description })
                }
            }
        }
        return list
    }
    function totalOf(list) {
        var total = 0
        for (var i = 0; i < list.length; ++i)
            total += Math.max(0, list[i].size)
        return total
    }
    function anyOf(list, key) {
        for (var i = 0; i < list.length; ++i) {
            if (list[i][key])
                return true
        }
        return false
    }

    readonly property var pending: stage === "confirm" ? describe(pendingIds) : []

    Connections {
        target: cleaner
        function onRunningChanged() {
            if (cleaner.running) {
                dialog.stage = "running"
            }
        }
        function onFinished(results, totalFreed) {
            dialog.results = results
            dialog.totalFreed = totalFreed
            dialog.stage = "done"
            systemInfo.refresh()
            if (!dialog.opened)
                dialog.open()
        }
    }

    contentItem: ColumnLayout {
        spacing: LingmoUI.Units.largeSpacing

        // ================================================================ choose
        Label {
            visible: dialog.stage === "choose"
            text: qsTr("Escolha o que limpar. Nada da sua pasta pessoal além de caches é apagado sem você marcar.")
            color: LingmoUI.Theme.secondaryTextColor
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        ScrollView {
            id: chooseScroll
            visible: dialog.stage === "choose"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(chooseColumn.implicitHeight, dialog.maxListHeight)
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                id: chooseColumn
                width: chooseScroll.availableWidth
                spacing: LingmoUI.Units.smallSpacing

                Repeater {
                    model: dialog.stage === "choose" ? cleaner.steps : []

                    delegate: LingmoUI.Card {
                        id: stepCard
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: stepRow.implicitHeight + LingmoUI.Units.largeSpacing * 1.5
                        hoverEnabled: true

                        RowLayout {
                            id: stepRow
                            anchors.fill: parent
                            anchors.leftMargin: LingmoUI.Units.smallSpacing
                            anchors.rightMargin: LingmoUI.Units.largeSpacing
                            spacing: LingmoUI.Units.smallSpacing

                            CheckBox {
                                checked: !!dialog.selected[stepCard.modelData.id]
                                onToggled: {
                                    var sel = Object.assign({}, dialog.selected)
                                    sel[stepCard.modelData.id] = checked
                                    dialog.selected = sel
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Label {
                                    text: stepCard.modelData.title
                                    font.bold: true
                                    color: LingmoUI.Theme.textColor
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: stepCard.modelData.description
                                    color: LingmoUI.Theme.secondaryTextColor
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                            }
                            Label {
                                text: stepCard.modelData.size < 0 ? (cleaner.estimating ? "…" : "")
                                      : (stepCard.modelData.upTo ? qsTr("até %1") : "%1").arg(files.formatBytes(stepCard.modelData.size))
                                color: LingmoUI.Theme.textColor
                                font.pointSize: LingmoUI.Units.fontSubheading
                                font.bold: true
                            }
                        }
                    }
                }

                // ---- developer tools
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: LingmoUI.Units.smallSpacing
                    spacing: LingmoUI.Units.smallSpacing

                    Label {
                        text: qsTr("Ferramentas de desenvolvimento")
                        font.bold: true
                        font.pointSize: LingmoUI.Units.fontSubheading
                        color: LingmoUI.Theme.textColor
                        Layout.fillWidth: true
                    }
                    LingmoUI.Spinner {
                        visible: devtools.scanning
                        running: visible
                        size: LingmoUI.Units.iconSizes.small
                    }
                    Label {
                        visible: devtools.scanning
                        text: devtools.progressTotal > 0
                              ? qsTr("Medindo %1 de %2").arg(devtools.progressDone + 1).arg(devtools.progressTotal)
                              : qsTr("Procurando...")
                        color: LingmoUI.Theme.secondaryTextColor
                        font.pointSize: LingmoUI.Units.fontCaption
                    }
                }
                Label {
                    visible: devtools.loaded && devtools.items.length === 0
                    text: qsTr("Nenhum cache de ferramenta de desenvolvimento encontrado.")
                    color: LingmoUI.Theme.secondaryTextColor
                    Layout.fillWidth: true
                }

                Repeater {
                    // Only what can be cleaned here; list-only items live in Armazenamento
                    model: {
                        if (dialog.stage !== "choose")
                            return []
                        var list = []
                        for (var i = 0; i < devtools.items.length; ++i) {
                            var it = devtools.items[i]
                            if (!it.listOnly && !it.current)
                                list.push(it)
                        }
                        return list
                    }
                    delegate: DevItemRow {
                        required property var modelData
                        Layout.fillWidth: true
                        item: modelData
                        checked: dialog.devSelected(modelData)
                        maxChildren: 3
                        onToggled: function(on) {
                            var c = Object.assign({}, dialog.devChoice)
                            c[modelData.id] = on
                            dialog.devChoice = c
                        }
                        onCleanClicked: {
                            dialog.pendingIds = ["dev:" + modelData.id]
                            dialog.cameFromChoose = true
                            dialog.stage = "confirm"
                        }
                    }
                }
            }
        }

        // ================================================================ confirm
        Label {
            visible: dialog.stage === "confirm"
            objectName: "confirmTotal"
            text: dialog.pending.length === 1
                  ? qsTr("Será apagado %1: %2.").arg(dialog.pending[0].title)
                        .arg((dialog.pending[0].upTo ? qsTr("até %1") : "%1").arg(files.formatBytes(dialog.totalOf(dialog.pending))))
                  : qsTr("Serão limpos %1 itens, liberando %2%3.").arg(dialog.pending.length)
                        .arg(dialog.anyOf(dialog.pending, "upTo") ? qsTr("até ") : qsTr("cerca de "))
                        .arg(files.formatBytes(dialog.totalOf(dialog.pending)))
            font.pointSize: LingmoUI.Units.fontSubheading
            font.bold: true
            color: LingmoUI.Theme.textColor
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        ScrollView {
            id: confirmScroll
            visible: dialog.stage === "confirm"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(confirmColumn.implicitHeight, dialog.maxListHeight - 80)
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                id: confirmColumn
                width: confirmScroll.availableWidth
                spacing: LingmoUI.Units.smallSpacing

                Repeater {
                    model: dialog.pending
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: LingmoUI.Units.smallSpacing
                        ColumnLayout {
                            spacing: 0
                            Layout.fillWidth: true
                            RowLayout {
                                spacing: LingmoUI.Units.smallSpacing
                                Label {
                                    text: modelData.title
                                    color: LingmoUI.Theme.textColor
                                }
                                LingmoUI.Badge {
                                    visible: modelData.reinstall
                                    text: qsTr("pode precisar reinstalar")
                                    tint: LingmoUI.Theme.orangeColor
                                }
                            }
                            Label {
                                visible: text.length > 0
                                text: modelData.command.length > 0 ? modelData.command
                                      : (modelData.path.indexOf(files.homePath + "/") === 0 ? "~" + modelData.path.substring(files.homePath.length) : modelData.path)
                                color: LingmoUI.Theme.secondaryTextColor
                                font.pointSize: LingmoUI.Units.fontCaption
                                font.family: "Monospace"
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                        }
                        Label {
                            text: modelData.size < 0 ? "" : (modelData.upTo ? qsTr("até %1") : "%1").arg(files.formatBytes(modelData.size))
                            color: LingmoUI.Theme.textColor
                            font.bold: true
                        }
                    }
                }
            }
        }

        Label {
            visible: dialog.stage === "confirm" && dialog.anyOf(dialog.pending, "reinstall")
            text: qsTr("Itens marcados com \"pode precisar reinstalar\" não voltam sozinhos: versões, ambientes e dependências de "
                       + "projetos precisarão ser instalados de novo para serem usados.")
            color: LingmoUI.Theme.warningColor
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label {
            visible: (dialog.stage === "confirm" && dialog.anyOf(dialog.pending, "root"))
            text: qsTr("A senha de administrador será pedida uma vez para limpar os itens do sistema.")
            color: LingmoUI.Theme.secondaryTextColor
            font.pointSize: LingmoUI.Units.fontCaption
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label {
            visible: dialog.stage === "confirm"
            text: qsTr("O que estiver rodando agora (contêineres, compilações, a versão em uso) não é tocado.")
            color: LingmoUI.Theme.secondaryTextColor
            font.pointSize: LingmoUI.Units.fontCaption
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        // ================================================================ running
        ColumnLayout {
            visible: dialog.stage === "running"
            spacing: LingmoUI.Units.smallSpacing
            Layout.fillWidth: true

            RowLayout {
                spacing: LingmoUI.Units.largeSpacing
                Layout.fillWidth: true
                LingmoUI.Spinner {
                    running: dialog.stage === "running"
                    size: LingmoUI.Units.iconSizes.medium
                }
                Label {
                    text: cleaner.currentStep.length > 0 ? qsTr("Limpando: %1").arg(cleaner.currentStep) : qsTr("Limpando...")
                    color: LingmoUI.Theme.textColor
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
            ProgressBar {
                from: 0
                to: 1
                value: cleaner.progress
                Layout.fillWidth: true
            }
            Label {
                visible: dialog.islandAvailable
                text: qsTr("O progresso também aparece na Ilha da barra de status: você pode fechar esta janela.")
                color: LingmoUI.Theme.secondaryTextColor
                font.pointSize: LingmoUI.Units.fontCaption
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        // ================================================================ done
        Label {
            visible: dialog.stage === "done"
            objectName: "freedTotal"
            text: qsTr("Espaço liberado: %1").arg(files.formatBytes(dialog.totalFreed))
            font.pointSize: LingmoUI.Units.fontHeading
            font.bold: true
            color: LingmoUI.Theme.textColor
        }

        ScrollView {
            id: doneScroll
            visible: dialog.stage === "done"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(doneColumn.implicitHeight, dialog.maxListHeight)
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                id: doneColumn
                width: doneScroll.availableWidth
                spacing: LingmoUI.Units.smallSpacing

                Repeater {
                    model: dialog.stage === "done" ? dialog.results : []

                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: LingmoUI.Units.smallSpacing

                        LingmoUI.IconItem {
                            width: LingmoUI.Units.iconSizes.small
                            height: width
                            source: modelData.ok ? "emblem-ok-symbolic" : "dialog-warning"
                            Layout.preferredWidth: width
                            Layout.preferredHeight: height
                        }
                        ColumnLayout {
                            spacing: 0
                            Layout.fillWidth: true
                            Label {
                                text: modelData.title
                                color: LingmoUI.Theme.textColor
                                Layout.fillWidth: true
                            }
                            Label {
                                visible: modelData.message.length > 0
                                text: modelData.message
                                color: modelData.ok ? LingmoUI.Theme.secondaryTextColor : LingmoUI.Theme.redColor
                                font.pointSize: LingmoUI.Units.fontCaption
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }
                        Label {
                            text: modelData.ok ? files.formatBytes(modelData.freed) : qsTr("não executado")
                            color: modelData.ok ? LingmoUI.Theme.textColor : LingmoUI.Theme.secondaryTextColor
                        }
                    }
                }
            }
        }

        // ================================================================ buttons
        RowLayout {
            Layout.fillWidth: true
            spacing: LingmoUI.Units.smallSpacing

            Label {
                visible: dialog.stage === "choose"
                readonly property var chosen: dialog.stage === "choose" ? dialog.describe(dialog.chosenIds()) : []
                text: chosen.length > 0 ? qsTr("Selecionado: %1").arg(files.formatBytes(dialog.totalOf(chosen))) : ""
                color: LingmoUI.Theme.textColor
                font.bold: true
            }
            Item { Layout.fillWidth: true }

            LingmoUI.ActionButton {
                visible: dialog.stage === "choose" || dialog.stage === "confirm"
                text: dialog.stage === "confirm" && dialog.cameFromChoose ? qsTr("Voltar") : qsTr("Cancelar")
                onClicked: {
                    if (dialog.stage === "confirm" && dialog.cameFromChoose)
                        dialog.stage = "choose"
                    else
                        dialog.close()
                }
            }
            LingmoUI.ActionButton {
                visible: dialog.stage === "choose"
                objectName: "startCleanupButton"
                text: qsTr("Limpar...")
                primary: true
                enabled: dialog.chosenIds().length > 0 && !cleaner.running
                onClicked: {
                    dialog.pendingIds = dialog.chosenIds()
                    dialog.cameFromChoose = true
                    dialog.stage = "confirm"
                }
            }
            LingmoUI.ActionButton {
                visible: dialog.stage === "confirm"
                objectName: "confirmCleanupButton"
                text: qsTr("Limpar %1").arg(files.formatBytes(dialog.totalOf(dialog.pending)))
                destructive: true
                enabled: dialog.pending.length > 0 && !cleaner.running
                onClicked: {
                    dialog.stage = "running"
                    cleaner.run(dialog.pendingIds)
                }
            }
            LingmoUI.ActionButton {
                visible: dialog.stage === "running" && dialog.islandAvailable
                text: qsTr("Continuar em segundo plano")
                onClicked: dialog.close()
            }
            LingmoUI.ActionButton {
                visible: dialog.stage === "done"
                text: qsTr("Fechar")
                primary: true
                onClicked: dialog.close()
            }
        }
    }
}
