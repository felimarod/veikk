import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Which part of the pad is used, which monitor it drives, and which way up.
Item {
    RowLayout {
        anchors.fill: parent
        spacing: 24

        // The screen on top, with the tablet's rectangle on it; the pad under it. Each is named.
        ColumnLayout {
            id: left
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            spacing: 8
            // What the two pictures share once the labels are taken out.
            readonly property real room: Math.max(0, height - screenLabel.height - padLabel.height - 3 * spacing)

            Label {
                id: screenLabel
                text: qsTr("Screen: %1").arg(
                    backend.monitor !== "" ? backend.monitor
                    : backend.realSize && backend.monitors.length > 0 ? backend.monitors[0]
                    : qsTr("All monitors"))
                font.bold: true
                color: Colors.textMuted
            }

            // The screen is the background; the rectangle is where the tablet acts.
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 0
                Layout.preferredHeight: left.room * 0.68

                Rectangle {
                    id: screen
                    readonly property real ratio: backend.monitorAspect > 0 ? backend.monitorAspect : 1.6
                    width: Math.min(parent.width, parent.height * ratio)
                    height: width / ratio
                    anchors.centerIn: parent
                    color: Colors.background
                    radius: Math.min(4, theme.radius)
                    border.color: Colors.textDim
                    border.width: 2

                    Rectangle {
                        id: target
                        x: backend.targetX * screen.width
                        y: backend.targetY * screen.height
                        width: backend.targetW * screen.width
                        height: backend.targetH * screen.height
                        radius: Math.min(4, theme.radius)
                        color: Qt.rgba(Qt.color(theme.accent).r, Qt.color(theme.accent).g, Qt.color(theme.accent).b, 0.18)
                        border.width: 2
                        border.color: theme.accent

                        // Keyboard: arrows move it, Shift+arrows resize it.
                        activeFocusOnTab: true
                        Accessible.role: Accessible.Slider
                        Accessible.name: qsTr("Where the tablet acts")
                        Keys.onPressed: (e) => {
                            var dx = e.key === Qt.Key_Left ? -1 : e.key === Qt.Key_Right ? 1 : 0
                            var dy = e.key === Qt.Key_Up ? -1 : e.key === Qt.Key_Down ? 1 : 0
                            if (dx === 0 && dy === 0) return
                            if ((e.modifiers & Qt.ShiftModifier) && !backend.realActive)
                                backend.setTarget(backend.targetX, backend.targetY,
                                                  backend.targetW + dx * 0.01, backend.targetH + dy * 0.01)
                            else
                                backend.setTarget(backend.targetX + dx * 0.01, backend.targetY + dy * 0.01,
                                                  backend.targetW, backend.targetH)
                            e.accepted = true
                        }
                        Rectangle {
                            visible: target.activeFocus
                            anchors.fill: parent
                            anchors.margins: -4
                            color: "transparent"
                            border.width: 2
                            border.color: Material.foreground
                            radius: parent.radius + 4
                        }

                        Label {
                            anchors.centerIn: parent
                            text: qsTr("Tablet")
                            color: theme.accent
                            font.bold: true
                        }

                        // Moves it over the screen.
                        MouseArea {
                            anchors.fill: parent
                            preventStealing: true
                            cursorShape: Qt.SizeAllCursor
                            property real startX
                            property real startY
                            property real originX
                            property real originY
                            onPressed: (m) => {
                                target.forceActiveFocus()
                                var p = mapToItem(screen, m.x, m.y)
                                startX = p.x
                                startY = p.y
                                originX = backend.targetX
                                originY = backend.targetY
                            }
                            onPositionChanged: (m) => {
                                if (!pressed) return
                                var p = mapToItem(screen, m.x, m.y)
                                backend.setTarget(originX + (p.x - startX) / screen.width,
                                                  originY + (p.y - startY) / screen.height,
                                                  backend.targetW, backend.targetH)
                            }
                        }

                        // Resizes it from the bottom-right corner; real size fixes the size.
                        Rectangle {
                            visible: !backend.realActive
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
                                cursorShape: Qt.SizeFDiagCursor
                                onPositionChanged: (m) => {
                                    if (!pressed) return
                                    var p = mapToItem(screen, m.x, m.y)
                                    backend.setTarget(backend.targetX, backend.targetY,
                                                      p.x / screen.width - backend.targetX,
                                                      p.y / screen.height - backend.targetY)
                                }
                            }
                        }
                    }
                }
            }

            Label {
                id: padLabel
                text: qsTr("Tablet")
                font.bold: true
                color: Colors.textMuted
            }

        // The pad, with the active area drawn on it.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            Layout.preferredHeight: left.room * 0.32

            Rectangle {
                id: pad
                width: Math.min(parent.width, parent.height * backend.tabletAspect)
                height: width / backend.tabletAspect
                anchors.centerIn: parent
                color: Colors.surface
                radius: theme.radius
                border.color: Colors.border

                Rectangle {
                    id: area
                    enabled: !backend.realActive // the area is computed in real size
                    x: backend.areaX * pad.width
                    y: backend.areaY * pad.height
                    width: backend.areaW * pad.width
                    height: backend.areaH * pad.height
                    radius: Math.min(4, theme.radius)
                    color: Qt.rgba(Qt.color(theme.accent).r, Qt.color(theme.accent).g, Qt.color(theme.accent).b, 0.18)
                    border.width: 2
                    border.color: theme.accent

                    // Keyboard: arrows move the area, Shift+arrows resize it.
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Slider
                    Accessible.name: qsTr("Active area")
                    Accessible.description: Qt.locale().toString(backend.areaW * 100, 'f', 0) + " % × "
                        + Qt.locale().toString(backend.areaH * 100, 'f', 0) + " %"
                    Keys.onPressed: (e) => {
                        var dx = e.key === Qt.Key_Left ? -1 : e.key === Qt.Key_Right ? 1 : 0
                        var dy = e.key === Qt.Key_Up ? -1 : e.key === Qt.Key_Down ? 1 : 0
                        if (dx === 0 && dy === 0) return
                        var step = 0.01
                        if (e.modifiers & Qt.ShiftModifier)
                            backend.setArea(backend.areaX, backend.areaY, backend.areaW + dx * step, backend.areaH + dy * step)
                        else
                            backend.setArea(backend.areaX + dx * step, backend.areaY + dy * step, backend.areaW, backend.areaH)
                        e.accepted = true
                    }
                    Rectangle {
                        visible: area.activeFocus
                        anchors.fill: parent
                        anchors.margins: -4
                        color: "transparent"
                        border.width: 2
                        border.color: Material.foreground
                        radius: parent.radius + 4
                    }

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
        }

        // The controls scroll when the window is short, instead of running off it.
        Flickable {
            Layout.preferredWidth: 260
            Layout.minimumWidth: 260
            Layout.maximumWidth: 260
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            contentHeight: controls.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: controls
            width: parent.width
            spacing: 12

            Label { text: qsTr("Monitor"); color: Colors.textMuted }
            ComboBox {
                Layout.fillWidth: true
                model: [qsTr("All monitors")].concat(backend.monitors)
                currentIndex: Math.max(0, backend.monitors.indexOf(backend.monitor) + 1)
                onActivated: (i) => backend.setMonitor(i === 0 ? "" : backend.monitors[i - 1])
            }

            Label { text: qsTr("Rotation"); color: Colors.textMuted }
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
                text: qsTr("Real size (1:1)")
                checked: backend.realSize
                onToggled: backend.setRealSize(checked)
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Colors.textDim
                text: !backend.realSize
                    ? qsTr("A centimetre on the pad is a centimetre on the screen. Drag the rectangle to place it.")
                    : backend.realActive
                        ? qsTr("The pad covers %1 × %2 mm of a %3 × %4 mm screen.")
                              .arg(Qt.locale().toString(backend.rotation % 180 === 0 ? backend.tabletWidthMm : backend.tabletHeightMm, 'f', 0))
                              .arg(Qt.locale().toString(backend.rotation % 180 === 0 ? backend.tabletHeightMm : backend.tabletWidthMm, 'f', 0))
                              .arg(Qt.locale().toString(backend.monitorWidthMm, 'f', 0))
                              .arg(Qt.locale().toString(backend.monitorHeightMm, 'f', 0))
                        : qsTr("The screen reports no physical size, so the area stays as set below.")
            }
            Label { text: qsTr("Pad width (mm)"); color: Colors.textMuted; visible: backend.realSize }
            SpinBox {
                Layout.fillWidth: true
                visible: backend.realSize
                from: 200
                to: 10000
                stepSize: 10
                editable: true
                value: Math.round(backend.tabletWidthMm * 10)
                textFromValue: (v) => Qt.locale().toString(v / 10, 'f', 1)
                valueFromText: (t) => Math.round(Number.fromLocaleString(Qt.locale(), t) * 10)
                onValueModified: backend.setTabletWidthMm(value / 10)
            }

            Switch {
                text: qsTr("Keep proportions")
                enabled: !backend.realActive
                checked: backend.keepProportions
                onToggled: backend.setKeepProportions(checked)
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Colors.textDim
                text: qsTr("A circle drawn on the pad stays a circle on the monitor.")
            }

            Label { text: qsTr("Pad width ÷ height"); color: Colors.textMuted }
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
                text: qsTr("Fill the screen")
                enabled: !backend.realActive
                flat: true
                onClicked: backend.resetTarget()
            }
            Button {
                text: qsTr("Use the whole pad")
                enabled: !backend.realActive
                flat: true
                onClicked: backend.resetArea()
            }
        }
        }
    }
}
