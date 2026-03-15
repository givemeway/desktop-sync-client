import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AuraUI 1.0

Item {
    id: activityRoot

    Column {
        anchors.fill: parent
        spacing: 20

        // ── Header Row ────────────────────────────────────────────────────────
        RowLayout {
            width: parent.width
            height: 40

            Text {
                text: "My Cloud Drive"
                color: Theme.textPrimary
                font.pixelSize: 26
                font.weight: Font.Medium
                Layout.fillWidth: true
            }

            // Search bar
            Rectangle {
                width: 190; height: 34
                radius: 17
                color: "#15FFFFFF"
                border.color: Theme.glassBorder
                border.width: 1
                Layout.alignment: Qt.AlignVCenter

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    spacing: 8
                    Text { text: "🔍"; color: Theme.textSecondary; font.pixelSize: 13 }
                    Text { text: "Search files…"; color: Theme.textSecondary; font.pixelSize: 12 }
                }
            }

            // New Folder button
            Rectangle {
                width: 96; height: 34; radius: 8
                color: "#1AFFFFFF"
                border.color: Theme.glassBorder
                border.width: 1
                Layout.alignment: Qt.AlignVCenter
                Text { text: "New Folder"; color: "white"; anchors.centerIn: parent; font.pixelSize: 12 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor }
            }

            // Upload button
            Rectangle {
                width: 76; height: 34; radius: 8
                color: Theme.auraBlue
                Layout.alignment: Qt.AlignVCenter
                Text { text: "Upload"; color: "white"; anchors.centerIn: parent; font.pixelSize: 12; font.weight: Font.Medium }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor }
            }
        }

        // ── File List Panel ───────────────────────────────────────────────────
        Rectangle {
            width: parent.width
            height: parent.height - 60
            radius: 14
            color: "#0C121F"
            border.color: Theme.glassBorder
            border.width: 1
            clip: true

            Column {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 10

                // Table headers
                RowLayout {
                    width: parent.width
                    height: 28
                    Text { text: "Status";        color: Theme.textSecondary; font.pixelSize: 12; Layout.preferredWidth: 52 }
                    Text { text: "Name";          color: Theme.textSecondary; font.pixelSize: 12; Layout.preferredWidth: 200  }
                    Text { text: "Progress";      color: Theme.textSecondary; font.pixelSize: 12; Layout.preferredWidth: 90 }
                    Text { text: "Type";          color: Theme.textSecondary; font.pixelSize: 12; Layout.preferredWidth: 90 }
                    Text { text: "Size";          color: Theme.textSecondary; font.pixelSize: 12; Layout.preferredWidth: 90 }
                    Text { text: "Path";          color: Theme.textSecondary; font.pixelSize: 12; Layout.preferredWidth: 200 }
                  }

                Rectangle { width: parent.width; height: 1; color: Theme.glassBorder }

                // File rows
                ListView {
                    width: parent.width
                    height: parent.height - 40
                    spacing: 6
                    clip: true
                    model: syncController.activityModel

                    ScrollBar.vertical: ScrollBar {
                      policy: ScrollBar.AsNeeded    // only shows when content overflows
                    }
                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 52
                        radius: 10
                       // color: status == "syncing" ? "#1A334466" : "transparent"
                        border.color: status == "syncing" ? "#336699" : "transparent"
                        border.width: 1
                        property bool hovered:false
                        color: hovered  ? "#1A2A3A" :        // hover color
                               status === "syncing" ? "#1A334466" : "transparent"                          // default
                        MouseArea {
                          anchors.fill: parent
                          hoverEnabled: true                        // must enable this
                          //cursorShape: Qt.PointingHandCursor        // hand cursor on hover
                          onEntered: parent.hovered = true          // mouse enters row
                          onExited:  parent.hovered = false         // mouse leaves row
                        }

                        Behavior on color { ColorAnimation { duration: 150 }
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 14

                            // Status indicator
                            Item {
                                Layout.preferredWidth: 36
                                Layout.preferredHeight: 36

                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 22; height: 22; radius: 11
                                    color: "transparent"
                                    border.color: status == "queued" ? Theme.auraBlue : 
                                                  status == "done" ? Theme.successGreen : 
                                                  status == "error" ? Theme.errorRed : 
                                                  Theme.auraCyan 
                                    border.width: 2.5
                                    opacity: status == "syncing" ? 1.0 : 0.7

                                    RotationAnimation on rotation {
                                        running: status == "syncing" 
                                        from: 0; to: 360
                                        duration: 1800
                                        loops: Animation.Infinite
                                    }
                                }
                            }

                            // File icon + name
                            RowLayout {
                                Layout.preferredWidth: 200
                                spacing: 10

                                Rectangle {
                                    width: 30; height: 30; radius: 6
                                    color: meta === ".pdf"    ? "#EF4444"        :
                                           meta === ".zip"    ? "#60A5FA"        :
                                           meta === "folder" ? Theme.successGreen :
                                           meta === ".doc"    ? Theme.auraBlue   : "#F59E0B"

                                    Text {
                                        anchors.centerIn: parent
                                        text: meta === "folder" ? "📁" : meta.toUpperCase()
                                        color: "white"
                                        font.pixelSize: meta === "folder" ? 14 : 9
                                        font.weight: Font.Bold
                                    }
                                }

                                Text {
                                    text: name
                                    color: Theme.textPrimary
                                    font.pixelSize: 13
                                    font.weight: Font.Medium
                                    elide: Text.ElideRight
                                    Layout.preferredWidth: 170
                                }
                            }

                            Text {
                              text: status  == "syncing" ? meta != "folder" ? percentage : status : status // done or error or progress - 0-100% for upload/download
                                color: status == "syncing" ? Theme.textPrimary : Theme.textSecondary
                                font.pixelSize: 12
                                Layout.preferredWidth: 90
                            }

                            Text {
                                text: type // downloading - uploading - folder create - folder move etc
                                color: Theme.textSecondary
                                font.pixelSize: 12
                                Layout.preferredWidth: 90
                            }

                            Text {
                              text: meta == "folder" ? "  --  " : size 
                                color: Theme.textSecondary
                                font.pixelSize: 12
                                Layout.preferredWidth: 90 
                            }
                            Text {
                                text: path
                                color: status == "syncing" ? Theme.auraBlue : Theme.textSecondary
                                font.pixelSize: 12
                                Layout.preferredWidth: 200
                            }
                        }

                        // Bottom progress bar for syncing row
                        Rectangle {
                            anchors.bottom: parent.bottom
                            anchors.left: parent.left
                            anchors.right: parent.right
                            height: 2; radius: 1
                            color: "#1A1D24"
                            visible: status == "syncing"

                            Rectangle {
                                width: parent.width * progress
                                height: 2; radius: 1
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
        }
    }
}
