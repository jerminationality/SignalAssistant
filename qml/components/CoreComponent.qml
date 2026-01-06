import QtQuick 2.15

Item {
    id: root
    property string text: ""
    property color textColor: "#F5F5F5"
    property int fontSize: 14
    property Item dragLayer: null
    property Item dropTarget: null
    property var dropCallback: null
    property int designWidth: 127
    property int designHeight: 63
    property Item dragProxy: null
    property bool dragActive: false
    property real dragOffsetX: 0
    property real dragOffsetY: 0
    property real pressX: 0
    property real pressY: 0

    implicitWidth: designWidth
    implicitHeight: designHeight
    width: implicitWidth
    height: implicitHeight

    Image {
        anchors.fill: parent
        source: "../assets/component.svg"
        fillMode: Image.Stretch
        smooth: true
        asynchronous: true
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 10
        radius: 10
        color: "#2C2C2C"
        border.width: 0
    }

    Text {
        anchors.centerIn: parent
        width: parent.width - 20
        text: root.text
        color: root.textColor
        font.pixelSize: root.fontSize
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
    }

    MouseArea {
        id: dragArea
        anchors.fill: parent
        cursorShape: root.dragActive ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        acceptedButtons: Qt.LeftButton
        preventStealing: true
        onPressed: {
            if (mouse.button !== Qt.LeftButton) {
                mouse.accepted = false
                return
            }
            pressX = mouse.x
            pressY = mouse.y
        }
        onPositionChanged: {
            if (!(mouse.buttons & Qt.LeftButton))
                return

            if (!root.dragActive) {
                if (Math.abs(mouse.x - pressX) > 4 || Math.abs(mouse.y - pressY) > 4)
                    root.startDrag(mouse.x, mouse.y)
            } else {
                root.updateDrag(mouse.x, mouse.y)
            }
        }
        onReleased: {
            if (mouse.button !== Qt.LeftButton)
                return
            root.finishDrag()
        }
        onCanceled: {
            root.cancelDrag()
        }
    }

    Component {
        id: dragProxyComponent
        Item {
            property string tileText: ""
            property color tileTextColor: "#F5F5F5"
            property int tileFontSize: 14
            width: root.width
            height: root.height

            Image {
                anchors.fill: parent
                source: "../assets/component.svg"
                fillMode: Image.Stretch
                smooth: true
                asynchronous: true
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: 10
                radius: 10
                color: "#2C2C2C"
                border.width: 0
            }

            Text {
                anchors.centerIn: parent
                width: parent.width - 20
                text: tileText
                color: tileTextColor
                font.pixelSize: tileFontSize
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
        }
    }

    function startDrag(x, y) {
        if (!dragLayer || dragActive)
            return

        var origin = root.mapToItem(dragLayer, 0, 0)
        dragProxy = dragProxyComponent.createObject(dragLayer, {
            x: origin.x,
            y: origin.y,
            tileText: root.text,
            tileTextColor: root.textColor,
            tileFontSize: root.fontSize,
            opacity: 0.5,
            z: 1000
        })

        if (!dragProxy)
            return

        dragOffsetX = x
        dragOffsetY = y
        dragActive = true
        updateDrag(x, y)
    }

    function updateDrag(x, y) {
        if (!dragActive || !dragProxy)
            return

        var mapped = root.mapToItem(dragLayer, x, y)
        dragProxy.x = mapped.x - dragOffsetX
        dragProxy.y = mapped.y - dragOffsetY
    }

    function finishDrag() {
        if (!dragActive)
            return

        if (dragProxy) {
            if (dropTarget) {
                var center = dragProxy.mapToItem(dropTarget, dragProxy.width / 2, dragProxy.height / 2)
                if (center.x >= 0 && center.x <= dropTarget.width && center.y >= 0 && center.y <= dropTarget.height) {
                    if (dropCallback)
                        dropCallback({
                            text: root.text,
                            color: root.textColor,
                            fontSize: root.fontSize,
                            localPoint: center
                        })
                }
            }

            dragProxy.destroy()
            dragProxy = null
        }

        dragActive = false
    }

    function cancelDrag() {
        if (dragProxy) {
            dragProxy.destroy()
            dragProxy = null
        }
        dragActive = false
    }
}
