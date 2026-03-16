import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import AuraUI 1.0

Item {
    id: root

    // ── Exposed state — bind these to your C++ SyncController ────────────────
    // In your C++ backend expose these as Q_PROPERTYs and set via context property
    // e.g.  engine.rootContext()->setContextProperty("SyncController", &ctrl);
    // Then replace the local properties below with SyncController.syncRunning etc.
    property bool   syncRunning:  false
    property string syncFolder:   Qt.resolvedUrl("~/Documents/AuraSync")
    property int    uploadSpeed:  0     // KB/s — feed from backend
    property int    downloadSpeed: 0    // KB/s — feed from backend
    property string loggedInUser: ""    // set from main.qml after login

    // ── Layout ────────────────────────────────────────────────────────────────
    Column {
        anchors.fill: parent
        spacing: 28

        // Page title
        Column {
            spacing: 4
            Text {
                text: "Settings"
                font.pixelSize: 24
                font.weight: Font.Bold
                color: Theme.textPrimary
            }
            Text {
                text: "Configure sync, account, and preferences"
                font.pixelSize: 13
                color: Theme.textSecondary
            }
        }

        // ── Sync Control Card ─────────────────────────────────────────────────
        SettingsCard {
            width: parent.width
            title: "Sync Control"
            iconCode: FA.arrowsRotate
            iconColor: root.syncRunning ? Theme.auraCyan : Theme.textSecondary

            content: Column {
                spacing: 16
                width: parent.width

                // Status row
                Row {
                    spacing: 12
                    width: parent.width

                    // Animated status dot
                    Rectangle {
                        width: 10; height: 10; radius: 5
                        anchors.verticalCenter: parent.verticalCenter
                        color: root.syncRunning ? Theme.successGreen : "#555"

                        SequentialAnimation on opacity {
                            running: root.syncRunning
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.3; duration: 700 }
                            NumberAnimation { to: 1.0; duration: 700 }
                        }
                    }

                    Text {
                        text: root.syncRunning ? "Sync is running" : "Sync is stopped"
                        color: root.syncRunning ? Theme.successGreen : Theme.textSecondary
                        font.pixelSize: 14
                        anchors.verticalCenter: parent.verticalCenter
                    }

             //       Item { width: 1; height: 1; Layout.fillWidth: true }

                    // Speed badges (visible while syncing)
                    Row {
                        spacing: 8
                        visible: root.syncRunning
                        anchors.verticalCenter: parent.verticalCenter

                        SpeedBadge { icon: FA.cloudArrowUp;   color: Theme.auraCyan;  value: root.uploadSpeed }
                        SpeedBadge { icon: FA.cloudArrowDown; color: Theme.auraBlue;  value: root.downloadSpeed }
                    }
                }

                // Start / Stop button
                Rectangle {
                    width: 180; height: 44; radius: 12
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop {
                            position: 0
                            color: root.syncRunning ? Theme.errorRed : Theme.auraCyan
                            Behavior on color { ColorAnimation { duration: 250 } }
                        }
                        GradientStop {
                            position: 1
                            color: root.syncRunning ? "#AA2020" : Theme.auraBlue
                            Behavior on color { ColorAnimation { duration: 250 } }
                        }
                    }
                    opacity: syncToggleMa.pressed ? 0.75 : 1
                    scale:   syncToggleMa.pressed ? 0.97 : 1
                    Behavior on scale   { NumberAnimation { duration: 80 } }
                    Behavior on opacity { NumberAnimation { duration: 80 } }

                    Row {
                        anchors.centerIn: parent
                        spacing: 10
                        Text {
                            text: root.syncRunning ? FA.stop : FA.play
                            font.family: FA.solid
                            font.pixelSize: 14
                            color: "white"
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: root.syncRunning ? "Stop Sync" : "Start Sync"
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            color: "white"
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    MouseArea {
                        id: syncToggleMa
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.syncRunning = !root.syncRunning
                            // call your backend: SyncController.setRunning(root.syncRunning)
                        }
                    }
                }
            }
        }

        // ── Sync Location Card ────────────────────────────────────────────────
        SettingsCard {
            width: parent.width
            title: "Sync Location"
            iconCode: FA.folderOpen
            iconColor: Theme.auraBlue

            content: Column {
                spacing: 12
                width: parent.width

                Text {
                    text: "Local folder that Aura keeps in sync with the cloud."
                    font.pixelSize: 12
                    color: Theme.textSecondary
                }

                // Path display + browse button
                Row {
                    width: parent.width
                    spacing: 12

                    Rectangle {
                        width: parent.width - browseBtn.width - 12
                        height: 44; radius: 10
                        color: "#12141E"
                        border.color: Theme.glassBorder
                        border.width: 1
                        clip: true

                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 10

                            Text {
                                text: FA.folderOpen
                                font.family: FA.solid
                                font.pixelSize: 14
                                color: Theme.auraBlue
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                text: root.syncFolder
                                font.pixelSize: 13
                                color: Theme.textPrimary
                                anchors.verticalCenter: parent.verticalCenter
                                elide: Text.ElideLeft
                                width: parent.width - 30
                            }
                        }
                    }

                    Rectangle {
                        id: browseBtn
                        width: 100; height: 44; radius: 10
                        color: "#1E2130"
                        border.color: Theme.glassBorder
                        border.width: 1
                        opacity: browseMa.pressed ? 0.7 : 1

                        Row {
                            anchors.centerIn: parent
                            spacing: 8
                            Text {
                                text: FA.folderPlus
                                font.family: FA.solid
                                font.pixelSize: 13
                                color: Theme.auraCyan
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: "Browse"
                                font.pixelSize: 13
                                color: Theme.textPrimary
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        MouseArea {
                            id: browseMa
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: folderDialog.open()
                        }
                    }
                }

                // Warn if sync is running when folder is changed
                Row {
                    spacing: 6
                    visible: root.syncRunning
                    Text {
                        text: FA.triangleExclaim
                        font.family: FA.solid
                        font.pixelSize: 12
                        color: Theme.warningYellow
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: "Stop sync before changing the folder."
                        font.pixelSize: 12
                        color: Theme.warningYellow
                    }
                }
            }
        }

        // ── Account Card ──────────────────────────────────────────────────────
        SettingsCard {
            width: parent.width
            title: "Account"
            iconCode: FA.userCircle
            iconColor: Theme.textSecondary

            content: Row {
                spacing: 16
                width: parent.width

                Rectangle {
                    width: 44; height: 44; radius: 22
                    color: "#2A2D3E"
                    Text {
                        anchors.centerIn: parent
                        text: FA.userCircle
                        font.family: FA.solid
                        font.pixelSize: 22
                        color: Theme.textSecondary
                    }
                }

                Column {
                    spacing: 3
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        text: root.loggedInUser !== "" ? root.loggedInUser : "Not signed in"
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        color: Theme.textPrimary
                    }
                    Text {
                        text: "Free plan · 5 GB"
                        font.pixelSize: 12
                        color: Theme.textSecondary
                    }
                }

                Item { width: 1; height: 1 }   // spacer placeholder

                // Sign out button
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 90; height: 34; radius: 8
                    color: "#1E2130"
                    border.color: Theme.glassBorder
                    border.width: 1
                    opacity: signOutMa.pressed ? 0.7 : 1

                    Text {
                        anchors.centerIn: parent
                        text: "Sign out"
                        font.pixelSize: 12
                        color: Theme.errorRed
                    }

                    MouseArea {
                        id: signOutMa
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: signOutDialog.open()
                    }
                }
            }
        }
    }

    // ── Folder picker dialog ──────────────────────────────────────────────────
    FolderDialog {
        id: folderDialog
        title: "Choose Sync Folder"
        onAccepted: {
            root.syncFolder = selectedFolder
            // SyncController.setSyncFolder(selectedFolder)
        }
    }

    // ── Sign out confirmation ─────────────────────────────────────────────────
    Dialog {
        id: signOutDialog
        title: "Sign out"
        modal: true
        anchors.centerIn: Overlay.overlay
        background: Rectangle {
            color: "#1A1D2A"
            border.color: Theme.glassBorder
            border.width: 1
            radius: 14
        }
        contentItem: Column {
            spacing: 16
            padding: 8
            Text {
                text: "Are you sure you want to sign out?\nSync will be stopped."
                font.pixelSize: 13
                color: Theme.textSecondary
                wrapMode: Text.WordWrap
            }
            Row {
                spacing: 12
                anchors.right: parent.right

                Rectangle {
                    width: 80; height: 36; radius: 8
                    color: "#2A2D3E"
                    border.color: Theme.glassBorder; border.width: 1
                    Text { anchors.centerIn: parent; text: "Cancel"; font.pixelSize: 13; color: Theme.textPrimary }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: signOutDialog.close() }
                }

                Rectangle {
                    width: 90; height: 36; radius: 8
                    color: Theme.errorRed
                    Text { anchors.centerIn: parent; text: "Sign out"; font.pixelSize: 13; font.weight: Font.Medium; color: "white" }
                    MouseArea {
                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            signOutDialog.close()
                            root.syncRunning = false
                            // emit signal / call SyncController.signOut()
                        }
                    }
                }
            }
        }
    }

    // ── Helper components (inline, no extra files needed) ─────────────────────

    component SettingsCard: Rectangle {
        property string title: ""
        property string iconCode: ""
        property color  iconColor: Theme.textSecondary
        property Item content

        height: cardInner.implicitHeight + 40
        color: "#1A1D2A"
        border.color: Theme.glassBorder
        border.width: 1
        radius: 14

        Column {
            id: cardInner
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 20
            spacing: 14

            // Card header
            Row {
                spacing: 10
                Text {
                    text: iconCode
                    font.family: FA.solid
                    font.pixelSize: 15
                    color: iconColor
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: title
                    font.pixelSize: 14
                    font.weight: Font.SemiBold
                    color: Theme.textPrimary
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // Divider
            Rectangle { width: parent.width; height: 1; color: Theme.glassBorder; opacity: 0.5 }

            // Slot for content
            Item {
                width: parent.width
                height: content ? content.implicitHeight : 0
                Component.onCompleted: { if (content) content.parent = this }
            }
        }
    }

    component SpeedBadge: Rectangle {
        property string icon: ""
        //property color  color: Theme.auraCyan
        property int    value: 0

        width: 80; height: 26; radius: 6
        color: Qt.rgba(parent.color.r, parent.color.g, parent.color.b, 0.12)
        border.color: Qt.rgba(parent.color.r, parent.color.g, parent.color.b, 0.25)
        border.width: 1

        Row {
            anchors.centerIn: parent
            spacing: 5
            Text {
                text: icon
                font.family: FA.solid
                font.pixelSize: 11
                color: parent.parent.color
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: value + " KB/s"
                font.pixelSize: 11
                color: parent.parent.color
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
}
