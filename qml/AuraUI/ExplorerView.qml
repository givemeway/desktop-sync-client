import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AuraUI 1.0

Item {
    id: explorerRoot

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

    // ── Load root on first appearance ─────────────────────────────────────────
    Component.onCompleted: {
        syncController.loadDirectory("/")
    }

    // ── Layout ────────────────────────────────────────────────────────────────
    Column {
        anchors.fill: parent
        spacing: 20

        // ── Header ───────────────────────────────────────────────────────────
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

            // Download selected button
            Rectangle {
                width: 130; height: 34; radius: 8
                color: syncController.selectedIds().length > 0 ? Theme.auraBlue : "#1AFFFFFF"
                border.color: Theme.glassBorder
                border.width: 1
                Layout.alignment: Qt.AlignVCenter
                opacity: syncController.selectedIds().length > 0 ? 1.0 : 0.5
                Row {
                    anchors.centerIn: parent
                    spacing: 6
                    Text {
                        text: "\uf019"
                        font.family: "Font Awesome 6 Free"
                        font.weight: Font.Black
                        font.pixelSize: 12
                        color: "white"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: syncController.selectedIds().length > 0
                              ? "Download (" + syncController.selectedIds().length + ")"
                              : "Download"
                        color: "white"
                        font.pixelSize: 12
                        font.weight: Font.Medium
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    enabled: syncController.selectedIds().length > 0
                    onClicked: {
                        var ids = syncController.selectedIds()
                        for (var i = 0; i < ids.length; i++) {
                            syncController.downloadFile(ids[i])
                        }
                    }
                }
                Behavior on color { ColorAnimation { duration: 150 } }
            }
        }

        // ── Breadcrumb bar ────────────────────────────────────────────────────
        Row {
            width: parent.width
            height: 24
            spacing: 4

            Repeater {
                // pathHistory + currentPath gives us the full breadcrumb trail
                model: syncController.pathHistory.concat([syncController.currentPath])
                Row {
                    spacing: 4
                    Text {
                        text: modelData === "/" ? "All Files" : modelData.split("/").pop()
                        color: index === syncController.pathHistory.length
                               ? Theme.textPrimary : Theme.auraCyan
                        font.pixelSize: 13
                        font.weight: index === syncController.pathHistory.length
                                     ? Font.Medium : Font.Normal
                        anchors.verticalCenter: parent.verticalCenter
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: index < syncController.pathHistory.length
                                         ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: {
                                // clicking a past breadcrumb navigates back to that level
                                if (index < syncController.pathHistory.length) {
                                    syncController.loadDirectory(modelData)
                                }
                            }
                        }
                    }
                    Text {
                        text: "/"
                        color: Theme.textSecondary
                        font.pixelSize: 13
                        visible: index < syncController.pathHistory.length
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }

        // ── File list panel ───────────────────────────────────────────────────
        Rectangle {
            width: parent.width
            height: parent.height - 40 - 24 - 40
            radius: 14
            color: "#0C121F"
            border.color: Theme.glassBorder
            border.width: 1
            clip: true

            Column {
                anchors.fill: parent
                spacing: 0

                // ── Column headers ────────────────────────────────────────────
                Rectangle {
                    width: parent.width
                    height: 40
                    color: "#0A0F1A"
                    radius: 14

                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: 14
                        color: "#0A0F1A"
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        spacing: 0

                        // Select all checkbox
                        Item {
                            Layout.preferredWidth: 36
                            Layout.preferredHeight: 18
                            Layout.alignment: Qt.AlignVCenter
                            Rectangle {
                                width: 18; height: 18; radius: 3
                                anchors.centerIn: parent
                                color: "transparent"
                                border.color: Theme.glassBorder
                                border.width: 1

                                Rectangle {
                                    anchors.fill: parent
                                    anchors.margins: 3
                                    radius: 2
                                    color: Theme.auraBlue
                                    visible: syncController.selectedIds().length ===
                                             syncController.explorerModel.rowCount() &&
                                             syncController.explorerModel.rowCount() > 0
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: syncController.selectAll()
                                }
                            }
                        }

                        Text {
                            text: "Name ↓"
                            color: Theme.textSecondary
                            font.pixelSize: 12
                            font.weight: Font.Medium
                            Layout.fillWidth: true
                        }
                        Text {
                            text: "Size"
                            color: Theme.textSecondary
                            font.pixelSize: 12
                            font.weight: Font.Medium
                            Layout.preferredWidth: 100
                        }
                        Text {
                            text: "Versions"
                            color: Theme.textSecondary
                            font.pixelSize: 12
                            font.weight: Font.Medium
                            Layout.preferredWidth: 90
                        }
                        Text {
                            text: "Modified"
                            color: Theme.textSecondary
                            font.pixelSize: 12
                            font.weight: Font.Medium
                            Layout.preferredWidth: 180
                        }
                        Item { Layout.preferredWidth: 60 }
                    }
                }

                // Separator
                Rectangle {
                    width: parent.width
                    height: 1
                    color: Theme.glassBorder
                }

                // ── Loading overlay — shown while fetchDirectory is in flight ──
                Item {
                    width: parent.width
                    height: parent.height - 41
                    visible: syncController.isExplorerLoading

                    BusyIndicator {
                        anchors.centerIn: parent
                        running: syncController.isExplorerLoading
                    }
                }

                // ── File rows ─────────────────────────────────────────────────
                ListView {
                    width: parent.width
                    height: parent.height - 41
                    spacing: 0
                    clip: true
                    visible: !syncController.isExplorerLoading

                    // ── real C++ model ────────────────────────────────────────
                    model: syncController.explorerModel

                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                    }

                    delegate: Rectangle {
                        id: row
                        width: ListView.view.width
                        height: 50

                        // isSelected comes from ExplorerModel's IsSelectedRole
                        color: isSelected ? "#0F1E35" :
                               hovered    ? "#0A1628" : "transparent"
                        property bool hovered: false

                        Behavior on color { ColorAnimation { duration: 120 } }

                        // Bottom separator
                        Rectangle {
                            anchors.bottom: parent.bottom
                            anchors.left: parent.left
                            anchors.right: parent.right
                            height: 1
                            color: "#0DFFFFFF"
                        }

                        // Left accent for selected rows
                        Rectangle {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: 3
                            color: Theme.auraBlue
                            visible: isSelected
                            radius: 2
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onEntered: row.hovered = true
                            onExited:  row.hovered = false
                            onClicked: {
                                // type comes from ExplorerModel's TypeRole
                                if (type === "folder") {
                                    syncController.loadDirectory(path)
                                }
                            }
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            spacing: 0

                            // Checkbox
                            Item {
                                Layout.preferredWidth: 36
                                Layout.preferredHeight: 18
                                Layout.alignment: Qt.AlignVCenter
                                Rectangle {
                                    width: 18; height: 18; radius: 3
                                    anchors.centerIn: parent
                                    color: isSelected ? Theme.auraBlue : "transparent"
                                    border.color: isSelected ? Theme.auraBlue : Theme.glassBorder
                                    border.width: 1
                                    opacity: row.hovered || isSelected ? 1.0 : 0.4
                                    Behavior on opacity { NumberAnimation { duration: 120 } }

                                    Text {
                                        anchors.centerIn: parent
                                        text: "\uf00c"
                                        font.family: "Font Awesome 6 Free"
                                        font.weight: Font.Black
                                        font.pixelSize: 10
                                        color: "white"
                                        visible: isSelected
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: (mouse) => {
                                            mouse.accepted = true
                                            // id comes from ExplorerModel's IdRole
                                            syncController.toggleSelected(id)
                                        }
                                    }
                                }
                            }

                            // File icon + name
                            Row {
                                spacing: 12
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter

                                Rectangle {
                                    width: 32; height: 32; radius: 6
                                    color: Qt.rgba(
                                        parseInt(explorerRoot.fileIconColor(name, type === "folder").slice(1,3), 16)/255,
                                        parseInt(explorerRoot.fileIconColor(name, type === "folder").slice(3,5), 16)/255,
                                        parseInt(explorerRoot.fileIconColor(name, type === "folder").slice(5,7), 16)/255,
                                        0.15)
                                    anchors.verticalCenter: parent.verticalCenter

                                    Text {
                                        anchors.centerIn: parent
                                        text: explorerRoot.fileIcon(name, type === "folder")
                                        font.family: "Font Awesome 6 Free"
                                        font.weight: Font.Black
                                        font.pixelSize: 15
                                        color: explorerRoot.fileIconColor(name, type === "folder")
                                    }
                                }

                                Column {
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 2

                                    Text {
                                        text: name
                                        color: type === "folder" ? Theme.auraCyan : Theme.textPrimary
                                        font.pixelSize: 13
                                        font.weight: Font.Medium
                                        elide: Text.ElideRight
                                        width: explorerRoot.width - 500
                                    }
                                    Text {
                                        text: type === "folder" ? "Folder" : name.split('.').pop().toUpperCase() + " file"
                                        color: Theme.textSecondary
                                        font.pixelSize: 10
                                        opacity: 0.7
                                    }
                                }
                            }

                            // Size — size role comes as pre-formatted string from ExplorerModel
                            Text {
                                text: size
                                color: Theme.textSecondary
                                font.pixelSize: 12
                                Layout.preferredWidth: 100
                                Layout.alignment: Qt.AlignVCenter
                            }

                            // Versions
                            Item {
                                Layout.preferredWidth: 90
                                Layout.alignment: Qt.AlignVCenter
                                height: 24

                                Rectangle {
                                    visible: versions > 0
                                    width: 28; height: 20; radius: 10
                                    color: "#1AFFFFFF"
                                    anchors.verticalCenter: parent.verticalCenter

                                    Text {
                                        anchors.centerIn: parent
                                        text: versions
                                        color: Theme.textSecondary
                                        font.pixelSize: 11
                                    }
                                }

                                Text {
                                    visible: versions <= 0
                                    text: "--"
                                    color: Theme.textSecondary
                                    font.pixelSize: 12
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            // Modified — lastModified role is pre-formatted string from ExplorerModel
                            Text {
                                text: lastModified
                                color: Theme.textSecondary
                                font.pixelSize: 12
                                Layout.preferredWidth: 180
                                Layout.alignment: Qt.AlignVCenter
                            }

                            // Action buttons — visible on hover
                            Row {
                                spacing: 6
                                Layout.preferredWidth: 60
                                Layout.alignment: Qt.AlignVCenter
                                opacity: row.hovered ? 1.0 : 0.0
                                Behavior on opacity { NumberAnimation { duration: 150 } }

                                // Download button — files only
                                Rectangle {
                                    width: 26; height: 26; radius: 6
                                    color: "#1A3B82F6"
                                    visible: type !== "folder"

                                    Text {
                                        anchors.centerIn: parent
                                        text: "\uf019"
                                        font.family: "Font Awesome 6 Free"
                                        font.weight: Font.Black
                                        font.pixelSize: 11
                                        color: Theme.auraBlue
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: (mouse) => {
                                            mouse.accepted = true
                                            syncController.downloadFile(id)
                                        }
                                    }
                                }

                                // Delete button
                                Rectangle {
                                    width: 26; height: 26; radius: 6
                                    color: "#15FFFFFF"

                                    Text {
                                        anchors.centerIn: parent
                                        text: "\uf2ed"
                                        font.family: "Font Awesome 6 Free"
                                        font.weight: Font.Black
                                        font.pixelSize: 11
                                        color: "#EF4444"
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: (mouse) => {
                                            mouse.accepted = true
                                            if (type === "folder") {
                                                syncController.deleteFolder(path)
                                            } else {
                                                syncController.deleteFile(id)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
