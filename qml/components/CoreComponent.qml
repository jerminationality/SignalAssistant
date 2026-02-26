import QtQuick 2.15

Item {
    id: root
    property string text: ""
    property string componentType: ""
    property color textColor: "#F5F5F5"
    property int fontSize: 14
    property Item dragLayer: null
    property Item dropTarget: null
    property var dropCallback: null
    property var dropController: null
    property int sidebarWidth: 211
    property int designWidth: {
        if (componentType === "AMPS") return 138
        if (componentType === "CABINETS") return 127
        return 72  // PRE-FX and POST-FX
    }
    property int designHeight: {
        if (componentType === "AMPS") return 68
        if (componentType === "CABINETS") return 127
        return 112  // PRE-FX and POST-FX
    }
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

    Rectangle {
        anchors.fill: parent
        radius: 3
        color: "#2C2C2C"
        border.width: 2.5
        border.color: {
            if (root.componentType === "PRE-FX") return "#33964D"
            if (root.componentType === "AMPS") return "#CF6C42"
            if (root.componentType === "CABINETS") return "#B18F60"
            if (root.componentType === "POST-FX") return "#30768F"
            return "#FFFFFF"
        }
    }

    Text {
        anchors.centerIn: parent
        width: parent.width - 20
        text: root.text
        color: root.textColor
        font.pixelSize: root.fontSize
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        wrapMode: Text.WordWrap
    }

    MouseArea {
        id: dragArea
        anchors.fill: parent
        cursorShape: root.dragActive ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        acceptedButtons: Qt.LeftButton
        preventStealing: root.dragActive  // Only prevent stealing once drag is active
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
                var deltaX = Math.abs(mouse.x - pressX)
                var deltaY = Math.abs(mouse.y - pressY)
                
                // Only start drag if horizontal movement is clearly dominant
                // Require: horizontal > vertical AND horizontal > threshold
                if (deltaX > deltaY && deltaX > 10) {
                    root.startDrag(mouse.x, mouse.y)
                }
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

        // Check if over sidebar
        var isOverSidebar = (dragProxy.x + dragProxy.width / 2) < sidebarWidth

        // Update preview in drop target
        if (dropController && dropController.updateDragPreview && dropTarget) {
            var centerInTarget = dragProxy.mapToItem(dropTarget, dragProxy.width / 2, dragProxy.height / 2)
            dropController.updateDragPreview(root.componentType, centerInTarget.x, centerInTarget.y, root.text, isOverSidebar, false)
        }
    }

    function finishDrag() {
        if (!dragActive)
            return

        if (dragProxy) {
            // Check if over sidebar
            var isOverSidebar = (dragProxy.x + dragProxy.width / 2) < sidebarWidth

            if (dropTarget) {
                var center = dragProxy.mapToItem(dropTarget, dragProxy.width / 2, dragProxy.height / 2)
                if (center.x >= 0 && center.x <= dropTarget.width && center.y >= 0 && center.y <= dropTarget.height) {
                    if (dropCallback)
                        dropCallback({
                            text: root.text,
                            componentType: root.componentType,
                            color: root.textColor,
                            fontSize: root.fontSize,
                            localPoint: center,
                            isOverSidebar: isOverSidebar
                        })
                }
            }

            dragProxy.destroy()
            dragProxy = null
        }

        // Clear preview
        if (dropController && dropController.clearDragPreview) {
            dropController.clearDragPreview()
        }

        dragActive = false
    }

    function cancelDrag() {
        if (dragProxy) {
            dragProxy.destroy()
            dragProxy = null
        }

        // Clear preview
        if (dropController && dropController.clearDragPreview) {
            dropController.clearDragPreview()
        }

        dragActive = false
    }
}
