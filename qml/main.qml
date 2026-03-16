import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AuraUI 1.0

ApplicationWindow {
    id: window
    width: 1200
    height: 800
    visible: true
    title: "QDrive Cloud Sync"
    color: Theme.bgDark

// ── FontAwesome fonts ──────────────────────────────────────────────────────
    FontLoader { id: faSolid;   source: "qrc:/assets/fonts/fa-solid-900.otf"   }
    FontLoader { id: faRegular; source: "qrc:/assets/fonts/fa-regular-400.otf" }
    FontLoader { id: faBrands;  source: "qrc:/assets/fonts/fa-brands-400.otf"  }
    // Ambient background glow
/*    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "#1A1E2E" }
            GradientStop { position: 0.5; color: Theme.bgDark }
            GradientStop { position: 1.0; color: "#12101A" }
        }
    }
*/
    // ── Title Bar ─────────────────────────────────────────────────────────────
    Item {
        id: titleBar
        width: parent.width
        height: 44
        z: 100
/*
        // macOS traffic-light dots
        Row {
            anchors.left: parent.left
            anchors.leftMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Repeater {
                model: ["#FF5F56", "#FFBD2E", "#27C93F"]
                Rectangle {
                    width: 12; height: 12; radius: 6
                    color: modelData
                }
            }
        }
        // Drag-to-move
        MouseArea {
            anchors.fill: parent
            property point clickPos: Qt.point(0, 0)
            onPressed: (mouse) => { clickPos = Qt.point(mouse.x, mouse.y) }
            onPositionChanged: (mouse) => {
                window.x += mouse.x - clickPos.x
                window.y += mouse.y - clickPos.y
            }
        }
        */
    }
    // ── App Container ─────────────────────────────────────────────────────────
    Rectangle {
        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 0
        color: "transparent"

        Row {
            anchors.fill: parent

            // ── Sidebar ───────────────────────────────────────────────────────
            Rectangle {
                id: sidebar
                width: Theme.sidebarWidth
                height: parent.height
                color: Theme.bgSidebar

                // Right edge separator
                Rectangle {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 1
                    color: Theme.glassBorder
                }

                Column {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 0

                    // Brand
                    Row {
                        spacing: 10
                        bottomPadding: 32

                        Rectangle {
                            width: 26; height: 26; radius: 13
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0; color: Theme.auraCyan }
                                GradientStop { position: 1; color: Theme.auraBlue }
                            }
                        }
                        Text {
                            text: "QDrive Cloud Sync"
                            color: Theme.textPrimary
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    // Nav items
                    Column {
                        id: navMenu
                        width: parent.width
                        spacing: 4
                        property int activeIndex: 0

                        Repeater {
                            model: [
                                { label: "Home",     sub: "Dashboard", icon: "\uf015", view: "qrc:/AuraUI/DashboardView.qml" },
                                { label: "Syncing",  sub: "Active",    icon: "\uf021", view: "qrc:/AuraUI/ActivityView.qml"  },
                                { label: "Files",    sub: "Folders",   icon: "\uf07c", view: "qrc:/AuraUI/ExplorerView.qml"  },
                                { label: "Sharing",  sub: "Links",     icon: "\uf0c1", view: ""                              },
                                { label: "Backups",  sub: "Snapshots", icon: "\uf1da", view: ""                              },
                                { label: "Settings", sub: "Config",    icon: "\uf013", view: "qrc:/AuraUI/SettingsView.qml"  }
                            ]
                            delegate: Rectangle {
                                width: navMenu.width
                                height: 54
                                radius: 10 
                                color: navMenu.activeIndex === index
                                       ? "#15FFFFFF" : "transparent"
                                border.color: navMenu.activeIndex === index
                                              ? Theme.glassBorder : "transparent"
                                border.width: 1

                                // Cyan indicator bar
                                Rectangle {
                                    width: 3; height: 22; radius: 2
                                    anchors.left: parent.left
                                    anchors.leftMargin: 4
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: Theme.auraCyan
                                    visible: navMenu.activeIndex === index
                                }

                                Row {
                                    anchors.fill: parent
                                    anchors.leftMargin: 20
                                    spacing: 14

                                    Text {
                                        text: modelData.icon
                                        font.pixelSize: 16
                                        color: navMenu.activeIndex === index
                                               ? Theme.auraCyan : Theme.textSecondary
                                        anchors.verticalCenter: parent.verticalCenter
                                    }

                                    Column {
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 2
                                        Text {
                                            text: modelData.label
                                            font.pixelSize: 13
                                            font.weight: navMenu.activeIndex === index
                                                         ? Font.Medium : Font.Normal
                                            color: navMenu.activeIndex === index
                                                   ? Theme.textPrimary : Theme.textSecondary
                                        }
                                        Text {
                                            text: modelData.sub
                                            font.pixelSize: 10
                                            color: navMenu.activeIndex === index
                                                   ? Theme.auraCyan : Theme.textSecondary
                                            opacity: 0.8
                                        }
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        navMenu.activeIndex = index
                                        if (modelData.view !== "")
                                            contentStack.replace(modelData.view)
                                    }
                                }
                            }
                        }
                    }

                    Item { height: 1; Layout.fillHeight: true }

                    // User profile (pinned to bottom via Column + Item spacer trick)
                    Item {
                        width: parent.width
                        height: parent.height
                               - 26           // brand
                               - 32           // brand bottom padding
                               - (54 + 4) * 6 // nav items
                               - 60           // profile height

                        // invisible spacer
                    }

                    Rectangle {
                        width: parent.width
                        height: 50
                        color: "transparent"

                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 10

                            Rectangle {
                                width: 34; height: 34; radius: 17
                                color: "#2A2D36"

                                Text {
                                    anchors.centerIn: parent
                                    text: "👨🏻"
                                    font.pixelSize: 18
                                }

                                // Online dot
                                Rectangle {
                                    width: 9; height: 9; radius: 5
                                    color: Theme.successGreen
                                    border.color: Theme.bgSidebar
                                    border.width: 2
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                }
                            }

                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 2
                                Text { text: "Sandeep Kumar G R";  color: Theme.textPrimary;   font.pixelSize: 13; font.weight: Font.Medium }
                                Text { text: "Online";   color: Theme.successGreen;  font.pixelSize: 10 }
                            }
                        }
                    }
                }
            }

            // ── Content Area ──────────────────────────────────────────────────
            Rectangle {
                width: parent.width - sidebar.width
                height: parent.height
                color: Theme.bgDark

                StackView {
                    id: contentStack
                    anchors.fill: parent
                    anchors.margins: 36
                    initialItem: "qrc:/AuraUI/DashboardView.qml"

                    replaceEnter: Transition {
                        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 350; easing.type: Easing.OutCubic }
                        NumberAnimation { property: "y"; from: 16; to: 0; duration: 350; easing.type: Easing.OutQuint }
                    }
                    replaceExit: Transition {
                        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 180 }
                    }
                }
            }
        }
    }
}
