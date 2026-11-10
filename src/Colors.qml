pragma Singleton
import QtQuick

// The colours every view shares. The accent and the corner radius come from
// the Omarchy theme (the `theme` context property); these are the neutrals.
QtObject {
    readonly property color background: "#0e0e10"
    readonly property color surface: "#17171a"
    readonly property color border: "#2a2a2f"
    readonly property color textMuted: "#9a9aa0"
    readonly property color textDim: "#6a6a70"
    readonly property color disabled: "#55555c"
    readonly property color error: "#ff9a8a"
    readonly property color errorSurface: "#3a1d1d"
    readonly property color scrim: "#d0000000"
}
