import QtQuick
import QtQuick.Controls
import QtQuick.Effects

Window {
    width: 1000
    height: 700
    visible: true
    title: qsTr("Nexus Sync Engine")
    color: "#050505" // Deep dark background

    // Background Gradient
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#0a0a0a" }
            GradientStop { position: 1.0; color: "#000000" }
        }
    }

    // Glassmorphism Sidebar
    Rectangle {
        id: sideBar
        width: 250
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        color: "#15ffffff" // Very subtle white overlay
        border.color: "#20ffffff"
        border.width: 1
        
        // Add Blur effect here if using MultiEffect from QtQuick.Effects (Qt 6.5+)
        
        Column {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 30

            Text {
                text: "NEXUS"
                color: "#00aaff"
                font.pixelSize: 24
                font.bold: true
                letterSpacing: 4
            }

            Rectangle { width: parent.width; height: 1; color: "#20ffffff" }

            // Menu Items
            Column {
                spacing: 15
                width: parent.width
                
                Repeater {
                    model: ["Dashboard", "File Explorer", "Shared", "Settings"]
                    delegate: Text {
                        text: modelData
                        color: index === 0 ? "white" : "#88ffffff"
                        font.pixelSize: 16
                        font.weight: index === 0 ? Font.DemiBold : Font.Normal
                        
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onEntered: parent.color = "white"
                            onExited: if(index !== 0) parent.color = "#88ffffff"
                        }
                    }
                }
            }
        }
    }

    // Main Content
    Item {
        anchors.left: sideBar.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 40

        // Status Header
        Row {
            anchors.top: parent.top
            spacing: 20
            
            Text {
                text: syncController.status
                color: "white"
                font.pixelSize: 32
                font.weight: Font.Light
            }
            
            Rectangle {
                width: 10; height: 10
                radius: 5
                anchors.verticalCenter: parent.verticalCenter
                color: "#00ff88"
                
                SequentialAnimation on opacity {
                    loops: Animation.Infinite
                    NumberAnimation { from: 1.0; to: 0.3; duration: 1000; easing.type: Easing.InOutQuad }
                    NumberAnimation { from: 0.3; to: 1.0; duration: 1000; easing.type: Easing.InOutQuad }
                }
            }
        }

        // The "Sync Orb" (Simplified for initial version)
        Rectangle {
            anchors.centerIn: parent
            width: 300; height: 300
            radius: 150
            color: "transparent"
            border.color: "#10ffffff"
            border.width: 2

            // Glowing Outer Ring
            Rectangle {
                anchors.fill: parent
                anchors.margins: -10
                radius: 160
                color: "transparent"
                border.color: "#00aaff"
                border.width: 1
                opacity: 0.2
            }

            Text {
                anchors.centerIn: parent
                text: Math.round(syncController.progress * 100) + "%"
                color: "white"
                font.pixelSize: 48
                font.weight: Font.ExtraLight
            }
        }
    }
}
