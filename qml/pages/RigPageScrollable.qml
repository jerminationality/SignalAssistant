import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../components"

Item {
    id: root
    width: 1280
    height: 800
    property Item dragSurface: dragLayer

    readonly property var accordionData: [
        { title: "INPUT", headerColor: "#4F4F4F", items: ["Guitar", "Mic", "Sample"] },
        { title: "PRE-FX", headerColor: "#33964D", items: ["Compressor", "Gate", "EQ", "Wah", "Pitch", "Drive"] },
        { title: "AMPS", headerColor: "#CF6C42", items: ["Amp 1", "Amp 2"] },
        { title: "CABINETS", headerColor: "#B18F60", items: ["1 × 12", "2 × 12", "4 × 12", "Bass Cab", "IR Loader"] },
        { title: "POST-FX", headerColor: "#30768F", items: ["Delay", "Reverb", "Chorus", "Flanger", "Phaser", "Tremolo"] }
    ]

    readonly property var tabData: [
        { icon: "../assets/icons/coreIcon.svg", name: "Core" },
        { icon: "../assets/icons/utilityIcon.svg", name: "Utility" },
        { icon: "../assets/icons/systemIcon.svg", name: "System" }
    ]

    property int currentTab: 0

    Image {
        anchors.fill: parent
        source: "../assets/bgFill.png"
        fillMode: Image.Stretch
        asynchronous: true
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            id: sidebar
            Layout.preferredWidth: 195
            Layout.fillHeight: true

            Image {
                anchors.fill: parent
                source: "../assets/sidebarFill.png"
                asynchronous: true
            }

            Column {
                anchors.fill: parent
                spacing: 0

                Item {
                    id: tabBar
                    width: parent.width
                    height: 46

                    Row {
                        anchors.fill: parent
                        spacing: 0

                        Repeater {
                            model: tabData

                            delegate: Item {
                                width: 65
                                height: parent.height

                                Rectangle {
                                    anchors.fill: parent
                                    color: "#242424"
                                    border.width: 0
                                }

                                ColorizedIcon {
                                    anchors.centerIn: parent
                                    source: modelData.icon
                                    width: 24
                                    height: 24
                                    tint: root.currentTab === index ? "#FFFFFF" : "#9A9A9A"
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.currentTab = index
                                }
                            }
                        }
                    }
                }

                Item {
                    id: accordionHost
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: tabBar.bottom
                    anchors.bottom: parent.bottom
                    clip: true

                    Loader {
                        id: coreLoader
                        anchors.fill: parent
                        active: root.currentTab === 0
                        sourceComponent: coreAccordionColumn
                    }

                    Component {
                        id: coreAccordionColumn

                        Flickable {
                            anchors.fill: parent
                            contentWidth: width
                            contentHeight: accordionColumn.implicitHeight
                            flickableDirection: Flickable.VerticalFlick
                            boundsBehavior: Flickable.StopAtBounds
                            clip: true

                            Column {
                                id: accordionColumn
                                width: parent.width
                                spacing: 0

                                Repeater {
                                    model: root.accordionData

                                    delegate: CoreAccordion {
                                        width: parent.width
                                        title: modelData.title
                                        headerColor: modelData.headerColor
                                        items: modelData.items
                                        defaultExpanded: index === 0
                                        dragLayer: root.dragSurface
                                        dropTarget: mainContent.dropArea
                                        dropCallback: mainContent.handleDrop
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Item {
            id: mainContent
            Layout.fillWidth: true
            Layout.fillHeight: true

            readonly property int tileWidth: 127
            readonly property int tileHeight: 63
            readonly property int workspaceWidth: 1920
            readonly property int workspaceHeight: 1080
            property alias dropArea: workspaceContent

            ListModel {
                id: placedTiles
            }

            function clampPosition(rawX, rawY) {
                var minX = 0
                var minY = 0
                var maxX = Math.max(0, dropArea.width - tileWidth)
                var maxY = Math.max(0, dropArea.height - tileHeight)
                return {
                    x: Math.min(Math.max(rawX, minX), maxX),
                    y: Math.min(Math.max(rawY, minY), maxY)
                }
            }

            function clampContentOffset(rawX, rawY) {
                var maxX = Math.max(0, workspaceView.contentWidth - workspaceView.width)
                var maxY = Math.max(0, workspaceView.contentHeight - workspaceView.height)
                return {
                    x: Math.min(Math.max(rawX, 0), maxX),
                    y: Math.min(Math.max(rawY, 0), maxY)
                }
            }

            function handleDrop(payload) {
                if (!payload || !dropArea)
                    return

                var target = clampPosition(payload.localPoint.x - tileWidth / 2, payload.localPoint.y - tileHeight / 2)
                placedTiles.append({
                    text: payload.text || "",
                    color: payload.color || "#F5F5F5",
                    fontSize: payload.fontSize || 14,
                    x: target.x,
                    y: target.y
                })
            }

            Item {
                id: contentFrame
                anchors.fill: parent

                MapOverview {
                    id: overviewMap
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.rightMargin: 32
                    anchors.bottomMargin: 32
                    z: 50
                    opacity: 0.92
                    workspaceWidth: workspaceView.contentWidth
                    workspaceHeight: workspaceView.contentHeight
                    viewWidth: workspaceView.width
                    viewHeight: workspaceView.height
                    contentX: workspaceView.contentX
                    contentY: workspaceView.contentY
                    itemsModel: placedTiles
                    tileWidth: mainContent.tileWidth
                    tileHeight: mainContent.tileHeight
                    visible: workspaceWidth > 0 && workspaceHeight > 0
                    onRequestViewPosition: function(x, y) {
                        var clamped = mainContent.clampContentOffset(x, y)
                        workspaceView.contentX = clamped.x
                        workspaceView.contentY = clamped.y
                    }
                }

                Flickable {
                    id: workspaceView
                    anchors.fill: parent
                    anchors.margins: 24
                    clip: true
                    interactive: false
                    contentWidth: workspaceContent.width
                    contentHeight: workspaceContent.height

                    Item {
                        id: workspaceContent
                        width: Math.max(mainContent.workspaceWidth, workspaceView.width)
                        height: Math.max(mainContent.workspaceHeight, workspaceView.height)
                    }

                    Repeater {
                        model: placedTiles
                        delegate: Item {
                            id: placedTile
                            width: mainContent.tileWidth
                            height: mainContent.tileHeight
                            x: model.x
                            y: model.y
                            parent: workspaceContent
                            z: 1

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
                                text: model.text
                                color: model.color
                                font.pixelSize: model.fontSize
                                font.weight: Font.DemiBold
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                            }

                            MouseArea {
                                id: tileDragArea
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton
                                pressAndHoldInterval: 1000
                                hoverEnabled: true
                                cursorShape: dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                                property bool dragging: false
                                property real pointerOffsetX: 0
                                property real pointerOffsetY: 0
                                onPressed: {
                                    dragging = false
                                    placedTile.opacity = 1.0
                                    pointerOffsetX = 0
                                    pointerOffsetY = 0
                                }
                                onPressAndHold: {
                                    dragging = true
                                    mouse.accepted = true
                                    placedTile.z = 30
                                    placedTile.opacity = 0.5
                                    var mapped = tileDragArea.mapToItem(mainContent.dropArea, mouse.x, mouse.y)
                                    pointerOffsetX = mapped.x - placedTile.x
                                    pointerOffsetY = mapped.y - placedTile.y
                                }
                                onPositionChanged: {
                                    if (!dragging)
                                        return
                                    var mapped = tileDragArea.mapToItem(mainContent.dropArea, mouse.x, mouse.y)
                                    var newX = mapped.x - pointerOffsetX
                                    var newY = mapped.y - pointerOffsetY
                                    var confined = mainContent.clampPosition(newX, newY)
                                    placedTile.x = confined.x
                                    placedTile.y = confined.y
                                }
                                onReleased: {
                                    if (dragging) {
                                        var confined = mainContent.clampPosition(placedTile.x, placedTile.y)
                                        placedTile.x = confined.x
                                        placedTile.y = confined.y
                                        placedTiles.setProperty(index, "x", placedTile.x)
                                        placedTiles.setProperty(index, "y", placedTile.y)
                                        placedTile.z = 1
                                        placedTile.opacity = 1.0
                                    }
                                    dragging = false
                                    pointerOffsetX = 0
                                    pointerOffsetY = 0
                                }
                                onCanceled: {
                                    if (dragging) {
                                        var confined = mainContent.clampPosition(placedTile.x, placedTile.y)
                                        placedTile.x = confined.x
                                        placedTile.y = confined.y
                                        placedTiles.setProperty(index, "x", placedTile.x)
                                        placedTiles.setProperty(index, "y", placedTile.y)
                                        placedTile.z = 1
                                        placedTile.opacity = 1.0
                                    }
                                    placedTile.opacity = 1.0
                                    dragging = false
                                    pointerOffsetX = 0
                                    pointerOffsetY = 0
                                }
                            }
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    propagateComposedEvents: true
                    property real lastX: 0
                    property real lastY: 0
                    property bool panning: false
                    onPressed: {
                        if (mouse.button !== Qt.RightButton) {
                            mouse.accepted = false
                            return
                        }
                        panning = true
                        lastX = mouse.x
                        lastY = mouse.y
                    }
                    onPositionChanged: {
                        if (!panning)
                            return
                        var dx = mouse.x - lastX
                        var dy = mouse.y - lastY
                        var clamped = mainContent.clampContentOffset(workspaceView.contentX - dx, workspaceView.contentY - dy)
                        workspaceView.contentX = clamped.x
                        workspaceView.contentY = clamped.y
                        lastX = mouse.x
                        lastY = mouse.y
                    }
                    onReleased: {
                        panning = false
                    }
                    onCanceled: {
                        panning = false
                    }
                }

                MultiPointTouchArea {
                    anchors.fill: parent
                    minimumTouchPoints: 2
                    maximumTouchPoints: 2
                    mouseEnabled: false
                    property real lastCenterX: 0
                    property real lastCenterY: 0
                    property bool baselineInitialized: false
                    onPressed: {
                        if (touchPoints.length < 2)
                            return
                        lastCenterX = (touchPoints[0].position.x + touchPoints[1].position.x) * 0.5
                        lastCenterY = (touchPoints[0].position.y + touchPoints[1].position.y) * 0.5
                        baselineInitialized = true
                    }
                    onUpdated: {
                        if (touchPoints.length < 2)
                            return
                        var centerX = (touchPoints[0].position.x + touchPoints[1].position.x) * 0.5
                        var centerY = (touchPoints[0].position.y + touchPoints[1].position.y) * 0.5
                        if (!baselineInitialized) {
                            lastCenterX = centerX
                            lastCenterY = centerY
                            baselineInitialized = true
                            return
                        }
                        var clamped = mainContent.clampContentOffset(
                                        workspaceView.contentX - (centerX - lastCenterX),
                                        workspaceView.contentY - (centerY - lastCenterY))
                        workspaceView.contentX = clamped.x
                        workspaceView.contentY = clamped.y
                        lastCenterX = centerX
                        lastCenterY = centerY
                    }
                    onReleased: {
                        baselineInitialized = false
                    }
                    onCanceled: {
                        baselineInitialized = false
                    }
                }
            }
        }
    }

    Item {
        id: dragLayer
        anchors.fill: parent
        z: 999
    }
}
