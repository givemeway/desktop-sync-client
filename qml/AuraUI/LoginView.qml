import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AuraUI 1.0

Item {
    id: root
    signal loginAccepted(string username)

    // ── Background ────────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            orientation: Gradient.Diagonal
            GradientStop { position: 0.0; color: "#0E1120" }
            GradientStop { position: 1.0; color: "#12101A" }
        }
    }

    // Ambient blobs
    Rectangle {
        width: 320; height: 320; radius: 160
        x: -80; y: -60
        color: Theme.auraBlue; opacity: 0.08
    }
    Rectangle {
        width: 240; height: 240; radius: 120
        x: parent.width - 140; y: parent.height - 120
        color: Theme.auraCyan; opacity: 0.07
    }

    // ── Card ──────────────────────────────────────────────────────────────────
    Rectangle {
        id: card
        anchors.centerIn: parent
        width: 400
        height: cardColumn.implicitHeight + 64
        radius: 20
        color: "#1A1D2A"
        border.color: Theme.glassBorder
        border.width: 1

        // subtle top shine
        Rectangle {
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            width: 180; height: 1
            color: Theme.auraCyan; opacity: 0.35
        }

        Column {
            id: cardColumn
            anchors.centerIn: parent
            width: parent.width - 64
            spacing: 0

            // ── Logo / branding ───────────────────────────────────────────────
            Item { width: 1; height: 8 }

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 52; height: 52; radius: 26
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: Theme.auraCyan }
                    GradientStop { position: 1; color: Theme.auraBlue }
                }

                Text {
                    anchors.centerIn: parent
                    text: FA.arrowsRotate
                    font.family: FA.solid
                    font.pixelSize: 22
                    color: "white"
                }
            }

            Item { width: 1; height: 16 }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "QDrive Cloud Sync"
                font.pixelSize: 22
                font.weight: Font.Bold
                color: Theme.textPrimary
            }

            Item { width: 1; height: 6 }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Sign in to continue"
                font.pixelSize: 13
                color: Theme.textSecondary
            }

            Item { width: 1; height: 32 }

            // ── Error banner ──────────────────────────────────────────────────
            Rectangle {
                id: errorBanner
                width: parent.width
                height: errorText.implicitHeight + 16
                radius: 8
                color: "#3B1A1A"
                border.color: Theme.errorRed
                border.width: 1
                visible: false
                opacity: visible ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 200 } }

                Row {
                    anchors.centerIn: parent
                    spacing: 8
                    Text {
                        text: FA.triangleExclaim
                        font.family: FA.solid
                        font.pixelSize: 13
                        color: Theme.errorRed
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        id: errorText
                        text: "Invalid email or password."
                        font.pixelSize: 13
                        color: Theme.errorRed
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }

            Item { width: 1; height: errorBanner.visible ? 12 : 0 }

            // ── Email field ───────────────────────────────────────────────────
            Text {
                text: "Email"
                font.pixelSize: 12
                font.weight: Font.Medium
                color: Theme.textSecondary
                bottomPadding: 6
            }

            Rectangle {
                id: emailBox
                width: parent.width; height: 44; radius: 10
                color: "#12141E"
                border.color: emailField.activeFocus ? Theme.auraCyan : Theme.glassBorder
                border.width: emailField.activeFocus ? 1.5 : 1
                Behavior on border.color { ColorAnimation { duration: 150 } }

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 10

                    Text {
                        text: FA.userCircle
                        font.family: FA.solid
                        font.pixelSize: 15
                        color: emailField.activeFocus ? Theme.auraCyan : Theme.textSecondary
                        anchors.verticalCenter: parent.verticalCenter
                        Behavior on color { ColorAnimation { duration: 150 } }
                    }

                    TextField {
                        id: emailField
                        width: parent.width - 30
                        height: parent.height
                        placeholderText: "you@example.com"
                        placeholderTextColor: "#40FFFFFF"
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        background: Item {}
                        verticalAlignment: TextInput.AlignVCenter
                        inputMethodHints: Qt.ImhEmailCharactersOnly
                        KeyNavigation.tab: passwordField
                        onAccepted: passwordField.forceActiveFocus()
                    }
                }
            }

            Item { width: 1; height: 16 }

            // ── Password field ────────────────────────────────────────────────
            Text {
                text: "Password"
                font.pixelSize: 12
                font.weight: Font.Medium
                color: Theme.textSecondary
                bottomPadding: 6
            }

            Rectangle {
                id: passwordBox
                width: parent.width; height: 44; radius: 10
                color: "#12141E"
                border.color: passwordField.activeFocus ? Theme.auraCyan : Theme.glassBorder
                border.width: passwordField.activeFocus ? 1.5 : 1
                Behavior on border.color { ColorAnimation { duration: 150 } }

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 10

                    Text {
                        text: FA.lock
                        font.family: FA.solid
                        font.pixelSize: 15
                        color: passwordField.activeFocus ? Theme.auraCyan : Theme.textSecondary
                        anchors.verticalCenter: parent.verticalCenter
                        Behavior on color { ColorAnimation { duration: 150 } }
                    }

                    TextField {
                        id: passwordField
                        width: parent.width - 54
                        height: parent.height
                        placeholderText: "••••••••"
                        placeholderTextColor: "#40FFFFFF"
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        background: Item {}
                        verticalAlignment: TextInput.AlignVCenter
                        echoMode: showPass.checked
                                  ? TextInput.Normal
                                  : TextInput.Password
                        onAccepted: attemptLogin()
                    }

                    // Eye toggle
                    Item {
                        width: 28; height: parent.height
                        CheckBox {
                            id: showPass
                            anchors.centerIn: parent
                            indicator: Text {
                                text: showPass.checked ? FA.eye : FA.eyeSlash
                                font.family: FA.solid
                                font.pixelSize: 15
                                color: showPass.checked
                                       ? Theme.auraCyan
                                       : Theme.textSecondary
                                anchors.centerIn: parent
                            }
                        }
                    }
                }
            }

            Item { width: 1; height: 24 }

            // ── Sign-in button ────────────────────────────────────────────────
            Rectangle {
                id: loginBtn
                width: parent.width; height: 46; radius: 12
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: Theme.auraCyan }
                    GradientStop { position: 1; color: Theme.auraBlue }
                }
                opacity: loginMa.pressed ? 0.75 : 1
                Behavior on opacity { NumberAnimation { duration: 80 } }
                scale: loginMa.pressed ? 0.98 : 1
                Behavior on scale { NumberAnimation { duration: 80 } }

                Row {
                    anchors.centerIn: parent
                    spacing: 10

                    BusyIndicator {
                        id: busyIndicator
                        width: 20; height: 20
                        running: false
                        visible: running
                        anchors.verticalCenter: parent.verticalCenter
                        palette.dark: "white"
                    }

                    Text {
                        text: busyIndicator.running ? "Signing in…" : "Sign In"
                        font.pixelSize: 15
                        font.weight: Font.SemiBold
                        color: "white"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                MouseArea {
                    id: loginMa
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: attemptLogin()
                }
            }

            Item { width: 1; height: 20 }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Forgot password?"
                font.pixelSize: 12
                color: Theme.auraCyan
                opacity: 0.8

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    // hook into your backend as needed
                }
            }

            Item { width: 1; height: 8 }
        }
    }

    // ── Logic ─────────────────────────────────────────────────────────────────
    function attemptLogin() {
        errorBanner.visible = false
        if (emailField.text.trim() === "" || passwordField.text === "") {
            errorText.text = "Please enter your email and password."
            errorBanner.visible = true
            return
        }

        busyIndicator.running = true
        loginTimer.start()
    }

    Timer {
        id: loginTimer
        interval: 900        // simulate auth round-trip
        onTriggered: {
            busyIndicator.running = false
            // ── Replace this condition with your real auth call ──────────────
            const ok = emailField.text.includes("@") && passwordField.text.length >= 4
            if (ok) {
                root.loginAccepted(emailField.text)
            } else {
                errorText.text = "Invalid email or password."
                errorBanner.visible = true
                passwordField.text = ""
                passwordField.forceActiveFocus()
            }
        }
    }
}
