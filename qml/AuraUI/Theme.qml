pragma Singleton
import QtQuick

QtObject {
    // Colors - Aura Premium Theme
    readonly property color auraMint:    "#85FFC7"
    readonly property color auraCyan:    "#00E5FF"
    readonly property color auraBlue:    "#3B82F6"
    readonly property color auraPurple:  "#8B5CF6"

    readonly property color bgDark:      "#181A20"
    readonly property color bgSidebar:   "#13151A"
    readonly property color glassWhite:  "#08FFFFFF"
    readonly property color glassBorder: "#1AFFFFFF"

    readonly property color textPrimary:   "#FFFFFF"
    readonly property color textSecondary: "#A0AEC0"

    readonly property color successGreen: "#10B981"
    readonly property color errorRed:     "#EF4444"

    // Effects
    readonly property real glassBlur:     20
    readonly property real cardRadius:    16
    readonly property real shadowOpacity: 0.3

    // Typography
    readonly property string fontFamily:  "Inter, -apple-system, sans-serif"
    readonly property int fontSizeH1:     32
    readonly property int fontSizeH2:     20
    readonly property int fontSizeBody:   14
    readonly property int fontSizeLabel:  12

    // Spacing
    readonly property real paddingS:      8
    readonly property real paddingM:      16
    readonly property real sidebarWidth:  220
}
