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
                color: "#17171a"
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
                        ctx.strokeStyle = "#2a2a2f"
                        ctx.lineWidth = 1
                        for (var g = 0; g <= 4; ++g) {
                            ctx.beginPath(); ctx.moveTo(toX(g / 4), 0); ctx.lineTo(toX(g / 4), height); ctx.stroke()
                            ctx.beginPath(); ctx.moveTo(0, toY(g / 4)); ctx.lineTo(width, toY(g / 4)); ctx.stroke()
                        }
                        // Handle stems.
                        ctx.strokeStyle = "#55555c"
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
                            border.color: "#0e0e10"

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
                    color: "#6a6a70"
                    font.pixelSize: 11
                }
                Label {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: 6
                    text: qsTr("Pressure out")
                    color: "#6a6a70"
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
                color: "#9a9aa0"
                text: qsTr("Drag the two handles. Pull the curve down for a softer pen that needs a firmer press; up for a lighter touch.")
            }
            Button { text: qsTr("Straight line"); flat: true; onClicked: backend.resetCurve() }

            Label { text: qsTr("Barrel button"); color: "#9a9aa0" }
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
