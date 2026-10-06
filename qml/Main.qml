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
    header.height: 40
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
    }

    StackView {
        id: stack
        objectName: "stack"
        anchors.fill: parent
        anchors.topMargin: rootWindow.header.height
        clip: true

        initialItem: OverviewPage {
            onOpenPage: function(name) { rootWindow.openPage(name) }
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
        StoragePage { onBack: stack.pop() }
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

    Connections {
        target: files
        function onError(message) { errorDialog.show(qsTr("Não foi possível concluir"), message) }
    }
}
