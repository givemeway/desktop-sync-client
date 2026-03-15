import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import AuraUI 1.0

Item {
    id: dashboardRoot

    // ── Page Title ────────────────────────────────────────────────────────────
    Text {
        id: pageTitle
        text: "Dashboard"
        color: Theme.textPrimary
        font.pixelSize: 26
        font.weight: Font.Medium
        anchors.top: parent.top
        anchors.left: parent.left
    }

    // ── Two-column body ───────────────────────────────────────────────────────
    Row {
        anchors.top: pageTitle.bottom
        anchors.topMargin: 24
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: 0

        // ══ LEFT: Orb Column ═════════════════════════════════════════════════
        Item {
            id: orbColumn
            width: parent.width * 0.57
            height: parent.height

            // Storage text above the orb
            Column {
                id: storageText
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 8
                spacing: 4

                Text {
                    text: "72%"
                    color: Theme.auraCyan
                    font.pixelSize: 22
                    font.weight: Font.Bold
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                Text {
                    text: "Storage Usage"
                    color: Theme.textPrimary
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                Text {
                    text: "7.2 TB / 10 TB used"
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    anchors.horizontalCenter: parent.horizontalCenter
                }
            }

            // ── Orb + Rings ───────────────────────────────────────────────────
            /*Item {
                id: orbArea
                width: 400
                height: 400
                anchors.centerIn: parent
                anchors.verticalCenterOffset: 10

                // ---- Progress rings via Canvas --------------------------------
                Canvas {
                    id: ringCanvas
                    anchors.fill: parent
                    antialiasing: true

                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.reset()
                        var cx = width  / 2
                        var cy = height / 2
                        var rInner = 185
                        var rOuter = 197

                        // Track circles (dim)
                        ctx.beginPath()
                        ctx.arc(cx, cy, rInner, 0, Math.PI * 2)
                        ctx.strokeStyle = "rgba(255,255,255,0.06)"
                        ctx.lineWidth = 1.5
                        ctx.stroke()

                        ctx.beginPath()
                        ctx.arc(cx, cy, rOuter, 0, Math.PI * 2)
                        ctx.strokeStyle = "rgba(255,255,255,0.04)"
                        ctx.lineWidth = 1
                        ctx.stroke()

                        // Inner cyan arc – 72% clockwise from top
                        var cyanGrad = ctx.createLinearGradient(cx - rInner, cy, cx + rInner, cy)
                        cyanGrad.addColorStop(0, "#00E5FF")
                        cyanGrad.addColorStop(1, "#0097A7")
                        ctx.beginPath()
                        ctx.arc(cx, cy, rInner,
                                -Math.PI / 2,
                                -Math.PI / 2 + Math.PI * 2 * 0.45)
                        ctx.strokeStyle = cyanGrad
                        ctx.lineWidth = 3
                        ctx.lineCap = "round"
                        ctx.stroke()

                        // Outer purple arc – remaining 28%
                        var purpleGrad = ctx.createLinearGradient(cx, cy - rOuter, cx, cy + rOuter)
                        purpleGrad.addColorStop(0, "#8B5CF6")
                        purpleGrad.addColorStop(1, "#6D28D9")
                        ctx.beginPath()
                        ctx.arc(cx, cy, rOuter,
                                -Math.PI / 2 + Math.PI * 2 * 0.55,
                                -Math.PI / 2 + Math.PI * 2)
                        ctx.strokeStyle = purpleGrad
                        ctx.lineWidth = 2
                        ctx.lineCap = "round"
                        ctx.stroke()
                    }
                }

                // ---- Outer ambient glow halo ----------------------------------
                Rectangle {
                    anchors.centerIn: parent
                    width: 310; height: 310
                    radius: 155
                    color: "transparent"

                    // We fake a radial bloom with layered translucent circles
                    Repeater {
                        model: 5
                        Rectangle {
                            anchors.centerIn: parent
                            width:  310 - index * 30
                            height: 310 - index * 30
                            radius: width / 2
                            color: "transparent"
                            border.color: Qt.rgba(0, 0.56 - index*0.06, 1, 0.06 - index*0.01)
                            border.width: 8
                        }
                    }
                }

                // ---- The Sphere ----------------------------------------------
                Item {
                    anchors.centerIn: parent
                    width: 262; height: 262

                    // Pulse animation
                    SequentialAnimation on scale {
                        loops: Animation.Infinite
                        NumberAnimation { to: 1.025; duration: 3000; easing.type: Easing.InOutSine }
                        NumberAnimation { to: 1.0;   duration: 3000; easing.type: Easing.InOutSine }
                    }

                    // Base sphere gradient (dark blue-purple bottom, light blue top)
                    Rectangle {
                        anchors.fill: parent
                        radius: width / 2
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "#C5E8F8" }  // icy highlight top
                            GradientStop { position: 0.25; color: "#7EC8E3" } // light blue
                            GradientStop { position: 0.5;  color: "#4B8EC2" } // mid blue
                            GradientStop { position: 0.75; color: "#3B3080" } // blue-purple
                            GradientStop { position: 1.0;  color: "#1A0F3A" } // deep purple bottom
                        }
                    }

                    // Teal shimmer overlay (bottom-left)
                    Rectangle {
                        anchors.fill: parent
                        radius: width / 2
                        color: "transparent"

                        // Approximate radial using a second gradient
                        Rectangle {
                            x: -10; y: parent.height * 0.55
                            width: parent.width * 0.75
                            height: parent.height * 0.55
                            radius: width / 2
                            color: "#6000E5CC" // teal glow
                            opacity: 0.45
                        }
                    }

                    // Purple shimmer (right side)
                    Rectangle {
                        x: parent.width * 0.55
                        y: 10
                        width: parent.width * 0.5
                        height: parent.height * 0.5
                        radius: width / 2
                        color: "#608B5CF6"
                        opacity: 0.35
                    }

                    // Specular highlight (top-left bright spot)
                    Rectangle {
                        x: parent.width * 0.12
                        y: parent.height * 0.08
                        width: parent.width * 0.45
                        height: parent.height * 0.38
                        radius: width / 2
                        color: "white"
                        opacity: 0.28
                    }

                    // Thin bright rim at top
                    Rectangle {
                        x: parent.width * 0.18
                        y: parent.height * 0.05
                        width: parent.width * 0.28
                        height: parent.height * 0.10
                        radius: width / 2
                        color: "white"
                        opacity: 0.55
                    }

                    // Outer rim border
                    Rectangle {
                        anchors.fill: parent
                        radius: width / 2
                        color: "transparent"
                        border.color: Theme.auraCyan
                        border.width: 1
                        opacity: 0.25
                    }
                }
            }
            */

            // ---- Status pill -------------------------------------------------
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 12
                anchors.horizontalCenter: parent.horizontalCenter
                width: 250; height: 30
                radius: 15
                color: "#20FFFFFF"
                border.color: Theme.glassBorder
                border.width: 1

                Row {
                    anchors.centerIn: parent
                    spacing: 8
                    Text { text: "((•))"; color: Theme.auraPurple; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: "Status: Syncing (18 mins remaining)"; color: Theme.textSecondary; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                }
            }
        }

        // ══ RIGHT: Activity + Cards ══════════════════════════════════════════
        Item {
            width: parent.width - orbColumn.width
            height: parent.height

            Column {
                anchors.fill: parent
                spacing: 20

                // Recent Sync Activity header
                Text {
                    text: "Recent Sync Activity"
                    color: Theme.textPrimary
                    font.pixelSize: 15
                    font.weight: Font.Medium
                }

                // Activity list
                Column {
                    width: parent.width
                    spacing: 14

                    Repeater {
                        model: [
                            { name: "Design_Final.pdf",    meta: "Syncing",    progress: 72,  syncing: true  },
                            { name: "Client_Brief.docx",   meta: "4 mins ago", progress: 100, syncing: false },
                            { name: "project_asset_v9.png",meta: "12 mins ago",progress: 100, syncing: false }
                        ]

                        delegate: Column {
                            width: parent.width
                            spacing: 6

                            Row {
                                width: parent.width
                                spacing: 12

                                // File icon
                                Rectangle {
                                    width: 34; height: 34; radius: 8
                                    color: "#1A1D26"
                                    border.color: "#2A2D38"
                                    border.width: 1
                                    Text {
                                        anchors.centerIn: parent
                                        text: index === 1 ? "📝" : index === 2 ? "🖼" : "📄"
                                        font.pixelSize: 14
                                    }
                                }

                                // Name + meta
                                Column {
                                    width: parent.width - 34 - 12 - (modelData.syncing ? 32 : 0)
                                    spacing: 3
                                    anchors.verticalCenter: parent.verticalCenter

                                    Text {
                                        text: modelData.name
                                        color: Theme.textPrimary
                                        font.pixelSize: 13
                                        font.weight: Font.Medium
                                        elide: Text.ElideRight
                                        width: parent.width
                                    }
                                    Text {
                                        text: modelData.meta
                                        color: modelData.syncing ? Theme.auraCyan : Theme.textSecondary
                                        font.pixelSize: 10
                                    }
                                }

                                // Progress % badge
                                Text {
                                    text: modelData.progress + "%"
                                    color: Theme.textSecondary
                                    font.pixelSize: 11
                                    visible: modelData.syncing
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            // Progress bar (syncing only)
                            Rectangle {
                                width: parent.width - 46
                                x: 46
                                height: 2
                                radius: 1
                                color: "#1A1D26"
                                visible: modelData.syncing

                                Rectangle {
                                    width: parent.width * (modelData.progress / 100)
                                    height: 2
                                    radius: 1
                                    gradient: Gradient {
                                        orientation: Gradient.Horizontal
                                        GradientStop { position: 0; color: Theme.auraCyan }
                                        GradientStop { position: 1; color: Theme.auraBlue }
                                    }
                                }
                            }
                        }
                    }
                }

                // Flexible spacer
                Item { width: 1; height: parent.height - 200 - 160 }

                // Action cards
                Column {
                    width: parent.width
                    spacing: 10

                    // Local Files
                    Rectangle {
                        width: parent.width
                        height: 64
                        radius: 12
                        color: "#1E2130"
                        border.color: "#2A2D3A"
                        border.width: 1

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 12

                            Rectangle {
                                width: 30; height: 30; radius: 7
                                color: "#151820"
                                border.color: "#2A2D38"; border.width: 1
                                Layout.alignment: Qt.AlignVCenter
                                Text { anchors.centerIn: parent; text: "💻"; font.pixelSize: 14 }
                            }
                            Column {
                                Layout.alignment: Qt.AlignVCenter
                                spacing: 2
                                Text { text: "Local Files";   color: Theme.textPrimary;   font.pixelSize: 13; font.weight: Font.Medium }
                                Text { text: "(MacBook Pro)"; color: Theme.textSecondary; font.pixelSize: 10 }
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: "›"; color: Theme.textSecondary; font.pixelSize: 20
                                Layout.alignment: Qt.AlignVCenter
                            }
                        }
                    }

                    // Cloud Files
                    Rectangle {
                        width: parent.width
                        height: 64
                        radius: 12
                        color: "#1E2130"
                        border.color: "#2A2D3A"
                        border.width: 1

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 12

                            Rectangle {
                                width: 30; height: 30; radius: 7
                                color: "#151820"
                                border.color: "#2A2D38"; border.width: 1
                                Layout.alignment: Qt.AlignVCenter
                                Text { anchors.centerIn: parent; text: "☁️"; font.pixelSize: 14 }
                            }
                            Column {
                                Layout.alignment: Qt.AlignVCenter
                                spacing: 2
                                Text { text: "Cloud Files"; color: Theme.textPrimary;   font.pixelSize: 13; font.weight: Font.Medium }
                                Text { text: "Sync status"; color: Theme.textSecondary; font.pixelSize: 10 }
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: "↻"; color: Theme.successGreen; font.pixelSize: 18
                                Layout.alignment: Qt.AlignVCenter
                            }
                        }
                    }
                }
            }
        }
    }
}
