import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: root
    width: 200
    height: 72
    radius: 12
    color: "#3F1010"
    border.color: "#FF4A4A"
    border.width: 1
    opacity: 0.95
    signal stopRequested()

    Row {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 16

        Rectangle {
            id: dot
            width: 24
            height: 24
            radius: 12
            anchors.verticalCenter: parent.verticalCenter
            color: "#FF4A4A"
        }

        Column {
            spacing: 4
            anchors.verticalCenter: parent.verticalCenter
            Text {
                text: "Recording"
                color: "#FFFFFF"
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }
            Text {
                text: "Press Ctrl + R to stop"
                color: "#F4BBBB"
                font.pixelSize: 12
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.stopRequested()
    }
}
