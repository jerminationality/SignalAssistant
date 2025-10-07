import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root
    property real level: 0.0 // 0..1
    width: 16; height: 120
    Rectangle {
        anchors.fill: parent
        color: "#111827"
        border.color: "#374151"
        radius: 6
    }
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Math.max(4, parent.height * level)
        radius: 6
        color: "#22c55e"
    }
}
