import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Which part of the pad is used, which monitor it drives, and which way up.
Item {
    RowLayout {
        anchors.fill: parent
        spacing: 24

        // The pad, with the active area drawn on it.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Rectangle {
                id: pad
                width: Math.min(parent.width, parent.height * backend.tabletAspect)
                height: width / backend.tabletAspect
                anchors.centerIn: parent
                color: "#17171a"
                radius: theme.radius
                border.color: "#2a2a2f"

                Rectangle {
                    id: area
                    x: backend.areaX * pad.width
                    y: backend.areaY * pad.height
                    width: backend.areaW * pad.width
                    height: backend.areaH * pad.height
                    radius: Math.min(4, theme.radius)
                    color: Qt.rgba(Qt.color(theme.accent).r, Qt.color(theme.accent).g, Qt.color(theme.accent).b, 0.18)
                    border.width: 2
                    border.color: theme.accent

                    Label {
                        anchors.centerIn: parent
                        text: Qt.locale().toString(backend.areaW * 100, 'f', 0) + " % × "
                            + Qt.locale().toString(backend.areaH * 100, 'f', 0) + " %"
                        color: theme.accent
                    }

                    // Moves the area.
                    MouseArea {
                        id: mover
                        anchors.fill: parent
                        preventStealing: true
                        property real startX
                        property real startY
                        property real originX
                        property real originY
                        onPressed: (m) => {
                            var p = mapToItem(pad, m.x, m.y)
                            startX = p.x; startY = p.y
                            originX = backend.areaX; originY = backend.areaY
                        }
                        onPositionChanged: (m) => {
                            if (!pressed) return
                            var p = mapToItem(pad, m.x, m.y)
                            backend.setArea(originX + (p.x - startX) / pad.width,
                                            originY + (p.y - startY) / pad.height,
                                            backend.areaW, backend.areaH)
                        }
                    }

                    // Resizes it from the bottom-right corner.
                    Rectangle {
                        width: 16
                        height: 16
                        x: parent.width - 8
                        y: parent.height - 8
                        radius: Math.min(3, theme.radius)
                        color: theme.accent

                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -6
                            preventStealing: true
                            onPositionChanged: (m) => {
                                if (!pressed) return
                                var p = mapToItem(pad, m.x, m.y)
                                backend.setArea(backend.areaX, backend.areaY,
                                                p.x / pad.width - backend.areaX,
                                                p.y / pad.height - backend.areaY)
                            }
                        }
                    }
                }
            }
        }

        ColumnLayout {
            Layout.preferredWidth: 260
            Layout.minimumWidth: 260
            Layout.maximumWidth: 260
            Layout.alignment: Qt.AlignTop
            spacing: 12

            Label { text: qsTr("Monitor"); color: "#9a9aa0" }
            ComboBox {
                Layout.fillWidth: true
                model: [qsTr("All monitors")].concat(backend.monitors)
                currentIndex: Math.max(0, backend.monitors.indexOf(backend.monitor) + 1)
                onActivated: (i) => backend.setMonitor(i === 0 ? "" : backend.monitors[i - 1])
            }

            Label { text: qsTr("Rotation"); color: "#9a9aa0" }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: [0, 90, 180, 270]
                    Button {
                        Layout.fillWidth: true
                        leftPadding: 4
                        rightPadding: 4
                        text: modelData + "°"
                        highlighted: backend.rotation === modelData
                        flat: !highlighted
                        onClicked: backend.setRotation(modelData)
                    }
                }
            }

            Switch {
                text: qsTr("Keep proportions")
                checked: backend.keepProportions
                onToggled: backend.setKeepProportions(checked)
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: "#6a6a70"
                text: qsTr("A circle drawn on the pad stays a circle on the monitor.")
            }

            Label { text: qsTr("Pad width ÷ height"); color: "#9a9aa0" }
            SpinBox {
                Layout.fillWidth: true
                from: 50
                to: 300
                stepSize: 5
                editable: true
                value: Math.round(backend.tabletAspect * 100)
                textFromValue: (v) => Qt.locale().toString(v / 100, 'f', 2)
                valueFromText: (t) => Math.round(Number.fromLocaleString(Qt.locale(), t) * 100)
                onValueModified: backend.setTabletAspect(value / 100)
            }

            Button {
                text: qsTr("Use the whole pad")
                flat: true
                onClicked: backend.resetArea()
            }
        }
    }
}
