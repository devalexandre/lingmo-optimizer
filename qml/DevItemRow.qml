import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// One item of "Ferramentas de desenvolvimento": what it is, where it is,
// whether it comes back by itself, its size, a checkbox and "Limpar".
LingmoUI.Card {
    id: row

    property var item: ({})
    property bool checked: false
    property bool busy: false            // a cleanup is running
    property int maxChildren: 4

    readonly property bool selectable: item.cleanable && !item.listOnly && item.busyReason.length === 0
    readonly property bool showAllChildren: childrenToggle.expanded

    signal toggled(bool on)
    signal cleanClicked()

    implicitHeight: content.implicitHeight + LingmoUI.Units.largeSpacing * 1.5
    hoverEnabled: true

    // "~/.cache/yay" instead of the full home path
    function shortPath(path) {
        var home = files.homePath
        if (path === home)
            return "~"
        return path.indexOf(home + "/") === 0 ? "~" + path.substring(home.length) : path
    }

    function sizeText() {
        if (item.size < 0)
            return item.listOnly ? "" : "…"
        var text = files.formatBytes(item.size)
        if (item.upTo)
            return qsTr("até %1").arg(text)
        if (item.partial)
            return qsTr("mais de %1").arg(text)
        return text
    }

    RowLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: LingmoUI.Units.largeSpacing * 0.75
        anchors.leftMargin: LingmoUI.Units.smallSpacing
        anchors.rightMargin: LingmoUI.Units.largeSpacing
        spacing: LingmoUI.Units.smallSpacing

        // Keeps the column aligned when there is nothing to select
        Item {
            Layout.preferredWidth: check.implicitWidth
            Layout.preferredHeight: check.implicitHeight
            Layout.alignment: Qt.AlignTop

            CheckBox {
                id: check
                anchors.centerIn: parent
                visible: !row.item.listOnly && !row.item.current
                enabled: row.selectable && !row.busy
                checked: row.checked && row.selectable
                onToggled: row.toggled(checked)
            }
            LingmoUI.IconItem {
                anchors.centerIn: parent
                visible: row.item.listOnly || row.item.current
                width: LingmoUI.Units.iconSizes.small
                height: width
                source: row.item.current ? "emblem-ok-symbolic" : "view-list-details"
                opacity: 0.6
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Flow {
                Layout.fillWidth: true
                spacing: LingmoUI.Units.smallSpacing

                Label {
                    text: row.item.title
                    font.bold: true
                    color: LingmoUI.Theme.textColor
                    elide: Text.ElideMiddle
                    width: Math.min(implicitWidth, content.width * 0.6)
                }
                LingmoUI.Badge {
                    visible: row.item.reinstall && !row.item.listOnly && !row.item.current
                    text: qsTr("pode precisar reinstalar")
                    tint: LingmoUI.Theme.orangeColor
                }
                LingmoUI.Badge {
                    visible: row.item.current
                    text: qsTr("em uso")
                    tint: LingmoUI.Theme.greenColor
                }
                LingmoUI.Badge {
                    visible: row.item.listOnly
                    text: qsTr("só listado")
                    subtle: true
                }
                LingmoUI.Badge {
                    visible: row.item.root
                    text: qsTr("pede senha")
                    subtle: true
                }
            }

            Label {
                text: row.item.description
                color: LingmoUI.Theme.secondaryTextColor
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                visible: text.length > 0
                text: row.item.comesBack
                color: LingmoUI.Theme.textColor
                font.pointSize: LingmoUI.Units.fontCaption
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                visible: row.item.warning.length > 0
                text: row.item.warning
                color: LingmoUI.Theme.warningColor
                font.pointSize: LingmoUI.Units.fontCaption
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                visible: row.item.busyReason.length > 0 || row.item.blockedReason.length > 0
                text: row.item.busyReason.length > 0
                      ? qsTr("Em uso agora por \"%1\": feche o programa para limpar.").arg(row.item.busyReason)
                      : qsTr("Não será limpo: %1.").arg(row.item.blockedReason)
                color: LingmoUI.Theme.errorColor
                font.pointSize: LingmoUI.Units.fontCaption
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                visible: text.length > 0
                text: {
                    var parts = []
                    if (row.item.path.length > 0)
                        parts.push(row.item.paths.length > 1 ? qsTr("%1 e mais %2").arg(row.shortPath(row.item.path)).arg(row.item.paths.length - 1)
                                                              : row.shortPath(row.item.path))
                    if (row.item.command.length > 0)
                        parts.push(qsTr("comando: %1").arg(row.item.command))
                    return parts.join("  ·  ")
                }
                color: LingmoUI.Theme.secondaryTextColor
                font.pointSize: LingmoUI.Units.fontCaption
                font.family: "Monospace"
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }

            // Models, volumes...
            Repeater {
                model: row.item.children.length === 0 ? []
                       : (row.showAllChildren ? row.item.children : row.item.children.slice(0, row.maxChildren))
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.leftMargin: LingmoUI.Units.smallSpacing
                    spacing: LingmoUI.Units.smallSpacing
                    Label {
                        text: "• " + modelData.name
                        color: LingmoUI.Theme.textColor
                        font.pointSize: LingmoUI.Units.fontCaption
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    Label {
                        visible: modelData.detail.length > 0
                        text: modelData.detail
                        color: LingmoUI.Theme.secondaryTextColor
                        font.pointSize: LingmoUI.Units.fontCaption
                    }
                    Label {
                        visible: modelData.size >= 0
                        text: files.formatBytes(modelData.size)
                        color: LingmoUI.Theme.secondaryTextColor
                        font.pointSize: LingmoUI.Units.fontCaption
                    }
                }
            }
            Label {
                id: childrenToggle
                property bool expanded: false
                visible: row.item.children.length > row.maxChildren
                text: expanded ? qsTr("Mostrar menos") : qsTr("Mostrar todos (%1)").arg(row.item.children.length)
                color: LingmoUI.Theme.highlightColor
                font.pointSize: LingmoUI.Units.fontCaption
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: childrenToggle.expanded = !childrenToggle.expanded
                }
            }
        }

        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            spacing: LingmoUI.Units.smallSpacing

            Label {
                text: row.sizeText()
                font.pointSize: LingmoUI.Units.fontSubheading
                font.bold: true
                color: LingmoUI.Theme.textColor
                Layout.alignment: Qt.AlignRight
            }
            LingmoUI.ActionButton {
                visible: !row.item.listOnly && !row.item.current
                objectName: "devCleanButton"
                text: qsTr("Limpar")
                destructive: row.item.reinstall
                subtle: true
                enabled: row.selectable && !row.busy
                Layout.alignment: Qt.AlignRight
                onClicked: row.cleanClicked()
            }
            LingmoUI.ActionButton {
                visible: row.item.link.length > 0
                text: qsTr("Configurações")
                subtle: true
                Layout.alignment: Qt.AlignRight
                onClicked: devtools.openLink(row.item.link)
            }
            LingmoUI.ActionButton {
                visible: row.item.listOnly && row.item.link.length === 0 && row.item.path.length > 0
                         && row.item.path.charAt(0) === "/" && row.item.group !== "docker"
                text: qsTr("Abrir pasta")
                subtle: true
                Layout.alignment: Qt.AlignRight
                onClicked: files.openInFileManager(row.item.path)
            }
        }
    }
}
