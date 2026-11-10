import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// The tablet from above with its six buttons in a column on the left. The one
// held lights up, and next to it is what the system recognises: the keys the
// pad types and, when a rule changes them, what the desktop gets instead.
Item {
    id: root
    implicitWidth: 240
    implicitHeight: 170

    readonly property int slots: 6
    readonly property bool known: backend.padSlots.some(function (c) { return c !== "" })

    Rectangle {
        id: body
        anchors.fill: parent
        radius: Math.min(10, theme.radius + 4)
        color: Colors.surface
        border.color: Colors.border

        // The drawing surface.
        Rectangle {
            id: surface
            x: 44; y: 10
            width: parent.width - 54
            height: parent.height - 20
            radius: Math.min(4, theme.radius)
            color: Colors.background
            border.color: Colors.border
        }

        Repeater {
            model: root.slots
            Rectangle {
                id: key
                readonly property bool held: backend.padSlot === index
                readonly property bool learned: backend.padSlots[index] !== ""
                x: 10
                y: 12 + index * ((body.height - 24) / root.slots)
                width: 26
                height: (body.height - 24) / root.slots - 6
                radius: Math.min(5, theme.radius)
                color: held ? theme.accent : "transparent"
                border.width: 1.5
                border.color: held ? theme.accent : learned ? Colors.textMuted : Colors.textDim

                Label {
                    anchors.centerIn: parent
                    text: index + 1
                    font.bold: true
                    color: key.held ? theme.accentForeground : key.learned ? Colors.textMuted : Colors.textDim
                }

                // What the system recognises, beside the button.
                Rectangle {
                    visible: key.held
                    x: parent.width + 10
                    anchors.verticalCenter: parent.verticalCenter
                    width: bubbleText.implicitWidth + 18
                    height: bubbleText.implicitHeight + 10
                    radius: Math.min(6, theme.radius)
                    color: Colors.surface
                    border.color: theme.accent
                    z: 2
                    Column {
                        id: bubbleText
                        anchors.centerIn: parent
                        Label {
                            text: backend.padChord
                            font.bold: true
                            color: theme.accent
                        }
                        Label {
                            visible: backend.padSends !== backend.padChord
                            text: "→ " + (backend.padSends === "none" ? qsTr("nothing") : backend.padSends)
                            font.pixelSize: 12
                            color: Colors.textMuted
                        }
                    }
                }
            }
        }

        // A chord that is not one of the learned buttons still shows what came in.
        Rectangle {
            visible: backend.padChord !== "" && backend.padSlot < 0
            anchors.centerIn: surface
            width: unknownText.implicitWidth + 18
            height: unknownText.implicitHeight + 10
            radius: Math.min(6, theme.radius)
            color: Colors.surface
            border.color: theme.accent
            Column {
                id: unknownText
                anchors.centerIn: parent
                Label { text: backend.padChord; font.bold: true; color: theme.accent }
                Label {
                    visible: backend.padSends !== backend.padChord
                    text: "→ " + (backend.padSends === "none" ? qsTr("nothing") : backend.padSends)
                    font.pixelSize: 12
                    color: Colors.textMuted
                }
            }
        }

        Label {
            visible: backend.padChord === ""
            anchors.centerIn: surface
            width: surface.width - 16
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.pixelSize: 12
            color: Colors.textDim
            text: root.known ? qsTr("Press a pad button") : qsTr("Press Identify buttons to teach veikk which is which")
        }
    }
}
