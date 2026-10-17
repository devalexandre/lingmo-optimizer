import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// Yes/no question with a destructive (red) confirm button by default
Dialog {
    id: dialog

    property string message
    property string details          // paths, shown in a smaller font
    property string confirmText: qsTr("Confirmar")
    property bool destructive: true
    property var payload             // whatever the caller needs back

    signal confirmed(var payload)

    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(520, parent ? parent.width - LingmoUI.Units.largeSpacing * 4 : 520)
    padding: LingmoUI.Units.largeSpacing * 1.5

    contentItem: ColumnLayout {
        spacing: LingmoUI.Units.largeSpacing

        Label {
            text: dialog.message
            color: LingmoUI.Theme.textColor
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Rectangle {
            visible: dialog.details.length > 0
            Layout.fillWidth: true
            implicitHeight: Math.min(detailsLabel.implicitHeight, 160) + LingmoUI.Units.largeSpacing
            radius: LingmoUI.Theme.smallRadius
            color: LingmoUI.Theme.alternateBackgroundColor
            clip: true

            Label {
                id: detailsLabel
                anchors.fill: parent
                anchors.margins: LingmoUI.Units.smallSpacing
                text: dialog.details
                color: LingmoUI.Theme.secondaryTextColor
                font.pointSize: LingmoUI.Units.fontCaption
                wrapMode: Text.WrapAnywhere
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: LingmoUI.Units.smallSpacing

            Item { Layout.fillWidth: true }

            LingmoUI.ActionButton {
                text: qsTr("Cancelar")
                onClicked: dialog.reject()
            }

            LingmoUI.ActionButton {
                text: dialog.confirmText
                destructive: dialog.destructive
                primary: !dialog.destructive
                onClicked: {
                    dialog.accept()
                    dialog.confirmed(dialog.payload)
                }
            }
        }
    }
}
