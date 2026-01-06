import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root
    width: 420
    implicitHeight: contentLayout.implicitHeight + 2
    height: implicitHeight

    property string sessionName: ""
    property string playbackState: ""
    property real progress: 0
    property real duration: 0
    property real position: 0
    property bool hexAudioEnabled: false
    property real dragProgress: -1
    property bool loopEnabled: false

    signal playRequested()
    signal pauseRequested()
    signal stopRequested()
    signal hexMonitorToggled(bool enabled)
    signal seekRequested(real normalized)
    signal loopToggled(bool enabled)

    focus: false

    // Background removed per request to keep overlay floating without fill/border

    ColumnLayout {
        id: contentLayout
        anchors.fill: parent
        anchors.margins: 2
        anchors.bottomMargin: 4
        spacing: 2

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Text {
                text: sessionName.length > 0 ? sessionName : "Recorded Session"
                color: "#FFFFFF"
                font.pixelSize: 17
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.fillWidth: false
                Layout.maximumWidth: Math.max(0, contentLayout.width - hexToggle.implicitWidth - 16)
            }

            Switch {
                id: hexToggle
                font.pixelSize: 13
                checked: root.hexAudioEnabled
                onToggled: root.hexMonitorToggled(checked)
                Layout.alignment: Qt.AlignVCenter
            }

            Item { Layout.fillWidth: true }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            RowLayout {
                spacing: 4
                Layout.alignment: Qt.AlignVCenter

                Button {
                    id: playButton
                    implicitWidth: 40
                    implicitHeight: 34
                    padding: 0
                    focusPolicy: Qt.NoFocus
                    background: Rectangle {
                        anchors.fill: parent
                        radius: 0
                        color: (playButton.down || root.playbackState === "Playing") ? "#16a34a" : "transparent"
                        border.width: 0
                    }
                    contentItem: Image {
                        anchors.centerIn: parent
                        source: Qt.resolvedUrl("../assets/icons/lucide-play.svg")
                        sourceSize.width: 18
                        sourceSize.height: 18
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                    }
                    onClicked: root.playRequested()
                }

                Button {
                    id: pauseButton
                    implicitWidth: 40
                    implicitHeight: 34
                    padding: 0
                    focusPolicy: Qt.NoFocus
                    background: Rectangle {
                        anchors.fill: parent
                        radius: 0
                        color: (pauseButton.down || root.playbackState === "Paused") ? "#facc15" : "transparent"
                        border.width: 0
                    }
                    contentItem: Image {
                        anchors.centerIn: parent
                        source: Qt.resolvedUrl("../assets/icons/lucide-pause.svg")
                        sourceSize.width: 18
                        sourceSize.height: 18
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                    }
                    onClicked: root.pauseRequested()
                }

                Button {
                    id: stopButton
                    implicitWidth: 40
                    implicitHeight: 34
                    padding: 0
                    focusPolicy: Qt.NoFocus
                    background: Rectangle {
                        anchors.fill: parent
                        radius: 0
                        color: stopButton.down ? "#dc2626" : "transparent"
                        border.width: 0
                    }
                    contentItem: Image {
                        anchors.centerIn: parent
                        source: Qt.resolvedUrl("../assets/icons/lucide-stop.svg")
                        sourceSize.width: 18
                        sourceSize.height: 18
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                    }
                    onClicked: root.stopRequested()
                }
                Button {
                    id: loopButton
                    implicitWidth: 40
                    implicitHeight: 34
                    padding: 0
                    focusPolicy: Qt.NoFocus
                    ToolTip.visible: hovered
                    ToolTip.text: root.loopEnabled ? "Looping on" : "Looping off"
                    background: Rectangle {
                        anchors.fill: parent
                        radius: 0
                        color: root.loopEnabled ? "#2563eb" : "transparent"
                        border.width: 0
                    }
                    contentItem: Image {
                        anchors.centerIn: parent
                        source: Qt.resolvedUrl("../assets/icons/lucide-repeat.svg")
                        sourceSize.width: 18
                        sourceSize.height: 18
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        opacity: root.loopEnabled ? 1.0 : 0.6
                    }
                    onClicked: root.loopToggled(!root.loopEnabled)
                }
            }

            ColumnLayout {
                spacing: 4
                Layout.fillWidth: true

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Rectangle {
                        id: bar
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        height: 6
                        radius: 3
                        color: "#1A1A1A"

                        function handleSeek(px, commit) {
                            var ratio = width > 0 ? Math.max(0, Math.min(1, px / width)) : 0
                            if (commit) {
                                root.seekRequested(ratio)
                                root.dragProgress = -1
                            } else {
                                root.dragProgress = ratio
                            }
                        }

                        property real effectiveProgress: {
                            var value = root.dragProgress >= 0 ? root.dragProgress : root.progress
                            if (isNaN(value))
                                value = 0
                            return Math.max(0, Math.min(1, value))
                        }

                        Rectangle {
                            width: Math.max(0, Math.min(parent.width, parent.width * bar.effectiveProgress))
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            radius: 3
                            color: "#62DAFF"
                        }

                        Rectangle {
                            width: 2
                            height: parent.height + 6
                            radius: 1
                            color: "#F0F0F0"
                            anchors.verticalCenter: parent.verticalCenter
                            x: Math.max(0, Math.min(parent.width - width, parent.width * bar.effectiveProgress - width / 2))
                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.SizeHorCursor
                                property bool dragging: false
                                acceptedButtons: Qt.LeftButton
                                onPressed: {
                                    dragging = true
                                    bar.handleSeek(parent.x + mouse.x, false)
                                }
                                onPositionChanged: if (dragging) bar.handleSeek(parent.x + mouse.x, false)
                                onReleased: {
                                    if (dragging)
                                        bar.handleSeek(parent.x + mouse.x, true)
                                    dragging = false
                                }
                                onCanceled: {
                                    dragging = false
                                    root.dragProgress = -1
                                }
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            property bool dragging: false
                            onPressed: {
                                dragging = true
                                bar.handleSeek(mouse.x, false)
                            }
                            onPositionChanged: {
                                if (dragging)
                                    bar.handleSeek(mouse.x, false)
                            }
                            onReleased: {
                                if (dragging)
                                    bar.handleSeek(mouse.x, true)
                                dragging = false
                            }
                            onCanceled: {
                                dragging = false
                                root.dragProgress = -1
                            }
                            onClicked: bar.handleSeek(mouse.x, true)
                        }
                    }
                }

                Text {
                    id: durationLabel
                    color: "#D0D0D0"
                    font.pixelSize: 12
                    Layout.fillWidth: true
                    text: formatTime(position) + " / " + formatTime(duration)
                }
            }
        }

    }

    function formatTime(value) {
        if (isNaN(value) || value <= 0)
            return "0:00";
        var total = Math.floor(value)
        var minutes = Math.floor(total / 60)
        var seconds = total % 60
        return minutes + ":" + (seconds < 10 ? "0" + seconds : seconds)
    }
}
