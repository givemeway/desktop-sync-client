import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import AuraUI 1.0

Item {
    id: dashboardRoot
  function computeSyncStatus(isSyncing,filesSyncing){
    if(isSyncing && filesSyncing > 0){
      return " Syncing.. " + filesSyncing + " items";
    }
    if(isSyncing && filesSyncing == 0){
      return " Your files are up to date";
    }
    if(!isSyncing){
      return " Sync Disabled";
    }
  }
function syncStatusIcon(isSyncing,filesSyncing){
    if(isSyncing && filesSyncing > 0)
      return "\uf021";
    if(!isSyncing)
      return "((•))";
    if(isSyncing && filesSyncing == 0){
      return "\uf00c";
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
     if(status == "syncing"){
        return "syncing";
      }
      if(status == "queued")
        return "queued";
      if(status == "done"){
        return typeName(type,lastUpdated);
      }
    } // ── Page Title ────────────────────────────────────────────────────────────
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
                    text: (syncController.usagePercentage * 100).toFixed(2) + "%"
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
                    //text: "7.2 TB / 10 TB used"
                    text: syncController.storageUsed + " / " + syncController.quota + " used"
                    color: Theme.textSecondary
                    font.pixelSize: 12
                    anchors.horizontalCenter: parent.horizontalCenter
                }
            }

            // ── Orb + Rings ───────────────────────────────────────────────────
            Item {
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
                                -Math.PI / 2 + Math.PI * 2 * 0.55 )
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
                                -Math.PI / 2 + Math.PI * 2 * 0.45,
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
                        NumberAnimation { to: 1.025; duration: 1500; easing.type: Easing.InOutSine }
                        NumberAnimation { to: 1.0;   duration: 1500; easing.type: Easing.InOutSine }
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
            // ---- Status pill -------------------------------------------------
            Rectangle {
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 12
                anchors.horizontalCenter: parent.horizontalCenter
                width: 250; height: 40
                radius: 15
                color: "#20FFFFFF"
                border.color: Theme.glassBorder
                border.width: 1

                Row {
                    anchors.centerIn: parent
                    spacing: 4
                    Text { text: dashboardRoot.syncStatusIcon(syncController.isSyncing,syncController.filesSyncing);
                          color: Theme.auraPurple; font.pixelSize: 16; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: dashboardRoot.computeSyncStatus(syncController.isSyncing,syncController.filesSyncing); 
                          color: syncController.isSyncing ? Theme.successGreen : Theme.textSecondary; font.pixelSize: 16; anchors.verticalCenter: parent.verticalCenter }
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
                width:parent.width
                height: parent.height
                // Recent Sync Activity header
                Text {
                    text: "Recent Sync Activity"
                    color: Theme.textPrimary
                    font.pixelSize: 15
                    font.weight: Font.Medium
                }
                // Activity list
                /*
                Column {
                    width: parent.width
                    spacing: 14
                    height: 150
                    Repeater {
                        id: recentRepeater
                        model: syncController.activityModel
                        height: parent.height
                        width: parent.width
                        delegate: Column {
                            width: parent.width
                            property int startIndex: Math.max(0,recentRepeater.count - 10)
                            visible: index >=startIndex
                            height: visible ? implicitHeight: 0
                            spacing: visible ? 6:0
                            Row {
                                width: parent.width
                                spacing: 12
                                // File icon
                                Rectangle {
                                    width: 34; height: 34; radius: 8
                                    color : "transparent"
                                    Text {
                                          anchors.centerIn: parent
                                          text: dashboardRoot.fileIcon(name, meta === "folder")
                                          font.family: "Font Awesome 7 Free"
                                          font.weight: Font.Black
                                          font.pixelSize: 24
                                          color: dashboardRoot.fileIconColor(name, meta === "folder")
                                    }
                                }
                                // Name + meta
                                Column {
                                    width: parent.width - 34 - 12 - (status == "syncing" ? 32 : 0)
                                    spacing: 3
                                    anchors.verticalCenter: parent.verticalCenter
                                    Text {
                                        text: name
                                        color: Theme.textPrimary
                                        font.pixelSize: 13
                                        font.weight: Font.Medium
                                        elide: Text.ElideRight
                                        width: parent.width
                                    }
                                    
                                    Text {
                                        text: dashboardRoot.getStatusText(status,meta,percentage,type,lastUpdated)
                                        color: status == "syncing" ? Theme.textPrimary : Theme.textSecondary 
                                        font.pixelSize: 10
                                        opacity: 0.7
                                        wrapMode: Text.NoWrap
                                        elide: Text.ElideRight
                                        maximumLineCount: 1
                                        clip: true
 
                                      }
                                }
                                // Progress % badge
                                Text {
                                    text: percentage
                                    color: Theme.textSecondary
                                    font.pixelSize: 11
                                    visible: status == "syncing" && meta != "folder"
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
                                visible: status == "syncing" && meta != "folder"

                                Rectangle {
                                    width: parent.width * progress
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
                  */
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
