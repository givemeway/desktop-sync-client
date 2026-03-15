import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AuraUI 1.0

Item {

    Column {
        anchors.fill: parent
        spacing: 28

        Text {
            text: "Settings"
            color: Theme.textPrimary
            font.pixelSize: 26
            font.weight: Font.Medium
        }

        // Settings panel
        Rectangle {
            width: parent.width
            height: parent.height - 60
            radius: 14
            color: "#0C121F"
            border.color: Theme.glassBorder
            border.width: 1

            Column {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 0

                Repeater {
                    model: [
                        { label: "Dark Mode",        sub: "Use dark interface theme",       on: true  },
                        { label: "Auto Sync",         sub: "Sync files automatically",       on: true  },
                        { label: "Bandwidth Limit",   sub: "Throttle upload/download speed", on: false },
                        { label: "Notifications",     sub: "Show desktop notifications",     on: true  }
                    ]

                    delegate: Column {
                        width: parent.width
                        spacing: 0

                        RowLayout {
                            width: parent.width
                            height: 64

                            Column {
                                Layout.fillWidth: true
                                spacing: 3
                                Text {
                                    text: modelData.label
                                    color: Theme.textPrimary
                                    font.pixelSize: 14
                                    font.weight: Font.Medium
                                }
                                Text {
                                    text: modelData.sub
                                    color: Theme.textSecondary
                                    font.pixelSize: 11
                                }
                            }

                            Switch {
                                checked: modelData.on
                                Layout.alignment: Qt.AlignVCenter

                                indicator: Rectangle {
                                    implicitWidth: 44
                                    implicitHeight: 24
                                    radius: 12
                                    color: parent.checked ? Theme.auraCyan : "#2A2D3A"
                                    border.color: parent.checked ? Theme.auraCyan : Theme.glassBorder
                                    border.width: 1

                                    Behavior on color { ColorAnimation { duration: 150 } }

                                    Rectangle {
                                        x: parent.checked ? parent.width - width - 3 : 3
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 18; height: 18; radius: 9
                                        color: "white"

                                        Behavior on x { NumberAnimation { duration: 150; easing.type: Easing.OutQuad } }
                                    }
                                }
                            }
                        }

                        // Divider (except after last)
                        Rectangle {
                            width: parent.width; height: 1
                            color: Theme.glassBorder
                            visible: index < 3
                        }
                    }
                }
            }
        }
    }
}
