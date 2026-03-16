import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AuraUI 1.0

Item {
    id: activityRoot

    // ── Status icon helper ────────────────────────────────────────────────────
    function statusIcon(s) {
        switch(s) {
            case "syncing":     return "\uf021"  // rotate/sync
            case "queued":      return "\uf017"  // clock
            case "done":        return "\uf058"  // check circle
            case "error":       return "\uf057"  // x circle
            default:            return "\uf111"  // circle
        }
    }

    function statusColor(s) {
        switch(s) {
            case "syncing":     return Theme.auraCyan
            case "queued":      return Theme.auraBlue
            case "done":        return Theme.successGreen
            case "error":       return Theme.errorRed
            default:            return Theme.textSecondary
        }
    }

    // ── File type icon helper ─────────────────────────────────────────────────
    function fileIcon(m) {
        switch(m) {
            case "folder":  return "\uf07c"  // folder open
            case ".pdf":    return "\uf1c1"  // pdf
            case ".zip":    return "\uf1c6"  // archive
            case ".doc":    return "\uf1c2"  // word
            default:        return "\uf15b"  // generic file
        }
    }
    
    function fileIconColor(m){
       switch(s){
            case "folder":  return "#181A20"  // bgDark
            case ".pdf":    return "#EF4444"  // pdf
            case ".zip":    return "#60A5FA"  // archive
            case ".doc":    return "#181A20"  // word
            default:        return "#181A20"  // generic file
      }
    }

    function typeIcon(m){
      switch(m){
        case "upload"              : return "\uf093"
        case "download"            : return "\uf019"
        case "local_folder_create" : return "\uf65e"
        case "cloud_folder_create" : return "\uf65e"
        case "local_delete"        : return "\uf1f8"
        case "cloud_delete"        : return "\uf1f8"
        case "local_move"          : return "\uf362"
        case "cloud_move"          : return "\uf362"
        case "cloud_rename"        : return "\uf044"
        case "local_rename"        : return "\uf044"
        default : return "unknown"
      }
    }
    function typeColor(m){
      switch(m){
        case "upload"              : return Theme.auraCyan
        case "download"            : return Theme.auraBlue 
        case "local_folder_create" : return Theme.successGreen
        case "cloud_folder_create" : return Theme.successGreen
        case "local_delete"        : return Theme.errorRed
        case "cloud_delete"        : return Theme.errorRed
        case "local_move"          : return "#BB88FF"
        case "cloud_move"          : return "#BB88FF"
        case "cloud_rename"        : return "#BB88FF"
        case "local_rename"        : return "#BB88FF"
        default                    : return Theme.textSecondary
       }
    }
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
                    Text {
                        text: "\uf002"
                        font.family: "Font Awesome 6 Free"
                        font.weight: Font.Black
                        font.pixelSize: 13
                        color: Theme.textSecondary
                    }
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

                        Behavior on color { ColorAnimation { duration: 150 }}

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 14

                            // Status indicator
                            Item {
                                Layout.preferredWidth: 36
                                Layout.preferredHeight: 36

                                Text {
                                    id: statusIconText
                                    anchors.centerIn: parent
                                    text: activityRoot.statusIcon(status)
                                    font.family: "Font Awesome 7 Free"
                                    font.weight: Font.Black
                                    font.pixelSize: 16
                                    color: activityRoot.statusColor(status)

                                    RotationAnimation on rotation {
                                        running: status === "syncing"
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
                                  color: "transparent"
                                  Text {
                                        anchors.centerIn: parent
                                        text: activityRoot.fileIcon(meta)
                                        font.family: "Font Awesome 7 Free"
                                        font.weight: Font.Black
                                        font.pixelSize: 16
                                        color: "white"
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
                                  text: status == "syncing" ? meta != "folder" ? percentage : status : status
                                  color: status == "syncing" ? Theme.textPrimary : Theme.textSecondary
                                  font.pixelSize: 12
                                  Layout.preferredWidth: 90
                              }
                            Text {
                                text: activityRoot.typeIcon(type) // downloading - uploading - folder create - folder move etc
                                color: activityRoot.typeColor(type)
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
