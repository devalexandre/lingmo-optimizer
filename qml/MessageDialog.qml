import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// Simple information dialog with an OK button
Dialog {
    id: dialog

    property string message

    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(460, parent ? parent.width - LingmoUI.Units.largeSpacing * 4 : 460)
    padding: LingmoUI.Units.largeSpacing * 1.5

    function show(title, text) {
        dialog.title = title
        dialog.message = text
        open()
    }

    contentItem: ColumnLayout {
        spacing: LingmoUI.Units.largeSpacing

        Label {
            text: dialog.message
            color: LingmoUI.Theme.textColor
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        ActionButton {
            text: qsTr("OK")
            primary: true
            Layout.alignment: Qt.AlignRight
            onClicked: dialog.close()
        }
    }
}
