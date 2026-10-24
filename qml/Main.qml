import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

LingmoUI.Window {
    id: rootWindow
    width: 900
    height: 620
    minimumWidth: 760
    minimumHeight: 560
    title: qsTr("Otimizador Lingmo")
    visible: true

    // Same window look as Lingmo Settings
    background.opacity: LingmoUI.Theme.darkMode ? 0.88 : 0.97
    header.height: LingmoUI.Units.windowHeaderHeight
    contentTopMargin: 0

    LingmoUI.WindowBlur {
        view: rootWindow
        geometry: Qt.rect(rootWindow.x, rootWindow.y, rootWindow.width, rootWindow.height)
        windowRadius: rootWindow.windowRadius
        enabled: true
    }

    function openPage(name) {
        if (name === "cpu")
            stack.push(cpuPage)
        else if (name === "memory")
            stack.push(memoryPage)
        else if (name === "storage")
            stack.push(storagePage)
        else if (name === "developer")
            stack.push(storagePage, { currentTab: 3 })
    }

    Component.onCompleted: {
        if (startPage.length > 0)
            openPage(startPage)
    }

    StackView {
        id: stack
        objectName: "stack"
        anchors.fill: parent
        anchors.topMargin: rootWindow.header.height
        clip: true

        initialItem: OverviewPage {
            onOpenPage: function(name) { rootWindow.openPage(name) }
            onCleanRequested: cleanupDialog.openDialog()
        }
    }

    Component {
        id: cpuPage
        CpuPage { onBack: stack.pop() }
    }
    Component {
        id: memoryPage
        MemoryPage { onBack: stack.pop() }
    }
    Component {
        id: storagePage
        StoragePage {
            onBack: stack.pop()
            onCleanRequested: function(ids) { cleanupDialog.confirmIds(ids) }
        }
    }

    // Back with Esc / Alt+Left
    Shortcut {
        sequences: [StandardKey.Back, "Esc"]
        enabled: stack.depth > 1
        onActivated: stack.pop()
    }

    MessageDialog {
        id: errorDialog
    }

    // One cleanup dialog for the whole app: choose, confirm, progress, summary
    CleanupDialog {
        id: cleanupDialog
        objectName: "cleanupDialog"
        parent: Overlay.overlay
        islandAvailable: island.activityId > 0
    }

    // Cleaning progress in the status bar's Island (com.lingmo.Island);
    // without a status bar nothing happens and the dialog shows it
    LingmoUI.IslandActivity {
        id: island
        appId: "lingmo-optimizer"
        active: cleaner.running
        kind: "generic"
        title: qsTr("Limpando")
        subtitle: cleaner.currentStep
        iconName: "edit-clear-all"
        progress: cleaner.progress
        hints: ({ detail: qsTr("%1 liberados").arg(files.formatBytes(cleaner.freed)) })
        finishMessage: qsTr("%1 liberados").arg(files.formatBytes(cleaner.freed))
        actions: [ { id: "open", label: qsTr("Abrir") } ]
        onActionInvoked: function(actionId) {
            if (actionId === "open") {
                rootWindow.raise()
                rootWindow.requestActivate()
                cleanupDialog.open()
            }
        }
    }

    Connections {
        target: files
        function onError(message) { errorDialog.show(qsTr("Não foi possível concluir"), message) }
    }
}
