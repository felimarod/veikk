import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1040
    height: 720
    minimumWidth: 760
    minimumHeight: 520
    visible: true
    title: "Veikk"
    color: Palette.background

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    // Single-key shortcuts must not fire while a field is being typed in or a
    // pad button is being learned, since the pad types real keys.
    property bool typing: (activeFocusItem && activeFocusItem.hasOwnProperty("cursorPosition")) || buttonsView.learning

    function setPage(index) { pages.currentIndex = index }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; onActivated: { win.helpVisible = false; buttonsView.learning = false } }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: Qt.quit() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: testView.clear() }
    Shortcut { sequence: "1"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.setPage(0) }
    Shortcut { sequence: "2"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.setPage(1) }
    Shortcut { sequence: "3"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.setPage(2) }
    Shortcut { sequence: "4"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.setPage(3) }
    Shortcut { sequences: [StandardKey.Undo]; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: backend.undo() }
    Shortcut { sequences: [StandardKey.Redo]; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: backend.redo() }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 14

            RowLayout {
                Layout.fillWidth: true
                spacing: 16

                Label {
                    text: "Veikk"
                    font.pixelSize: 24
                    font.bold: true
                }
                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    color: backend.penFound ? Material.foreground : Palette.error
                    text: backend.penFound
                        ? (backend.daemonRunning ? qsTr("%1, shaped by the service").arg(backend.deviceName)
                                                 : qsTr("%1, raw (service stopped)").arg(backend.deviceName))
                        : qsTr("No Veikk tablet found")
                }
                Button {
                    flat: !backend.daemonRunning
                    highlighted: !backend.daemonRunning
                    text: backend.daemonRunning ? qsTr("Stop service") : qsTr("Start service")
                    onClicked: backend.setDaemonEnabled(!backend.daemonRunning)
                }
            }

            Rectangle {
                Layout.fillWidth: true
                visible: msg.text !== ""
                implicitHeight: msg.implicitHeight + 20
                radius: theme.radius
                color: Palette.errorSurface
                Label {
                    id: msg
                    anchors.fill: parent
                    anchors.margins: 10
                    wrapMode: Text.WordWrap
                    text: backend.saveError !== "" ? backend.saveError
                        : backend.problem !== "" ? backend.problem
                        : !backend.daemonRunning
                            ? qsTr("The service is stopped, so the desktop sees this tablet as a plain mouse and none of these settings apply. Start the service to use them.")
                            : ""
                }
            }

            TabBar {
                id: pages
                Layout.fillWidth: true
                background: null
                TabButton { text: qsTr("1  Test") }
                TabButton { text: qsTr("2  Area") }
                TabButton { text: qsTr("3  Buttons") }
                TabButton { text: qsTr("4  Pen") }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: pages.currentIndex

                PenTestView { id: testView }
                AreaView {}
                ButtonsView { id: buttonsView }
                PenSettingsView {}
            }
        }

        // The shortcut overlay.
        Rectangle {
            anchors.fill: parent
            visible: win.helpVisible
            color: Palette.scrim
            MouseArea { anchors.fill: parent; onClicked: win.helpVisible = false }

            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width - 40, 460)
                height: helpColumn.implicitHeight + 40
                radius: theme.radius
                color: Palette.surface

                ColumnLayout {
                    id: helpColumn
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 8

                    Label { text: qsTr("Shortcuts"); font.pixelSize: 18; font.bold: true }
                    Repeater {
                        model: [
                            ["1 – 4", qsTr("Switch page")],
                            ["Space", qsTr("Clear the test canvas")],
                            ["Ctrl+Z", qsTr("Undo a setting")],
                            ["Ctrl+Shift+Z", qsTr("Redo a setting")],
                            ["?", qsTr("Show or hide this list")],
                            ["Esc", qsTr("Close this list, stop learning a button")],
                            ["Q", qsTr("Quit")]
                        ]
                        RowLayout {
                            Layout.fillWidth: true
                            Label { Layout.preferredWidth: 130; text: modelData[0]; color: theme.accent; font.family: "monospace" }
                            Label { Layout.fillWidth: true; text: modelData[1]; wrapMode: Text.WordWrap }
                        }
                    }
                }
            }
        }
    }
}
