import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// "Ferramentas de desenvolvimento": caches of package managers, Docker,
// Go, Node, Python, Rust, Java, JetBrains, AI models and old builds
ColumnLayout {
    id: view

    property bool active: false
    property var choice: ({})        // explicit checkbox choices by id (the rest: item default)

    signal cleanRequested(var ids)

    spacing: LingmoUI.Units.smallSpacing

    onActiveChanged: {
        if (active)
            devtools.scan()
    }

    function isSelected(item) {
        if (!item.cleanable || item.listOnly || item.busyReason.length > 0)
            return false
        return (item.id in choice) ? choice[item.id] : item.defaultSelected
    }
    function setSelected(id, on) {
        var c = Object.assign({}, choice)
        c[id] = on
        choice = c
    }
    function selectedIds() {
        var ids = []
        for (var i = 0; i < devtools.items.length; ++i) {
            if (isSelected(devtools.items[i]))
                ids.push(devtools.items[i].id)
        }
        return ids
    }
    function selectedSize() {
        var total = 0
        for (var i = 0; i < devtools.items.length; ++i) {
            var it = devtools.items[i]
            if (isSelected(it) && it.size > 0)
                total += it.size
        }
        return total
    }
    function rows() {
        var list = []
        var group = ""
        var groups = {}
        for (var g = 0; g < devtools.groups.length; ++g)
            groups[devtools.groups[g].id] = devtools.groups[g]
        for (var i = 0; i < devtools.items.length; ++i) {
            var it = devtools.items[i]
            if (it.group !== group) {
                group = it.group
                list.push({ header: true, group: groups[group] || { id: group, title: it.groupTitle, icon: "", size: 0 } })
            }
            list.push({ header: false, item: it })
        }
        return list
    }

    // ---- what is going on
    RowLayout {
        Layout.fillWidth: true
        Layout.rightMargin: LingmoUI.Units.largeSpacing
        spacing: LingmoUI.Units.smallSpacing

        LingmoUI.Spinner {
            visible: devtools.scanning
            running: visible
            size: LingmoUI.Units.iconSizes.small
        }
        Label {
            objectName: "devStatus"
            text: {
                if (devtools.scanning) {
                    var what = devtools.progressLabel.length > 0 ? devtools.progressLabel : qsTr("ferramentas instaladas")
                    return devtools.progressTotal > 0
                           ? qsTr("Medindo %1 (%2 de %3)...").arg(what).arg(devtools.progressDone + 1).arg(devtools.progressTotal)
                           : qsTr("Procurando: %1...").arg(what)
                }
                if (!devtools.loaded)
                    return ""
                var total = 0
                for (var i = 0; i < devtools.groups.length; ++i)
                    total += devtools.groups[i].size
                return qsTr("Caches e sobras de ferramentas de desenvolvimento: %1 no total. Medido às %2.")
                       .arg(files.formatBytes(total)).arg(devtools.scannedAt)
            }
            color: LingmoUI.Theme.secondaryTextColor
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        CheckBox {
            text: qsTr("Incluir pastas de projetos")
            checked: devtools.includeProjects
            enabled: !devtools.scanning
            onToggled: {
                devtools.includeProjects = checked
                devtools.rescan()
            }
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Procura node_modules e ambientes virtuais parados há mais de 90 dias e builds do CMake com mais de 30 dias.")
        }
        LingmoUI.ActionButton {
            text: devtools.scanning ? qsTr("Parar") : qsTr("Medir de novo")
            onClicked: devtools.scanning ? devtools.cancel() : devtools.rescan()
        }
    }

    ProgressBar {
        visible: devtools.scanning && devtools.progressTotal > 0
        from: 0
        to: Math.max(1, devtools.progressTotal)
        value: devtools.progressDone
        Layout.fillWidth: true
        Layout.rightMargin: LingmoUI.Units.largeSpacing
    }

    // ---- selection
    RowLayout {
        Layout.fillWidth: true
        Layout.rightMargin: LingmoUI.Units.largeSpacing
        spacing: LingmoUI.Units.smallSpacing
        visible: devtools.loaded && devtools.items.length > 0

        Label {
            readonly property int count: view.selectedIds().length
            text: count === 0 ? qsTr("Nada selecionado.")
                              : qsTr("%1 selecionado(s): %2").arg(count).arg(files.formatBytes(view.selectedSize()))
            color: LingmoUI.Theme.textColor
            Layout.fillWidth: true
        }
        LingmoUI.ActionButton {
            objectName: "devCleanSelected"
            text: qsTr("Limpar selecionados")
            primary: true
            enabled: view.selectedIds().length > 0 && !cleaner.running
            onClicked: view.cleanRequested(view.selectedIds())
        }
    }

    ListView {
        id: list
        objectName: "devList"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        spacing: LingmoUI.Units.smallSpacing
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        model: view.rows()
        cacheBuffer: 2000

        LingmoUI.WheelHandler { target: list }

        delegate: Loader {
            id: rowLoader
            required property var modelData
            width: ListView.view.width - LingmoUI.Units.largeSpacing
            sourceComponent: rowLoader.modelData.header ? headerComponent : itemComponent

            Component {
                id: headerComponent
                RowLayout {
                    spacing: LingmoUI.Units.smallSpacing
                    Label {
                        text: rowLoader.modelData.group.title
                        font.bold: true
                        font.pointSize: LingmoUI.Units.fontSubheading
                        color: LingmoUI.Theme.textColor
                        Layout.topMargin: LingmoUI.Units.smallSpacing
                        Layout.fillWidth: true
                    }
                    Label {
                        text: files.formatBytes(rowLoader.modelData.group.size)
                        color: LingmoUI.Theme.secondaryTextColor
                        Layout.topMargin: LingmoUI.Units.smallSpacing
                        Layout.rightMargin: LingmoUI.Units.largeSpacing
                    }
                }
            }
            Component {
                id: itemComponent
                DevItemRow {
                    item: rowLoader.modelData.item
                    checked: view.isSelected(rowLoader.modelData.item)
                    busy: cleaner.running
                    onToggled: function(on) { view.setSelected(rowLoader.modelData.item.id, on) }
                    onCleanClicked: view.cleanRequested([rowLoader.modelData.item.id])
                }
            }
        }

        LingmoUI.EmptyState {
            anchors.centerIn: parent
            width: Math.min(implicitWidth, list.width - LingmoUI.Units.largeSpacing * 2)
            visible: list.count === 0 && devtools.loaded && !devtools.scanning
            iconName: "checkmark"
            title: qsTr("Nenhum cache de ferramenta de desenvolvimento encontrado.")
        }
    }
}
