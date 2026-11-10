import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// The pen drawn from the side: the tip, the two barrel buttons (1 and 2) and the
// eraser end light up while they are in use.
Item {
    id: root
    implicitWidth: 240
    implicitHeight: 74

    readonly property color lit: theme.accent

    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var cy = height / 2 + 8
            function body(x, w, h, r) {
                ctx.beginPath()
                ctx.moveTo(x + r, cy - h / 2)
                ctx.lineTo(x + w - r, cy - h / 2)
                ctx.arcTo(x + w, cy - h / 2, x + w, cy - h / 2 + r, r)
                ctx.lineTo(x + w, cy + h / 2 - r)
                ctx.arcTo(x + w, cy + h / 2, x + w - r, cy + h / 2, r)
                ctx.lineTo(x + r, cy + h / 2)
                ctx.arcTo(x, cy + h / 2, x, cy + h / 2 - r, r)
                ctx.lineTo(x, cy - h / 2 + r)
                ctx.arcTo(x, cy - h / 2, x + r, cy - h / 2, r)
                ctx.closePath()
            }
            ctx.lineWidth = 1.5
            ctx.strokeStyle = Colors.textDim
            // Barrel.
            ctx.fillStyle = Colors.surface
            body(40, 160, 22, 5)
            ctx.fill(); ctx.stroke()
            // Tip (the cone), lit while touching the pad.
            ctx.fillStyle = backend.penDown ? root.lit : Colors.border
            ctx.beginPath()
            ctx.moveTo(4, cy)
            ctx.lineTo(40, cy - 9)
            ctx.lineTo(40, cy + 9)
            ctx.closePath()
            ctx.fill(); ctx.stroke()
            // Eraser end.
            ctx.fillStyle = backend.eraser ? root.lit : Colors.border
            body(200, 30, 22, 7)
            ctx.fill(); ctx.stroke()
            // The two barrel buttons.
            ctx.fillStyle = backend.barrel1 ? root.lit : Colors.background
            body(78, 28, 10, 4)
            ctx.fill(); ctx.stroke()
            ctx.fillStyle = backend.barrel2 ? root.lit : Colors.background
            body(114, 28, 10, 4)
            ctx.fill(); ctx.stroke()
        }
        Connections {
            target: backend
            function onPenChanged() { canvas.requestPaint() }
        }
        Connections {
            target: theme
            function onChanged() { canvas.requestPaint() }
        }
    }

    Label {
        x: 78; y: 0; width: 28
        horizontalAlignment: Text.AlignHCenter
        text: "1"
        font.bold: true
        color: backend.barrel1 ? root.lit : Colors.textMuted
    }
    Label {
        x: 114; y: 0; width: 28
        horizontalAlignment: Text.AlignHCenter
        text: "2"
        font.bold: true
        color: backend.barrel2 ? root.lit : Colors.textMuted
    }
    Label {
        x: 4; y: height - 16
        text: qsTr("Tip")
        font.pixelSize: 11
        color: backend.penDown ? root.lit : Colors.textDim
    }
    Label {
        x: 198; y: height - 16
        text: qsTr("Eraser")
        font.pixelSize: 11
        color: backend.eraser ? root.lit : Colors.textDim
    }
}
