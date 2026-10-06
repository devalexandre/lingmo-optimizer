import QtQuick
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// Small spinning ring. (LingmoUI's BusyIndicator currently fails to load:
// the compat one misses its image and the LingmoStyle one has a JS error.)
Item {
    id: spinner

    property bool running: visible

    implicitWidth: LingmoUI.Units.iconSizes.small
    implicitHeight: implicitWidth

    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var w = Math.max(2, width / 8)
            ctx.lineWidth = w
            ctx.lineCap = "round"
            ctx.strokeStyle = LingmoUI.Theme.highlightColor
            ctx.beginPath()
            ctx.arc(width / 2, height / 2, width / 2 - w, 0, Math.PI * 1.5, false)
            ctx.stroke()
        }
        Connections {
            target: LingmoUI.Theme
            function onHighlightColorChanged() { canvas.requestPaint() }
        }

        RotationAnimator on rotation {
            from: 0
            to: 360
            duration: 900
            loops: Animation.Infinite
            running: spinner.running
        }
    }
}
