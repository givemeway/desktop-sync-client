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
    
    function fileIcon(filename, isFolder) {
            if (isFolder) return "\uf07c"
            var ext = filename.split('.').pop().toLowerCase()
            switch(ext) {
                case "pdf":                     return "\uf1c1"
                case "zip": case "rar":         return "\uf1c6"
                case "doc": case "docx":        return "\uf1c2"
                case "xls": case "xlsx":        return "\uf1c3"
                case "ppt": case "pptx":        return "\uf1c4"
                case "png": case "jpg":
                case "jpeg": case "gif":        return "\uf1c5"
                case "mp4": case "mov":
                case "avi":                     return "\uf1c8"
                case "mp3": case "wav":         return "\uf1c7"
                case "js": case "ts":
                case "py": case "cpp":
                case "h":  case "qml":          return "\uf1c9"
                case "html": case "htm":        return "\uf13b"
                case "md":                      return "\uf15c"
                case "dll":                     return "\uf013"
                case "json":                    return "\uf1c9"
                default:                        return "\uf15b"
            }
          }    
    function fileIconColor(filename, isFolder) {
            if (isFolder) return "#60A5FA"
            var ext = filename.split('.').pop().toLowerCase()
            switch(ext) {
                case "pdf":                     return "#EF4444"
                case "zip": case "rar":         return "#F59E0B"
                case "doc": case "docx":        return "#3B82F6"
                case "xls": case "xlsx":        return "#10B981"
                case "ppt": case "pptx":        return "#F97316"
                case "png": case "jpg":
                case "jpeg": case "gif":        return "#8B5CF6"
                case "mp4": case "mov":         return "#EC4899"
                case "mp3": case "wav":         return "#06B6D4"
                case "js": case "ts":
                case "py": case "cpp":
                case "h":  case "qml":          return "#84CC16"
                case "html":                    return "#F97316"
                case "dll":                     return "#A0AEC0"
                default:                        return "#A0AEC0"
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

    function relativeTime(unixSeconds) {
        const now = Math.floor(Date.now() / 1000)
        let diff = now - parseInt(unixSeconds)

        if (diff < 0)
            diff = 0

        if (diff < 60)
            return "now"

        const minute = 60
        const hour = 3600
        const day = 86400
        const week = 7 * day
        const month = 30 * day
        const year = 365 * day

        if (diff < hour) {
            const n = Math.floor(diff / minute)
            return n + (n === 1 ? " min ago" : " min ago")
        }

        if (diff < day) {
            const n = Math.floor(diff / hour)
            return n + (n === 1 ? " hour ago" : " hours ago")
        }

        if (diff < week) {
            const n = Math.floor(diff / day)
            return n + (n === 1 ? " day ago" : " days ago")
        }

        if (diff < month) {
            const n = Math.floor(diff / week)
            return n + (n === 1 ? " week ago" : " weeks ago")
        }

        if (diff < year) {
            const n = Math.floor(diff / month)
            return n + (n === 1 ? " month ago" : " months ago")
        }

        const n = Math.floor(diff / year)
        return n + (n === 1 ? " year ago" : " years ago")
    }
    function typeName(m,u){
      switch(m){
        case "upload"              : return "Added " + relativeTime(u) 
        case "download"            : return "Added " + relativeTime(u)
        case "local_folder_create" : return "Added " + relativeTime(u)
        case "cloud_folder_create" : return "Added " + relativeTime(u)
        case "local_delete"        : return "Deleted " + relativeTime(u)
        case "cloud_delete"        : return "Deleted " + relativeTime(u)
        case "local_move"          : return "Moved " + relativeTime(u)
        case "cloud_move"          : return "Moved " +  relativeTime(u)
        case "cloud_rename"        : return "Renamed " + relativeTime(u)
        case "local_rename"        : return "Renamed " + relativeTime(u)
        default : return "unknown"
      }
    }
    function getStatusText(status,meta,percentage,type,lastUpdated){
      if(status == "syncing" && meta != "folder"){
          return percentage;
      }
      if(status == "syncing" && meta == "folder"){
        return "syncing"
      }
      if(status == "queued")
        return "queued";
      if(status == "done"){
        return typeName(type,lastUpdated);
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
            /*
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
              */
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
                /*    
                 *    RowLayout {
                    width: parent.width
                    height: 28
                    Layout.alignment: Qt.AlignLeft
                    //Text { text: "Status";   horizontalAlignment: Text.AlignLeft;width: 50;    color: Theme.textSecondary; font.pixelSize: 13; Layout.preferredWidth: 50 }
                    Text { text: "Name";      color: Theme.textSecondary; font.pixelSize: 12; Layout.preferredWidth: 250  }
                    Text { text: "Size";      color: Theme.textSecondary; font.pixelSize: 12; Layout.preferredWidth: 90 }
                  }
                Rectangle { width: parent.width; height: 1; color: Theme.glassBorder }
                */
                // File rows
                ListView {
                    width: parent.width
                    height: parent.height - 40
                    spacing: 6
                    clip: true
                    model: syncController.activityModel

                    section.property: "group"
                        section.criteria: ViewSection.FullString

                        section.delegate: Rectangle {
                            width: ListView.view.width
                            height: 30
                            color: "transparent"

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 6
                                anchors.verticalCenter: parent.verticalCenter
                                text: section
                                color: Theme.textSecondary
                                font.pixelSize: 11
                                font.weight: Font.Bold
                            }
                        }

                    ScrollBar.vertical: ScrollBar {
                      policy: ScrollBar.AsNeeded    // only shows when content overflows
                    }
                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 75
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
                            height: parent.height
                            spacing: 1
                            Layout.alignment: Qt.AlignVCenter
                            // File icon + name
                            RowLayout {
                                Layout.preferredWidth: 250
                                width: 250
                                Layout.alignment: Qt.AlignVCenter
                                height: parent.height 
                                spacing: 1
                                // Status indicator
                                Item {
                                    width: 50
                                    Layout.preferredWidth: 50
                                    Layout.preferredHeight: parent.height
                                    Layout.alignment: Qt.AlignVCenter
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
                                Rectangle {
                                  width: 50;
                                  height: parent.height;
                                  radius: 6
                                  color: "transparent"
                                  Text {
                                          anchors.centerIn: parent
                                          text: activityRoot.fileIcon(name, meta === "folder")
                                          font.family: "Font Awesome 7 Free"
                                          font.weight: Font.Black
                                          font.pixelSize: 36 
                                          color: activityRoot.fileIconColor(name, meta === "folder")
                                      }
                                }

                                Column {
                                    spacing: 2
                                    Layout.preferredWidth: 170
                                    Text {
                                        text: name
                                        color: type === "folder" ? Theme.auraCyan : Theme.textPrimary
                                        font.pixelSize: 13
                                        font.weight: Font.Medium
                                        elide: Text.ElideRight
                                      }
                                    Text {
                                        text: path
                                        color: Theme.textSecondary
                                        font.pixelSize: 10
                                        opacity: 0.7
                                        wrapMode: Text.NoWrap
                                        elide: Text.ElideRight
                                        maximumLineCount: 1
                                        clip: true
                                    }
                                    Text {
                                        text: activityRoot.getStatusText(status,meta,percentage,type,lastUpdated)
                                        color: status == "syncing" ? Theme.textPrimary : Theme.textSecondary 
                                        font.pixelSize: 10
                                        opacity: 0.7
                                        wrapMode: Text.NoWrap
                                        elide: Text.ElideRight
                                        maximumLineCount: 1
                                        clip: true
 
                                      }
                                }
                            }
                            Text {
                              text: meta == "folder" ? "  --  " : size 
                                color: Theme.textSecondary
                                font.pixelSize: 12
                                Layout.preferredWidth: 90 
                            }
                        }
                        // Bottom progress bar for syncing row
                        Rectangle {
                            anchors.bottom: parent.bottom
                            anchors.left: parent.left
                            anchors.right: parent.right
                            height: 2; radius: 1
                            color: "#1A1D24"
                            visible: status == "syncing" && meta != "folder"

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
