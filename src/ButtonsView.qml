import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// What each pad button sends. A button is whatever keys it types, so a rule
// says "when the pad sends these keys, send those instead".
Item {
    id: root
    property bool learning: false

    Connections {
        target: backend
        function onChordChanged() {
            if (root.learning) {
                fromField.text = backend.lastChord
                root.learning = false
                toField.forceActiveFocus()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 16

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Palette.textMuted
            text: qsTr("Press Learn, then press a button on the pad. Type what it should send, such as ctrl+z or f5, or none to make it do nothing. While the service runs, the button also acts on the desktop as it does today.")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Button {
                text: root.learning ? qsTr("Press a pad button…") : qsTr("Learn")
                highlighted: root.learning
                onClicked: { root.learning = !root.learning; fromField.text = "" }
            }
            TextField {
                id: fromField
                Layout.fillWidth: true
                placeholderText: qsTr("Button sends")
                onAccepted: toField.forceActiveFocus()
            }
            Label { text: "→" }
            TextField {
                id: toField
                Layout.fillWidth: true
                placeholderText: qsTr("Send instead")
                onAccepted: add.clicked()
            }
            Button {
                id: add
                text: qsTr("Add")
                highlighted: true
                onClicked: {
                    const error = backend.addButton(fromField.text, toField.text)
                    errorLabel.text = error
                    if (error === "") { fromField.text = ""; toField.text = "" }
                }
            }
        }

        Label {
            id: errorLabel
            Layout.fillWidth: true
            visible: text !== ""
            color: Palette.error
            wrapMode: Text.WordWrap
        }

        Label {
            text: qsTr("Last pad button: %1").arg(backend.lastChord !== "" ? backend.lastChord : "–")
            color: Palette.textDim
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: backend.buttons

            Label {
                anchors.centerIn: parent
                visible: parent.count === 0
                color: Palette.textDim
                text: qsTr("No buttons remapped.")
            }

            delegate: Rectangle {
                width: ListView.view.width
                height: 48
                radius: theme.radius
                color: Palette.surface

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 8
                    spacing: 12
                    Label { text: modelData.from; font.family: "monospace" }
                    Label { text: "→"; color: Palette.textDim }
                    Label {
                        Layout.fillWidth: true
                        text: modelData.to === "none" ? qsTr("does nothing") : modelData.to
                        font.family: modelData.to === "none" ? "" : "monospace"
                        color: modelData.to === "none" ? Palette.textMuted : Material.foreground
                    }
                    Button {
                        text: qsTr("Remove")
                        flat: true
                        onClicked: backend.removeButton(index)
                    }
                }
            }
        }
    }
}
