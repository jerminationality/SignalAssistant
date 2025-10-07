import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: badge
    property string text: AppController.latencyText
    color: "#0ea5e9"
    radius: 8
    height: 28
    width: implicitWidth
    implicitWidth: label.implicitWidth + 16
    Text {
        id: label
        anchors.centerIn: parent
        text: badge.text.length ? badge.text : "latency: —"
        color: "white"
    }
}
