import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// "Otimizar e Limpar": choose the steps (with how much each frees), run them,
// then show what each one actually freed
Dialog {
    id: dialog

    property var selected: ({})
    property var results: []
    property real totalFreed: 0
    property string stage: "choose"     // choose | running | done

    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(560, parent ? parent.width - LingmoUI.Units.largeSpacing * 4 : 560)
    padding: LingmoUI.Units.largeSpacing * 1.5
    closePolicy: stage === "running" ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
    title: stage === "done" ? qsTr("Limpeza concluída") : qsTr("Otimizar e Limpar")

    function openDialog() {
        var sel = {}
        for (var i = 0; i < cleaner.steps.length; ++i)
            sel[cleaner.steps[i].id] = true
        selected = sel
        stage = cleaner.running ? "running" : "choose"
        cleaner.estimate()
        open()
    }

    function chosenIds() {
        var ids = []
        for (var i = 0; i < cleaner.steps.length; ++i) {
            if (selected[cleaner.steps[i].id])
                ids.push(cleaner.steps[i].id)
        }
        return ids
    }

    function needsRoot() {
        for (var i = 0; i < cleaner.steps.length; ++i) {
            if (selected[cleaner.steps[i].id] && cleaner.steps[i].root)
                return true
        }
        return false
    }

    Connections {
        target: cleaner
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

        // ---- choose
        Label {
            visible: dialog.stage === "choose"
            text: qsTr("Escolha o que limpar. Nada da sua pasta pessoal além de caches é apagado.")
            color: LingmoUI.Theme.secondaryTextColor
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

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
                    anchors.leftMargin: LingmoUI.Units.largeSpacing
                    anchors.rightMargin: LingmoUI.Units.largeSpacing * 1.5
                    spacing: LingmoUI.Units.smallSpacing

                    CheckBox {
                        id: check
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
                            color: LingmoUI.Theme.textColor
                            Layout.fillWidth: true
                        }
                        Label {
                            text: stepCard.modelData.description
                            color: LingmoUI.Theme.secondaryTextColor
                            font.pointSize: LingmoUI.Units.fontCaption
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    Label {
                        text: stepCard.modelData.size < 0 ? (cleaner.estimating ? "…" : "")
                              : (stepCard.modelData.upTo ? qsTr("até %1") : "%1").arg(files.formatBytes(stepCard.modelData.size))
                        color: LingmoUI.Theme.textColor
                        font.bold: true
                    }
                }
            }
        }

        Label {
            visible: dialog.stage === "choose" && dialog.needsRoot()
            text: qsTr("A senha de administrador será pedida uma vez para limpar os itens do sistema.")
            color: LingmoUI.Theme.secondaryTextColor
            font.pointSize: LingmoUI.Units.fontCaption
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        // ---- running
        RowLayout {
            visible: dialog.stage === "running"
            spacing: LingmoUI.Units.largeSpacing
            Layout.fillWidth: true

            LingmoUI.Spinner {
                running: dialog.stage === "running"
                size: LingmoUI.Units.iconSizes.medium
            }
            Label {
                text: cleaner.currentStep.length > 0 ? qsTr("Limpando: %1").arg(cleaner.currentStep) : qsTr("Limpando...")
                color: LingmoUI.Theme.textColor
                Layout.fillWidth: true
            }
        }

        // ---- done
        Label {
            visible: dialog.stage === "done"
            text: qsTr("Espaço liberado: %1").arg(files.formatBytes(dialog.totalFreed))
            font.pointSize: LingmoUI.Units.fontHeading
            font.bold: true
            color: LingmoUI.Theme.textColor
        }

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

        RowLayout {
            Layout.fillWidth: true
            spacing: LingmoUI.Units.smallSpacing

            Item { Layout.fillWidth: true }

            LingmoUI.ActionButton {
                visible: dialog.stage === "choose"
                text: qsTr("Cancelar")
                onClicked: dialog.close()
            }
            LingmoUI.ActionButton {
                visible: dialog.stage === "choose"
                objectName: "startCleanupButton"
                text: qsTr("Limpar")
                primary: true
                enabled: dialog.chosenIds().length > 0
                onClicked: {
                    dialog.stage = "running"
                    cleaner.run(dialog.chosenIds())
                }
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
