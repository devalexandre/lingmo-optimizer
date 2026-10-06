import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// Title row of a detail page: back button + title, extra items on the right
RowLayout {
    id: header

    property string title
    default property alias actions: actionsRow.data

    signal back()

    spacing: LingmoUI.Units.smallSpacing

    IconButton {
        iconName: "go-previous"
        tipText: qsTr("Voltar")
        onClicked: header.back()
    }

    Label {
        text: header.title
        font.pointSize: LingmoUI.Theme.fontSize * 1.5
        font.bold: true
        color: LingmoUI.Theme.textColor
        elide: Text.ElideRight
        Layout.fillWidth: true
    }

    RowLayout {
        id: actionsRow
        spacing: LingmoUI.Units.smallSpacing
    }
}
