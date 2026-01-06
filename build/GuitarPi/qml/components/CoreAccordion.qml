import QtQuick 2.15
import QtQuick.Layouts 1.15
import "./"

Item {
    id: root
    property string title: ""
    property color headerColor: "#4F4F4F"
    property var items: []
    property bool expanded: false
    property bool defaultExpanded: false
    property color textColor: "#F5F5F5"
    property Item dragLayer: null
    property Item dropTarget: null
    property var dropCallback: null
    readonly property int headerHeight: 42
    readonly property int padding: 16
    readonly property int topPadding: 0
    readonly property int itemSpacing: 12

    width: parent ? parent.width : 195
    height: headerHeight + contentSection.height

    Component.onCompleted: expanded = defaultExpanded

    Rectangle {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        height: headerHeight
        color: headerColor
    }

    Text {
        anchors.left: header.left
        anchors.leftMargin: 20
        anchors.verticalCenter: header.verticalCenter
        text: root.title
        color: "#E4E4E4"
        font.pixelSize: 14
        font.weight: Font.Bold
    }

    Image {
        id: chevron
        anchors.right: header.right
        anchors.rightMargin: 22
        anchors.verticalCenter: header.verticalCenter
        source: "../assets/chevron.svg"
        width: 10
        height: 6
        rotation: expanded ? 180 : 0
        smooth: true
        asynchronous: true
        Behavior on rotation {
            NumberAnimation {
                duration: 200
                easing.type: Easing.OutCubic
            }
        }
    }

    MouseArea {
        anchors.fill: header
        cursorShape: Qt.PointingHandCursor
        onClicked: root.expanded = !root.expanded
    }

    Rectangle {
        id: contentSection
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        width: parent.width
        height: root.expanded ? contentColumn.implicitHeight + root.topPadding + root.padding : 0
        clip: true
        color: headerColor
        border.width: 0
        Behavior on height {
            NumberAnimation {
                duration: 220
                easing.type: Easing.OutCubic
            }
        }

        Column {
            id: contentColumn
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: root.topPadding
            spacing: root.itemSpacing
            padding: root.padding
            width: parent.width - root.padding * 2
            visible: root.expanded
            Behavior on y {
                NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
            }

            Repeater {
                model: root.items
                delegate: CoreComponent {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: typeof modelData === "string" ? modelData : modelData.text
                    textColor: root.textColor
                    dragLayer: root.dragLayer
                    dropTarget: root.dropTarget
                    dropCallback: root.dropCallback
                }
            }
        }
    }
}
