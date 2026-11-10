import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// How hard the pen has to press, and what its barrel button does.
Item {
    RowLayout {
        anchors.fill: parent
        spacing: 24

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Rectangle {
                id: plot
                width: Math.min(parent.width, parent.height)
                height: width
                anchors.centerIn: parent
                color: Palette.surface
                radius: theme.radius

                Canvas {
                    id: curveCanvas
                    anchors.fill: parent
                    anchors.margins: 24
                    function toX(v) { return v * width }
                    function toY(v) { return (1 - v) * height }

                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        ctx.strokeStyle = Palette.border
                        ctx.lineWidth = 1
                        for (var g = 0; g <= 4; ++g) {
                            ctx.beginPath(); ctx.moveTo(toX(g / 4), 0); ctx.lineTo(toX(g / 4), height); ctx.stroke()
                            ctx.beginPath(); ctx.moveTo(0, toY(g / 4)); ctx.lineTo(width, toY(g / 4)); ctx.stroke()
                        }
                        // Handle stems.
                        ctx.strokeStyle = Palette.disabled
                        ctx.beginPath(); ctx.moveTo(toX(0), toY(0)); ctx.lineTo(toX(backend.curveX1), toY(backend.curveY1)); ctx.stroke()
                        ctx.beginPath(); ctx.moveTo(toX(1), toY(1)); ctx.lineTo(toX(backend.curveX2), toY(backend.curveY2)); ctx.stroke()
                        // The curve itself, sampled through the same function the daemon uses.
                        ctx.strokeStyle = theme.accent
                        ctx.lineWidth = 3
                        ctx.beginPath()
                        for (var i = 0; i <= 64; ++i) {
                            var p = i / 64
                            if (i === 0) ctx.moveTo(toX(p), toY(backend.curveAt(p)))
                            else ctx.lineTo(toX(p), toY(backend.curveAt(p)))
                        }
                        ctx.stroke()
                    }

                    Connections {
                        target: backend
                        function onConfigChanged() { curveCanvas.requestPaint() }
                    }
                    Connections {
                        target: theme
                        function onChanged() { curveCanvas.requestPaint() }
                    }

                    Repeater {
                        model: 2
                        Rectangle {
                            readonly property real cx: index === 0 ? backend.curveX1 : backend.curveX2
                            readonly property real cy: index === 0 ? backend.curveY1 : backend.curveY2
                            width: 18
                            height: 18
                            radius: 9
                            x: curveCanvas.toX(cx) - 9
                            y: curveCanvas.toY(cy) - 9
                            color: theme.accent
                            border.width: 2
                            border.color: Palette.background

                            // Keyboard: arrows nudge the handle.
                            activeFocusOnTab: true
                            Accessible.role: Accessible.Slider
                            Accessible.name: index === 0 ? qsTr("Curve handle 1") : qsTr("Curve handle 2")
                            Keys.onPressed: (e) => {
                                var dx = e.key === Qt.Key_Left ? -1 : e.key === Qt.Key_Right ? 1 : 0
                                var dy = e.key === Qt.Key_Down ? -1 : e.key === Qt.Key_Up ? 1 : 0
                                if (dx === 0 && dy === 0) return
                                var nx = Math.max(0, Math.min(1, cx + dx * 0.02))
                                var ny = Math.max(0, Math.min(1, cy + dy * 0.02))
                                if (index === 0) backend.setCurve(nx, ny, backend.curveX2, backend.curveY2)
                                else backend.setCurve(backend.curveX1, backend.curveY1, nx, ny)
                                e.accepted = true
                            }
                            Rectangle {
                                visible: parent.activeFocus
                                anchors.centerIn: parent
                                width: parent.width + 8
                                height: width
                                radius: width / 2
                                color: "transparent"
                                border.width: 2
                                border.color: Material.foreground
                            }

                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -8
                                preventStealing: true
                                onPositionChanged: (m) => {
                                    if (!pressed) return
                                    var p = mapToItem(curveCanvas, m.x, m.y)
                                    var nx = Math.max(0, Math.min(1, p.x / curveCanvas.width))
                                    var ny = Math.max(0, Math.min(1, 1 - p.y / curveCanvas.height))
                                    if (index === 0) backend.setCurve(nx, ny, backend.curveX2, backend.curveY2)
                                    else backend.setCurve(backend.curveX1, backend.curveY1, nx, ny)
                                }
                            }
                        }
                    }

                    // Where the pen is pressing right now.
                    Rectangle {
                        visible: backend.penInRange && backend.pressure > 0
                        width: 10
                        height: 10
                        radius: 5
                        color: "white"
                        x: curveCanvas.toX(backend.pressure) - 5
                        y: curveCanvas.toY(backend.pressure) - 5
                    }
                }

                Label {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.margins: 6
                    text: qsTr("Pressure in")
                    color: Palette.textDim
                    font.pixelSize: 11
                }
                Label {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: 6
                    text: qsTr("Pressure out")
                    color: Palette.textDim
                    font.pixelSize: 11
                }
            }
        }

        ColumnLayout {
            Layout.preferredWidth: 260
            Layout.minimumWidth: 260
            Layout.maximumWidth: 260
            Layout.alignment: Qt.AlignTop
            spacing: 12

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Palette.textMuted
                text: qsTr("Drag the two handles. Pull the curve down for a softer pen that needs a firmer press; up for a lighter touch.")
            }
            Button { text: qsTr("Straight line"); flat: true; onClicked: backend.resetCurve() }

            Label { text: qsTr("Barrel button"); color: Palette.textMuted }
            ComboBox {
                Layout.fillWidth: true
                textRole: "label"
                valueRole: "value"
                model: [
                    { label: qsTr("Barrel button 1"), value: "stylus" },
                    { label: qsTr("Barrel button 2"), value: "stylus2" },
                    { label: qsTr("Disabled"), value: "none" }
                ]
                currentIndex: Math.max(0, indexOfValue(backend.barrel))
                onActivated: backend.setBarrel(currentValue)
            }
        }
    }
}
