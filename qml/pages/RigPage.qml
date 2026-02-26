import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../components"

Item {
    id: root
    width: 1024
    height: 600
    property Item dragSurface: dragLayer
    property int sidebarWidth: 211

    readonly property var accordionData: [
        { title: "PRE-FX", headerColor: "#33964D", items: ["Comp", "Gate", "EQ", "Wah", "Pitch", "Drive"] },
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

    Rectangle {
        anchors.fill: parent
        color: "#000000"
        z: -200
    }

    Image {
        anchors.fill: parent
        source: "../assets/BGFillLight.png"
        fillMode: Image.Stretch
        z: -100
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            id: sidebar
            Layout.preferredWidth: 211
            Layout.fillHeight: true

            Rectangle {
                anchors.fill: parent
                color: "#1F1F1F"
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
                                width: sidebar.width / 3
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
                    width: parent.width
                    height: parent.height - tabBar.height
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
                                        defaultExpanded: false
                                        dragLayer: root.dragSurface
                                        dropTarget: mainContent.dropArea
                                        dropCallback: mainContent.handleDrop
                                        dropController: mainContent.dropController
                                        sidebarWidth: root.sidebarWidth
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

            // Zoom state for Pre-FX focus view
            property bool preFxZoomed: false
            // Zoom state for Post-FX focus view
            property bool postFxZoomed: false

            // Animation duration for zoom transitions
            readonly property int zoomDuration: 400

            // Normal slot positions
            readonly property var normalPreFxSlots: [
                { x: 90, y: 21 },
                { x: 205, y: 21 },
                { x: 320, y: 21 },
                { x: 435, y: 21 },
                { x: 550, y: 21 },
                { x: 665, y: 21 }
            ]
            // Zoomed Pre-FX: 3x2 grid (mirrored from Swipe Up SVG)
            readonly property var zoomedPreFxSlots: [
                { x: 74, y: 28 },
                { x: 312, y: 28 },
                { x: 550, y: 28 },
                { x: 74, y: 308 },
                { x: 312, y: 308 },
                { x: 550, y: 308 }
            ]
            // Pre-FX off-screen top (when Post-FX is zoomed)
            readonly property var offscreenTopPreFxSlots: [
                { x: 90, y: -200 },
                { x: 205, y: -200 },
                { x: 320, y: -200 },
                { x: 435, y: -200 },
                { x: 550, y: -200 },
                { x: 665, y: -200 }
            ]
            // Normal component size for FX
            readonly property real normalFxWidth: 72
            readonly property real normalFxHeight: 112
            // Zoomed component size (same for both Pre-FX and Post-FX)
            readonly property real zoomedFxWidth: 191
            readonly property real zoomedFxHeight: 246

            // Normal Post-FX slot positions
            readonly property var normalPostFxSlots: [
                { x: 90, y: 454 },
                { x: 205, y: 454 },
                { x: 320, y: 454 },
                { x: 435, y: 454 },
                { x: 550, y: 454 },
                { x: 665, y: 454 }
            ]
            // Zoomed Post-FX: 3x2 grid (from Swipe Up SVG)
            readonly property var zoomedPostFxSlots: [
                { x: 74, y: 46 },
                { x: 312, y: 46 },
                { x: 550, y: 46 },
                { x: 74, y: 326 },
                { x: 312, y: 326 },
                { x: 550, y: 326 }
            ]
            // Post-FX off-screen bottom (when Pre-FX is zoomed)
            readonly property var offscreenBottomPostFxSlots: [
                { x: 90, y: 700 },
                { x: 205, y: 700 },
                { x: 320, y: 700 },
                { x: 435, y: 700 },
                { x: 550, y: 700 },
                { x: 665, y: 700 }
            ]

            // Normal Amp/Cab positions
            readonly property var normalAmpSlot: ({ x: 22, y: 183 })
            readonly property var normalCabSlot: ({ x: 586, y: 226 })
            // Amp/Cab partially visible at bottom (Pre-FX zoomed, mirrored)
            readonly property var preFxZoomedAmpSlot: ({ x: 44, y: 581 })
            readonly property var preFxZoomedCabSlot: ({ x: 547, y: 589 })
            // Amp/Cab partially visible at top (Post-FX zoomed, from SVG)
            readonly property var postFxZoomedAmpSlot: ({ x: 44, y: -159 })
            readonly property var postFxZoomedCabSlot: ({ x: 547, y: -110 })

            // Amp/Cab zoom state
            property bool ampCabZoomed: false

            // Amp/Cab zoomed positions (from SVG, mainContent-relative)
            readonly property var zoomedAmpSlot: ({ x: 42, y: 49 })
            readonly property var zoomedCabSlot: ({ x: 42, y: 330 })
            readonly property real zoomedAmpWidth: 722
            readonly property real zoomedAmpHeight: 271
            readonly property real zoomedCabWidth: 289
            readonly property real zoomedCabHeight: 188
            // Off-screen positions for FX when amp/cab zoomed (from SVG)
            readonly property var ampCabZoomedPreFxSlots: [
                { x: 90, y: -88 },
                { x: 205, y: -88 },
                { x: 320, y: -88 },
                { x: 435, y: -88 },
                { x: 550, y: -88 },
                { x: 665, y: -88 }
            ]
            readonly property var ampCabZoomedPostFxSlots: [
                { x: 90, y: 591 },
                { x: 205, y: 591 },
                { x: 320, y: 591 },
                { x: 435, y: 591 },
                { x: 550, y: 591 },
                { x: 665, y: 591 }
            ]

            // Single FX zoom state
            property bool singleFxZoomed: false
            property string singleFxZoomType: ""       // "PRE-FX" or "POST-FX"
            property int singleFxZoomIndex: -1          // slot index of focused FX
            property string previousZoomState: "normal" // "normal", "preFxZoomed", "postFxZoomed", "ampCabZoomed"

            // Single FX zoom layout (from SVG, mainContent-relative)
            readonly property real singleFxWidth: 383
            readonly property real singleFxHeight: 499
            readonly property real singleFxY: 40
            readonly property real singleFxCenterX: 215   // focused component x
            readonly property real singleFxSpacing: 543   // distance between components
            readonly property real singleFxLeftX: -329    // left neighbor x
            readonly property real singleFxRightX: 758    // right neighbor x

            // Animation scale and offset properties for future zoom/pan
            property real contentScale: 1.0
            property real contentOffsetX: 0
            property real contentOffsetY: 0

            Behavior on contentScale {
                NumberAnimation { duration: 300; easing.type: Easing.OutCubic }
            }
            Behavior on contentOffsetX {
                NumberAnimation { duration: 300; easing.type: Easing.OutCubic }
            }
            Behavior on contentOffsetY {
                NumberAnimation { duration: 300; easing.type: Easing.OutCubic }
            }

            // Slot configuration
            property var preFxSlots: [
                { x: 90, y: 21, occupied: false },
                { x: 205, y: 21, occupied: false },
                { x: 320, y: 21, occupied: false },
                { x: 435, y: 21, occupied: false },
                { x: 550, y: 21, occupied: false },
                { x: 665, y: 21, occupied: false }
            ]
            
            property var postFxSlots: [
                { x: 90, y: 454, occupied: false },
                { x: 205, y: 454, occupied: false },
                { x: 320, y: 454, occupied: false },
                { x: 435, y: 454, occupied: false },
                { x: 550, y: 454, occupied: false },
                { x: 665, y: 454, occupied: false }
            ]
            
            property var ampSlot: ({ x: 22, y: 183, occupied: false })
            property var cabSlot: ({ x: 586, y: 226, occupied: false })

            function togglePreFxZoom() {
                ampCabZoomed = false
                singleFxZoomed = false
                if (postFxZoomed) {
                    // If post-FX is zoomed, unzoom it first then zoom pre-FX
                    postFxZoomed = false
                    preFxZoomed = true
                } else {
                    preFxZoomed = !preFxZoomed
                }
                applyZoomState()
            }

            function togglePostFxZoom() {
                ampCabZoomed = false
                singleFxZoomed = false
                if (preFxZoomed) {
                    // If pre-FX is zoomed, unzoom it first then zoom post-FX
                    preFxZoomed = false
                    postFxZoomed = true
                } else {
                    postFxZoomed = !postFxZoomed
                }
                applyZoomState()
            }

            function applyZoomState() {
                // Determine Pre-FX target positions
                var srcPreFx
                if (preFxZoomed) {
                    srcPreFx = zoomedPreFxSlots
                } else if (postFxZoomed) {
                    srcPreFx = offscreenTopPreFxSlots
                } else {
                    srcPreFx = normalPreFxSlots
                }

                // Determine Post-FX target positions
                var srcPostFx
                if (postFxZoomed) {
                    srcPostFx = zoomedPostFxSlots
                } else if (preFxZoomed) {
                    srcPostFx = offscreenBottomPostFxSlots
                } else {
                    srcPostFx = normalPostFxSlots
                }

                // Determine Amp/Cab target positions
                var srcAmp, srcCab
                if (preFxZoomed) {
                    srcAmp = preFxZoomedAmpSlot
                    srcCab = preFxZoomedCabSlot
                } else if (postFxZoomed) {
                    srcAmp = postFxZoomedAmpSlot
                    srcCab = postFxZoomedCabSlot
                } else {
                    srcAmp = normalAmpSlot
                    srcCab = normalCabSlot
                }

                // Update slot positions (preserve occupied state)
                var newPreFx = preFxSlots.slice()
                for (var i = 0; i < 6; i++) {
                    newPreFx[i] = { x: srcPreFx[i].x, y: srcPreFx[i].y, occupied: newPreFx[i].occupied }
                }
                preFxSlots = newPreFx

                var newPostFx = postFxSlots.slice()
                for (var j = 0; j < 6; j++) {
                    newPostFx[j] = { x: srcPostFx[j].x, y: srcPostFx[j].y, occupied: newPostFx[j].occupied }
                }
                postFxSlots = newPostFx

                ampSlot = { x: srcAmp.x, y: srcAmp.y, occupied: ampSlot.occupied }
                cabSlot = { x: srcCab.x, y: srcCab.y, occupied: cabSlot.occupied }

                // Update placed component positions to match new slots
                for (var k = 0; k < placedComponents.count; k++) {
                    var comp = placedComponents.get(k)
                    var slotPos
                    if (comp.componentType === "PRE-FX") {
                        slotPos = srcPreFx[comp.slotIndex]
                    } else if (comp.componentType === "POST-FX") {
                        slotPos = srcPostFx[comp.slotIndex]
                    } else if (comp.componentType === "AMPS") {
                        slotPos = srcAmp
                    } else if (comp.componentType === "CABINETS") {
                        slotPos = srcCab
                    }
                    if (slotPos) {
                        placedComponents.set(k, { x: slotPos.x, y: slotPos.y })
                    }
                }
            }

            function enterAmpCabZoom() {
                ampCabZoomed = true
                preFxZoomed = false
                postFxZoomed = false
                singleFxZoomed = false
                applyAmpCabZoomPositions()
            }

            function exitAmpCabZoom() {
                ampCabZoomed = false
                applyZoomState()
            }

            function applyAmpCabZoomPositions() {
                // Move amp and cab to zoomed positions
                ampSlot = { x: zoomedAmpSlot.x, y: zoomedAmpSlot.y, occupied: ampSlot.occupied }
                cabSlot = { x: zoomedCabSlot.x, y: zoomedCabSlot.y, occupied: cabSlot.occupied }

                // Move Pre-FX slots off-screen top
                var newPreFx = preFxSlots.slice()
                for (var i = 0; i < 6; i++) {
                    newPreFx[i] = { x: ampCabZoomedPreFxSlots[i].x, y: ampCabZoomedPreFxSlots[i].y, occupied: newPreFx[i].occupied }
                }
                preFxSlots = newPreFx

                // Move Post-FX slots off-screen bottom
                var newPostFx = postFxSlots.slice()
                for (var j = 0; j < 6; j++) {
                    newPostFx[j] = { x: ampCabZoomedPostFxSlots[j].x, y: ampCabZoomedPostFxSlots[j].y, occupied: newPostFx[j].occupied }
                }
                postFxSlots = newPostFx

                // Update placed component positions
                for (var k = 0; k < placedComponents.count; k++) {
                    var comp = placedComponents.get(k)
                    if (comp.componentType === "AMPS") {
                        placedComponents.set(k, { x: zoomedAmpSlot.x, y: zoomedAmpSlot.y })
                    } else if (comp.componentType === "CABINETS") {
                        placedComponents.set(k, { x: zoomedCabSlot.x, y: zoomedCabSlot.y })
                    } else if (comp.componentType === "PRE-FX") {
                        placedComponents.set(k, { x: comp.x, y: -88 })
                    } else if (comp.componentType === "POST-FX") {
                        placedComponents.set(k, { x: comp.x, y: 591 })
                    }
                }
            }

            function enterSingleFxZoom(componentType, slotIndex) {
                // Remember previous state for exit
                if (ampCabZoomed) previousZoomState = "ampCabZoomed"
                else if (preFxZoomed) previousZoomState = "preFxZoomed"
                else if (postFxZoomed) previousZoomState = "postFxZoomed"
                else previousZoomState = "normal"

                singleFxZoomType = componentType
                singleFxZoomIndex = slotIndex
                singleFxZoomed = true
                applySingleFxZoomPositions()
            }

            function exitSingleFxZoom() {
                singleFxZoomed = false
                singleFxZoomType = ""
                singleFxZoomIndex = -1

                // Restore previous state
                if (previousZoomState === "ampCabZoomed") {
                    ampCabZoomed = true
                    preFxZoomed = false
                    postFxZoomed = false
                    previousZoomState = "normal"
                    applyAmpCabZoomPositions()
                    return
                } else if (previousZoomState === "preFxZoomed") {
                    preFxZoomed = true
                    postFxZoomed = false
                } else if (previousZoomState === "postFxZoomed") {
                    preFxZoomed = false
                    postFxZoomed = true
                } else {
                    preFxZoomed = false
                    postFxZoomed = false
                }
                previousZoomState = "normal"
                applyZoomState()
            }

            function pageSingleFx(direction) {
                // direction: -1 = left (previous), +1 = right (next)
                var slots = singleFxZoomType === "PRE-FX" ? preFxSlots : postFxSlots
                var newIndex = singleFxZoomIndex + direction
                if (newIndex < 0 || newIndex >= slots.length) return
                singleFxZoomIndex = newIndex
                applySingleFxZoomPositions()
            }

            function applySingleFxZoomPositions() {
                var slots = singleFxZoomType === "PRE-FX" ? preFxSlots : postFxSlots
                var focusedIndex = singleFxZoomIndex

                // Position all components of the same type in a horizontal strip
                for (var k = 0; k < placedComponents.count; k++) {
                    var comp = placedComponents.get(k)
                    if (comp.componentType === singleFxZoomType) {
                        var offset = comp.slotIndex - focusedIndex
                        var targetX = singleFxCenterX + (offset * singleFxSpacing)
                        placedComponents.set(k, { x: targetX, y: singleFxY })
                    } else {
                        // Move off-screen: Pre-FX zoom → everything slides down, Post-FX zoom → everything slides up
                        var offY = singleFxZoomType === "PRE-FX" ? 700 : -300
                        if (comp.componentType === "PRE-FX") {
                            placedComponents.set(k, { x: comp.x, y: offY })
                        } else if (comp.componentType === "POST-FX") {
                            placedComponents.set(k, { x: comp.x, y: offY })
                        } else if (comp.componentType === "AMPS") {
                            placedComponents.set(k, { x: comp.x, y: offY })
                        } else if (comp.componentType === "CABINETS") {
                            placedComponents.set(k, { x: comp.x, y: offY })
                        }
                    }
                }

                // Position focused type's slot outlines in the horizontal strip, hide others
                var newPreFx = preFxSlots.slice()
                for (var i = 0; i < 6; i++) {
                    if (singleFxZoomType === "PRE-FX") {
                        var preOffset = i - focusedIndex
                        newPreFx[i] = { x: singleFxCenterX + (preOffset * singleFxSpacing), y: singleFxY, occupied: newPreFx[i].occupied }
                    } else {
                        newPreFx[i] = { x: newPreFx[i].x, y: -300, occupied: newPreFx[i].occupied }
                    }
                }
                preFxSlots = newPreFx

                var newPostFx = postFxSlots.slice()
                for (var j = 0; j < 6; j++) {
                    if (singleFxZoomType === "POST-FX") {
                        var postOffset = j - focusedIndex
                        newPostFx[j] = { x: singleFxCenterX + (postOffset * singleFxSpacing), y: singleFxY, occupied: newPostFx[j].occupied }
                    } else {
                        newPostFx[j] = { x: newPostFx[j].x, y: 700, occupied: newPostFx[j].occupied }
                    }
                }
                postFxSlots = newPostFx

                var ampOffY = singleFxZoomType === "PRE-FX" ? 700 : -300
                ampSlot = { x: ampSlot.x, y: ampOffY, occupied: ampSlot.occupied }
                cabSlot = { x: cabSlot.x, y: ampOffY, occupied: cabSlot.occupied }
            }

            // Swipe detection moved into PinchArea (workspaceBackground) to receive events

            property var dragPreview: null
            property var shiftPreview: null  // Stores preview of shifted component positions
            property var draggedComponent: null  // Tracks which component is being dragged
            property bool disableAnimations: false  // Disable animations during instant updates
            property var hoverTimer: null  // Timer for 1s hover requirement
            property var pendingPreview: null  // Preview waiting for hover timer
            property var dragStartZoomState: null  // Stores zoom state before drag starts
            property bool dragZoomTransitionActive: false  // Tracks if zoom transition happened during drag

            ListModel {
                id: placedComponents
            }

            function findFirstOpenSlot(componentType) {
                if (componentType === "PRE-FX") {
                    for (var i = 0; i < preFxSlots.length; i++) {
                        if (!preFxSlots[i].occupied) return { index: i, slot: preFxSlots[i] }
                    }
                } else if (componentType === "POST-FX") {
                    for (var j = 0; j < postFxSlots.length; j++) {
                        if (!postFxSlots[j].occupied) return { index: j, slot: postFxSlots[j] }
                    }
                } else if (componentType === "AMPS") {
                    if (!ampSlot.occupied) return { index: 0, slot: ampSlot }
                } else if (componentType === "CABINETS") {
                    if (!cabSlot.occupied) return { index: 0, slot: cabSlot }
                }
                return null
            }

            function findNearestSlot(x, y, componentType) {
                var slots = []
                if (componentType === "PRE-FX") {
                    slots = preFxSlots
                } else if (componentType === "POST-FX") {
                    slots = postFxSlots
                } else if (componentType === "AMPS") {
                    return { index: 0, slot: ampSlot }
                } else if (componentType === "CABINETS") {
                    return { index: 0, slot: cabSlot }
                }

                var minDist = Number.MAX_VALUE
                var nearestIndex = -1
                
                for (var i = 0; i < slots.length; i++) {
                    var slotCenterX = slots[i].x + 36  // Half of FX width (72/2)
                    var slotCenterY = slots[i].y + 56  // Half of FX height (112/2)
                    var dx = x - slotCenterX
                    var dy = y - slotCenterY
                    var dist = Math.sqrt(dx * dx + dy * dy)
                    
                    if (dist < minDist) {
                        minDist = dist
                        nearestIndex = i
                    }
                }

                // Only consider it "near" if within 100 pixels
                if (nearestIndex >= 0 && minDist < 100) {
                    return { index: nearestIndex, slot: slots[nearestIndex], distance: minDist }
                }
                
                return null
            }

            function findInsertionPoint(x, y, componentType) {
                // For amps and cabinets, use single slot
                if (componentType === "AMPS" || componentType === "CABINETS") {
                    return findNearestSlot(x, y, componentType)
                }

                var slots = componentType === "PRE-FX" ? preFxSlots : postFxSlots
                var slotWidth = 72
                var slotSpacing = 115  // Distance between slot centers
                
                // Build list of occupied slot positions
                var occupiedSlots = []
                for (var i = 0; i < slots.length; i++) {
                    if (slots[i].occupied) {
                        occupiedSlots.push({ index: i, x: slots[i].x, y: slots[i].y })
                    }
                }

                // If no occupied slots, use first open slot
                if (occupiedSlots.length === 0) {
                    return findFirstOpenSlot(componentType)
                }

                // Sort occupied slots by index
                occupiedSlots.sort(function(a, b) { return a.index - b.index })

                // Check if position is between two occupied slots
                for (var j = 0; j < occupiedSlots.length - 1; j++) {
                    var leftSlot = occupiedSlots[j]
                    var rightSlot = occupiedSlots[j + 1]
                    
                    // Calculate the midpoint between the two slots
                    var leftEdge = leftSlot.x + slotWidth
                    var rightEdge = rightSlot.x
                    var midPoint = (leftEdge + rightEdge) / 2
                    
                    // Check if drop position is in the gap between these slots
                    if (x > leftEdge && x < rightEdge) {
                        // Position is between these two slots
                        // Insert after the left slot (which means at rightSlot.index position, shifting right slot)
                        return { 
                            index: rightSlot.index, 
                            slot: slots[rightSlot.index],
                            isBetween: true,
                            distance: 0
                        }
                    }
                }

                // Check if it's directly over an occupied slot (within slot boundaries with tolerance)
                for (var k = 0; k < occupiedSlots.length; k++) {
                    var occSlot = occupiedSlots[k]
                    if (x >= occSlot.x - 10 && x <= occSlot.x + slotWidth + 10) {
                        // Over this occupied slot - insert at this position
                        return {
                            index: occSlot.index,
                            slot: slots[occSlot.index],
                            isBetween: false,
                            distance: 0
                        }
                    }
                }

                // Check if before the first occupied slot
                if (x < occupiedSlots[0].x) {
                    return {
                        index: occupiedSlots[0].index,
                        slot: slots[occupiedSlots[0].index],
                        isBetween: true,
                        distance: 0
                    }
                }

                // Check if after the last occupied slot
                if (x > occupiedSlots[occupiedSlots.length - 1].x + slotWidth) {
                    // Find first open slot after the last occupied one
                    var lastOccupiedIndex = occupiedSlots[occupiedSlots.length - 1].index
                    for (var m = lastOccupiedIndex + 1; m < slots.length; m++) {
                        if (!slots[m].occupied) {
                            return {
                                index: m,
                                slot: slots[m],
                                isBetween: false,
                                distance: 0
                            }
                        }
                    }
                }

                // Not between or over occupied slots - use first open
                return findFirstOpenSlot(componentType)
            }

            function removeComponentAtSlot(componentType, slotIndex) {
                for (var i = placedComponents.count - 1; i >= 0; i--) {
                    var item = placedComponents.get(i)
                    if (item.componentType === componentType && item.slotIndex === slotIndex) {
                        placedComponents.remove(i)
                        
                        // Clear slot occupation
                        if (componentType === "PRE-FX") {
                            var preFx = preFxSlots.slice()
                            preFx[slotIndex].occupied = false
                            preFxSlots = preFx
                        } else if (componentType === "POST-FX") {
                            var postFx = postFxSlots.slice()
                            postFx[slotIndex].occupied = false
                            postFxSlots = postFx
                        } else if (componentType === "AMPS") {
                            ampSlot = { x: ampSlot.x, y: ampSlot.y, occupied: false }
                        } else if (componentType === "CABINETS") {
                            cabSlot = { x: cabSlot.x, y: cabSlot.y, occupied: false }
                        }
                        
                        return true
                    }
                }
                return false
            }

            function getSortedComponentsOfType(componentType) {
                var components = []
                for (var i = 0; i < placedComponents.count; i++) {
                    var item = placedComponents.get(i)
                    if (item.componentType === componentType) {
                        components.push({ index: i, slotIndex: item.slotIndex, text: item.text })
                    }
                }
                components.sort(function(a, b) { return a.slotIndex - b.slotIndex })
                return components
            }

            function insertComponentWithShift(payload, targetSlotIndex) {
                // Disable animations since components are already at preview positions
                disableAnimations = true
                
                // Get all components of this type, sorted by slot
                var components = getSortedComponentsOfType(payload.componentType)
                
                // Remove all components of this type temporarily
                for (var i = placedComponents.count - 1; i >= 0; i--) {
                    if (placedComponents.get(i).componentType === payload.componentType) {
                        placedComponents.remove(i)
                    }
                }

                // Build new list with insertion
                var newComponents = []
                var inserted = false
                var newPosition = 0  // Track the new position as we build the list
                
                for (var j = 0; j < components.length; j++) {
                    // Skip the component being moved
                    if (payload.originalSlotIndex !== undefined && components[j].slotIndex === payload.originalSlotIndex) {
                        continue
                    }
                    
                    // Insert dragged component when we reach target position
                    if (newPosition === targetSlotIndex && !inserted) {
                        newComponents.push({ text: payload.text })
                        inserted = true
                        newPosition++
                    }
                    
                    // Add existing component
                    newComponents.push({ text: components[j].text })
                    newPosition++
                }
                
                // If not inserted yet (dragging to end or beyond), add it
                if (!inserted) {
                    newComponents.push({ text: payload.text })
                }

                // Reassign slot indices and get appropriate slots
                var slots = []
                if (payload.componentType === "PRE-FX") {
                    slots = preFxSlots
                } else if (payload.componentType === "POST-FX") {
                    slots = postFxSlots
                }

                // Re-add components with new positions
                for (var k = 0; k < newComponents.length && k < slots.length; k++) {
                    placedComponents.append({
                        text: newComponents[k].text,
                        componentType: payload.componentType,
                        x: slots[k].x,
                        y: slots[k].y,
                        slotIndex: k
                    })
                }

                // Update occupation
                if (payload.componentType === "PRE-FX") {
                    var preFx = preFxSlots.slice()
                    for (var m = 0; m < preFx.length; m++) {
                        preFx[m].occupied = m < newComponents.length
                    }
                    preFxSlots = preFx
                } else if (payload.componentType === "POST-FX") {
                    var postFx = postFxSlots.slice()
                    for (var n = 0; n < postFx.length; n++) {
                        postFx[n].occupied = n < newComponents.length
                    }
                    postFxSlots = postFx
                }
                
                // Clear all drag/preview state
                draggedComponent = null
                dragPreview = null
                shiftPreview = null
                dragStartZoomState = null
                dragZoomTransitionActive = false
                
                // Re-enable animations
                disableAnimations = false
            }

            function handleDrop(payload) {
                if (!payload || !payload.componentType)
                    return

                // Only place if dropped outside sidebar
                if (payload.isOverSidebar) {
                    console.log("Dropped over sidebar - canceling placement")
                    return
                }

                // Zoom state transitions already handled in updateDragPreview
                var dropX = payload.localPoint ? payload.localPoint.x : 0
                var dropY = payload.localPoint ? payload.localPoint.y : 0
                
                // Determine target grid for FX based on Y position (top or bottom half)
                var targetComponentType = payload.componentType
                if (payload.componentType === "PRE-FX" || payload.componentType === "POST-FX") {
                    var midY = mainContent.dropArea.height / 2
                    targetComponentType = dropY < midY ? "PRE-FX" : "POST-FX"
                }

                var slotInfo = null
                var shouldReplace = false
                var shouldInsert = false

                // For amps and cabinets, always use the single slot
                if (payload.componentType === "AMPS" || payload.componentType === "CABINETS") {
                    slotInfo = findNearestSlot(dropX, dropY, payload.componentType)
                    shouldReplace = slotInfo.slot.occupied
                } else {
                    // For FX, use insertion point detection
                    slotInfo = findInsertionPoint(dropX, dropY, targetComponentType)
                    
                    if (slotInfo && slotInfo.isBetween) {
                        // Between two components - insert with shift
                        shouldInsert = true
                    } else if (slotInfo && slotInfo.slot.occupied) {
                        // Over an occupied slot - insert with shift
                        shouldInsert = true
                    } else {
                        // Empty area - just place
                        shouldReplace = false
                    }
                }

                if (!slotInfo) {
                    console.log("No available slots for", payload.componentType)
                    return
                }

                // Handle insertion with shift for FX
                if (shouldInsert && (targetComponentType === "PRE-FX" || targetComponentType === "POST-FX")) {
                    // Check if rearranging and dropping in same position
                    if (payload.isRearranging && payload.originalSlotIndex === slotInfo.index) {
                        // Dropped in same position - no change needed
                        clearDragPreview()
                        return
                    }
                    // Use targetComponentType for insertion
                    var insertPayload = {
                        text: payload.text,
                        componentType: targetComponentType,
                        isRearranging: payload.isRearranging,
                        originalSlotIndex: payload.originalSlotIndex
                    }
                    insertComponentWithShift(insertPayload, slotInfo.index)
                    return
                }

                // Remove existing component if replacing
                if (shouldReplace) {
                    removeComponentAtSlot(targetComponentType, slotInfo.index)
                }

                // Remove original if rearranging
                if (payload.isRearranging && payload.originalSlotIndex !== undefined) {
                    removeComponentAtSlot(payload.componentType, payload.originalSlotIndex)
                }

                placedComponents.append({
                    text: payload.text || "",
                    componentType: targetComponentType,
                    x: slotInfo.slot.x,
                    y: slotInfo.slot.y,
                    slotIndex: slotInfo.index
                })

                // Mark slot as occupied
                if (targetComponentType === "PRE-FX") {
                    var preFx = preFxSlots.slice()
                    preFx[slotInfo.index].occupied = true
                    preFxSlots = preFx
                } else if (targetComponentType === "POST-FX") {
                    var postFx = postFxSlots.slice()
                    postFx[slotInfo.index].occupied = true
                    postFxSlots = postFx
                } else if (targetComponentType === "AMPS") {
                    ampSlot = { x: ampSlot.x, y: ampSlot.y, occupied: true }
                } else if (targetComponentType === "CABINETS") {
                    cabSlot = { x: cabSlot.x, y: cabSlot.y, occupied: true }
                }

                // Clear preview and drag state
                dragPreview = null
                dragStartZoomState = null
                dragZoomTransitionActive = false
            }

            function updateDragPreview(componentType, x, y, text, isOverSidebar, isRearranging) {
                // Save initial zoom state when drag starts
                if (dragStartZoomState === null) {
                    dragStartZoomState = {
                        preFxZoomed: preFxZoomed,
                        postFxZoomed: postFxZoomed,
                        singleFxZoomed: singleFxZoomed,
                        singleFxZoomType: singleFxZoomType,
                        singleFxZoomIndex: singleFxZoomIndex,
                        ampCabZoomed: ampCabZoomed,
                        previousZoomState: previousZoomState
                    }
                    dragZoomTransitionActive = false
                }

                // Don't show preview if over sidebar
                if (isOverSidebar) {
                    dragPreview = null
                    if (hoverTimer) {
                        hoverTimer.destroy()
                        hoverTimer = null
                    }
                    pendingPreview = null
                    
                    // Restore original zoom state when dragged back to sidebar
                    if (dragZoomTransitionActive && dragStartZoomState) {
                        preFxZoomed = dragStartZoomState.preFxZoomed
                        postFxZoomed = dragStartZoomState.postFxZoomed
                        singleFxZoomed = dragStartZoomState.singleFxZoomed
                        singleFxZoomType = dragStartZoomState.singleFxZoomType
                        singleFxZoomIndex = dragStartZoomState.singleFxZoomIndex
                        ampCabZoomed = dragStartZoomState.ampCabZoomed
                        previousZoomState = dragStartZoomState.previousZoomState
                        
                        if (singleFxZoomed) {
                            applySingleFxZoomPositions()
                        } else if (ampCabZoomed) {
                            applyAmpCabZoomPositions()
                        } else {
                            applyZoomState()
                        }
                        
                        // Reset the drag state tracking
                        dragStartZoomState = null
                        dragZoomTransitionActive = false
                    }
                    return
                }
                
                // Determine target grid for FX based on Y position (top or bottom half)
                var targetComponentType = componentType
                if (componentType === "PRE-FX" || componentType === "POST-FX") {
                    var midY = dropArea.height / 2
                    targetComponentType = y < midY ? "PRE-FX" : "POST-FX"
                }

                var slotInfo = null
                var requiresHover = false

                // For amps and cabinets, always preview at the single slot
                if (targetComponentType === "AMPS" || targetComponentType === "CABINETS") {
                    slotInfo = findNearestSlot(x, y, targetComponentType)
                } else {
                    // For FX, use insertion point detection
                    slotInfo = findInsertionPoint(x, y, targetComponentType)
                    
                    if (slotInfo && (slotInfo.isBetween || slotInfo.slot.occupied)) {
                        // Between components or over occupied - require hover delay
                        requiresHover = true
                    }
                }

                if (slotInfo) {
                    var previewData = {
                        componentType: targetComponentType,
                        x: slotInfo.slot.x,
                        y: slotInfo.slot.y,
                        text: text || "",
                        slotIndex: slotInfo.index,
                        isRearranging: isRearranging || false
                    }
                    
                    // If requires hover and not same as pending, start timer
                    if (requiresHover) {
                        var previewKey = targetComponentType + "-" + slotInfo.index
                        var pendingKey = pendingPreview ? (pendingPreview.componentType + "-" + pendingPreview.slotIndex) : null
                        
                        if (previewKey !== pendingKey) {
                            // New target - cancel old timer and start new one
                            if (hoverTimer) {
                                hoverTimer.destroy()
                            }
                            pendingPreview = previewData
                            
                            var timerObj = Qt.createQmlObject('import QtQuick 2.15; Timer {}', mainContent)
                            timerObj.interval = 1000
                            timerObj.repeat = false
                            timerObj.triggered.connect(function() {
                                if (pendingPreview) {
                                    // Apply zoom state transition when preview becomes active
                                    if (!dragZoomTransitionActive) {
                                        handleDragZoomTransition(componentType)
                                    }
                                    applyDragPreview(pendingPreview, targetComponentType, text, isRearranging)
                                }
                                timerObj.destroy()
                                hoverTimer = null
                            })
                            hoverTimer = timerObj
                            hoverTimer.start()
                        }
                        // Don't show preview immediately - wait for timer
                        return
                    } else {
                        // Immediate preview - clear any pending timer
                        if (hoverTimer) {
                            hoverTimer.destroy()
                            hoverTimer = null
                        }
                        pendingPreview = null
                        
                        // Apply zoom state transition when preview becomes active
                        if (!dragZoomTransitionActive) {
                            handleDragZoomTransition(componentType)
                        }
                        
                        applyDragPreview(previewData, targetComponentType, text, isRearranging)
                    }
                } else {
                    dragPreview = null
                    shiftPreview = null
                    if (hoverTimer) {
                        hoverTimer.destroy()
                        hoverTimer = null
                    }
                    pendingPreview = null
                }
            }
            
            function handleDragZoomTransition(draggedType) {
                if (singleFxZoomed) {
                    // Single FX zoom: check if same type or different
                    if (draggedType === singleFxZoomType) {
                        // Same type: zoom to group focus for that FX type
                        singleFxZoomed = false
                        singleFxZoomType = ""
                        singleFxZoomIndex = -1
                        previousZoomState = "normal"
                        ampCabZoomed = false
                        
                        if (draggedType === "PRE-FX") {
                            preFxZoomed = true
                            postFxZoomed = false
                        } else if (draggedType === "POST-FX") {
                            preFxZoomed = false
                            postFxZoomed = true
                        }
                        applyZoomState()
                    } else {
                        // Different type: zoom to overview
                        singleFxZoomed = false
                        singleFxZoomType = ""
                        singleFxZoomIndex = -1
                        previousZoomState = "normal"
                        ampCabZoomed = false
                        preFxZoomed = false
                        postFxZoomed = false
                        applyZoomState()
                    }
                    dragZoomTransitionActive = true
                } else if (preFxZoomed) {
                    // Pre-FX group focus: if not PRE-FX, zoom to overview
                    if (draggedType !== "PRE-FX") {
                        preFxZoomed = false
                        postFxZoomed = false
                        applyZoomState()
                        dragZoomTransitionActive = true
                    }
                } else if (postFxZoomed) {
                    // Post-FX group focus: if not POST-FX, zoom to overview
                    if (draggedType !== "POST-FX") {
                        preFxZoomed = false
                        postFxZoomed = false
                        applyZoomState()
                        dragZoomTransitionActive = true
                    }
                } else if (ampCabZoomed) {
                    // Amp/Cab zoom: if FX component, zoom to overview
                    if (draggedType === "PRE-FX" || draggedType === "POST-FX") {
                        exitAmpCabZoom()
                        dragZoomTransitionActive = true
                    }
                }
            }

            function applyDragPreview(previewData, targetComponentType, text, isRearranging) {
                dragPreview = previewData

                // Calculate shift preview for insertion (both new components and rearrangement)
                if ((targetComponentType === "PRE-FX" || targetComponentType === "POST-FX")) {
                    var slotInfo = { index: previewData.slotIndex, slot: { x: previewData.x, y: previewData.y } }
                    var components = getSortedComponentsOfType(targetComponentType)
                    var slots = targetComponentType === "PRE-FX" ? preFxSlots : postFxSlots
                    var preview = {}
                    
                    // Find the dragged component's original slot (if rearranging)
                    var draggedOriginalSlot = -1
                    if (isRearranging && mainContent.draggedComponent && mainContent.draggedComponent.componentType === targetComponentType) {
                        draggedOriginalSlot = mainContent.draggedComponent.slotIndex
                    }
                    
                    // Always show shift preview when there are occupied slots
                    var showShift = components.length > 0
                    
                    if (showShift) {
                        // Build new order with dragged/new component inserted at target
                        var newOrder = []
                        var inserted = false
                        var newPosition = 0  // Track position in new order
                        
                        for (var i = 0; i < components.length; i++) {
                            var comp = components[i]
                            
                            // Insert dragged/new component when we reach target position
                            if (newPosition === slotInfo.index && !inserted) {
                                newOrder.push({ originalSlot: draggedOriginalSlot, text: text })
                                inserted = true
                                newPosition++
                            }
                            
                            // Skip the dragged component's original position (if rearranging)
                            if (isRearranging && comp.slotIndex === draggedOriginalSlot) {
                                continue
                            }
                            
                            newOrder.push({ originalSlot: comp.slotIndex, text: comp.text })
                            newPosition++
                        }
                        
                        // If not inserted yet (dragging to end or beyond), add it
                        if (!inserted) {
                            newOrder.push({ originalSlot: draggedOriginalSlot, text: text })
                        }
                        
                        // Map original slots to new positions
                        for (var j = 0; j < newOrder.length && j < slots.length; j++) {
                            if (newOrder[j].originalSlot !== -1) {
                                preview[newOrder[j].originalSlot] = {
                                    x: slots[j].x,
                                    y: slots[j].y,
                                    slotIndex: j
                                }
                            }
                        }
                        
                        shiftPreview = preview
                    } else {
                        shiftPreview = null
                    }
                } else {
                    shiftPreview = null
                }
            }

            function clearDragPreview() {
                // Restore zoom state if a transition occurred during drag
                if (dragZoomTransitionActive && dragStartZoomState) {
                    preFxZoomed = dragStartZoomState.preFxZoomed
                    postFxZoomed = dragStartZoomState.postFxZoomed
                    singleFxZoomed = dragStartZoomState.singleFxZoomed
                    singleFxZoomType = dragStartZoomState.singleFxZoomType
                    singleFxZoomIndex = dragStartZoomState.singleFxZoomIndex
                    ampCabZoomed = dragStartZoomState.ampCabZoomed
                    previousZoomState = dragStartZoomState.previousZoomState
                    
                    if (singleFxZoomed) {
                        applySingleFxZoomPositions()
                    } else if (ampCabZoomed) {
                        applyAmpCabZoomPositions()
                    } else {
                        applyZoomState()
                    }
                }
                
                dragPreview = null
                shiftPreview = null
                draggedComponent = null
                dragStartZoomState = null
                dragZoomTransitionActive = false
                if (hoverTimer) {
                    hoverTimer.destroy()
                    hoverTimer = null
                }
                pendingPreview = null
            }

            property alias dropArea: workspaceContent
            property var dropController: mainContent

            Item {
                id: contentFrame
                anchors.fill: parent

                PinchArea {
                    id: fxPinchArea
                    anchors.fill: parent
                    
                    property real startScale: 1.0
                    property bool pinchHandled: false

                    onPinchStarted: {
                        startScale = pinch.scale
                        pinchHandled = false
                    }

                    onPinchUpdated: {
                        if (pinchHandled) return
                        
                        if (!mainContent.singleFxZoomed && pinch.scale > startScale * 1.3) {
                            // Pinch zoom in - find FX slot under pinch center (works on empty & occupied)
                            pinchHandled = true
                            var centerX = pinch.center.x
                            var centerY = pinch.center.y

                            // Check Pre-FX slots
                            var preFxW = (mainContent.preFxZoomed ? mainContent.zoomedFxWidth : mainContent.normalFxWidth) * mainContent.contentScale
                            var preFxH = (mainContent.preFxZoomed ? mainContent.zoomedFxHeight : mainContent.normalFxHeight) * mainContent.contentScale
                            for (var i = 0; i < 6; i++) {
                                var pSlot = mainContent.preFxSlots[i]
                                var px = pSlot.x * mainContent.contentScale + mainContent.contentOffsetX
                                var py = pSlot.y * mainContent.contentScale + mainContent.contentOffsetY
                                if (centerX >= px && centerX <= px + preFxW &&
                                    centerY >= py && centerY <= py + preFxH) {
                                    mainContent.enterSingleFxZoom("PRE-FX", i)
                                    return
                                }
                            }

                            // Check Post-FX slots
                            var postFxW = (mainContent.postFxZoomed ? mainContent.zoomedFxWidth : mainContent.normalFxWidth) * mainContent.contentScale
                            var postFxH = (mainContent.postFxZoomed ? mainContent.zoomedFxHeight : mainContent.normalFxHeight) * mainContent.contentScale
                            for (var j = 0; j < 6; j++) {
                                var qSlot = mainContent.postFxSlots[j]
                                var qx = qSlot.x * mainContent.contentScale + mainContent.contentOffsetX
                                var qy = qSlot.y * mainContent.contentScale + mainContent.contentOffsetY
                                if (centerX >= qx && centerX <= qx + postFxW &&
                                    centerY >= qy && centerY <= qy + postFxH) {
                                    mainContent.enterSingleFxZoom("POST-FX", j)
                                    return
                                }
                            }

                            // Check Amp slot
                            var ampX = mainContent.ampSlot.x * mainContent.contentScale + mainContent.contentOffsetX
                            var ampY = mainContent.ampSlot.y * mainContent.contentScale + mainContent.contentOffsetY
                            var ampW = (mainContent.ampCabZoomed ? mainContent.zoomedAmpWidth : 530) * mainContent.contentScale
                            var ampH = (mainContent.ampCabZoomed ? mainContent.zoomedAmpHeight : 221) * mainContent.contentScale
                            if (centerX >= ampX && centerX <= ampX + ampW &&
                                centerY >= ampY && centerY <= ampY + ampH) {
                                mainContent.enterAmpCabZoom()
                                return
                            }

                            // Check Cab slot
                            var cabX = mainContent.cabSlot.x * mainContent.contentScale + mainContent.contentOffsetX
                            var cabY = mainContent.cabSlot.y * mainContent.contentScale + mainContent.contentOffsetY
                            var cabW = (mainContent.ampCabZoomed ? mainContent.zoomedCabWidth : 201) * mainContent.contentScale
                            var cabH = (mainContent.ampCabZoomed ? mainContent.zoomedCabHeight : 136) * mainContent.contentScale
                            if (centerX >= cabX && centerX <= cabX + cabW &&
                                centerY >= cabY && centerY <= cabY + cabH) {
                                mainContent.enterAmpCabZoom()
                                return
                            }
                        } else if ((mainContent.singleFxZoomed || mainContent.ampCabZoomed) && pinch.scale < startScale * 0.7) {
                            // Pinch zoom out - exit current zoom
                            pinchHandled = true
                            if (mainContent.singleFxZoomed) {
                                mainContent.exitSingleFxZoom()
                            } else {
                                mainContent.exitAmpCabZoom()
                            }
                        }
                    }

                    Item {
                        id: workspaceBackground
                        anchors.fill: parent

                        Item {
                            id: workspaceContent
                            anchors.fill: parent
                        }

                    // Pre-FX slot outlines
                    Repeater {
                        model: 6
                        Rectangle {
                            x: mainContent.preFxSlots[index].x * mainContent.contentScale + mainContent.contentOffsetX
                            y: mainContent.preFxSlots[index].y * mainContent.contentScale + mainContent.contentOffsetY
                            width: (mainContent.singleFxZoomed && mainContent.singleFxZoomType === "PRE-FX" ? mainContent.singleFxWidth : mainContent.preFxZoomed ? mainContent.zoomedFxWidth : mainContent.normalFxWidth) * mainContent.contentScale
                            height: (mainContent.singleFxZoomed && mainContent.singleFxZoomType === "PRE-FX" ? mainContent.singleFxHeight : mainContent.preFxZoomed ? mainContent.zoomedFxHeight : mainContent.normalFxHeight) * mainContent.contentScale
                            radius: 3
                            color: mainContent.preFxSlots[index].occupied ? "transparent" : "#33964D26"
                            border.width: 2.5
                            border.color: mainContent.preFxSlots[index].occupied ? "transparent" : "#33964D"
                            opacity: mainContent.singleFxZoomed ? (mainContent.singleFxZoomType === "PRE-FX" ? 0.1 : 0.0) : mainContent.postFxZoomed ? 0.0 : 0.1

                            Behavior on x { NumberAnimation { duration: mainContent.zoomDuration; easing.type: Easing.OutCubic } }
                            Behavior on y { NumberAnimation { duration: mainContent.zoomDuration; easing.type: Easing.OutCubic } }
                            Behavior on width { NumberAnimation { duration: mainContent.zoomDuration; easing.type: Easing.OutCubic } }
                            Behavior on height { NumberAnimation { duration: mainContent.zoomDuration; easing.type: Easing.OutCubic } }
                            Behavior on opacity { NumberAnimation { duration: mainContent.zoomDuration; easing.type: Easing.OutCubic } }

                            MouseArea {
                                anchors.fill: parent
                                enabled: !mainContent.preFxSlots[index].occupied
                                propagateComposedEvents: true
                                onPressed: mouse.accepted = false
                                onPositionChanged: mouse.accepted = false
                                onReleased: mouse.accepted = false
                                onDoubleClicked: {
                                    mainContent.enterSingleFxZoom("PRE-FX", index)
                                }
                            }
                        }
                    }

                    // Post-FX slot outlines
                    Repeater {
                        model: 6
                        Rectangle {
                            x: mainContent.postFxSlots[index].x * mainContent.contentScale + mainContent.contentOffsetX
                            y: mainContent.postFxSlots[index].y * mainContent.contentScale + mainContent.contentOffsetY
                            width: (mainContent.singleFxZoomed && mainContent.singleFxZoomType === "POST-FX" ? mainContent.singleFxWidth : mainContent.postFxZoomed ? mainContent.zoomedFxWidth : mainContent.normalFxWidth) * mainContent.contentScale
                            height: (mainContent.singleFxZoomed && mainContent.singleFxZoomType === "POST-FX" ? mainContent.singleFxHeight : mainContent.postFxZoomed ? mainContent.zoomedFxHeight : mainContent.normalFxHeight) * mainContent.contentScale
                            radius: 3
                            color: mainContent.postFxSlots[index].occupied ? "transparent" : "#30768F26"
                            border.width: 2.5
                            border.color: mainContent.postFxSlots[index].occupied ? "transparent" : "#30768F"
                            opacity: mainContent.singleFxZoomed ? (mainContent.singleFxZoomType === "POST-FX" ? 0.1 : 0.0) : mainContent.preFxZoomed ? 0.0 : 0.1

                            Behavior on x { NumberAnimation { duration: mainContent.zoomDuration; easing.type: Easing.OutCubic } }
                            Behavior on y { NumberAnimation { duration: mainContent.zoomDuration; easing.type: Easing.OutCubic } }
                            Behavior on width { NumberAnimation { duration: mainContent.zoomDuration; easing.type: Easing.OutCubic } }
                            Behavior on height { NumberAnimation { duration: mainContent.zoomDuration; easing.type: Easing.OutCubic } }
                            Behavior on opacity { NumberAnimation { duration: mainContent.zoomDuration; easing.type: Easing.OutCubic } }

                            MouseArea {
                                anchors.fill: parent
                                enabled: !mainContent.postFxSlots[index].occupied
                                propagateComposedEvents: true
                                onPressed: mouse.accepted = false
                                onPositionChanged: mouse.accepted = false
                                onReleased: mouse.accepted = false
                                onDoubleClicked: {
                                    mainContent.enterSingleFxZoom("POST-FX", index)
                                }
                            }
                        }
                    }

                    // Amp slot outline
                    Rectangle {
                        x: mainContent.ampSlot.x * mainContent.contentScale + mainContent.contentOffsetX
                        y: mainContent.ampSlot.y * mainContent.contentScale + mainContent.contentOffsetY
                        width: (mainContent.ampCabZoomed ? mainContent.zoomedAmpWidth : 530) * mainContent.contentScale
                        height: (mainContent.ampCabZoomed ? mainContent.zoomedAmpHeight : 221) * mainContent.contentScale
                        radius: 9.4
                        color: mainContent.ampSlot.occupied ? "transparent" : "#CF6C4226"
                        border.width: 2.5
                        border.color: mainContent.ampSlot.occupied ? "transparent" : "#CF6C42"
                        opacity: mainContent.singleFxZoomed ? 0.0 : mainContent.singleFxZoomed ? 0.0 : (mainContent.preFxZoomed || mainContent.postFxZoomed) ? 0.0 : 0.1

                        Behavior on x { NumberAnimation { duration: mainContent.zoomDuration; easing.type: Easing.OutCubic } }
                        Behavior on y { NumberAnimation { duration: mainContent.zoomDuration; easing.type: Easing.OutCubic } }
                        Behavior on width { NumberAnimation { duration: mainContent.zoomDuration; easing.type: Easing.OutCubic } }
                        Behavior on height { NumberAnimation { duration: mainContent.zoomDuration; easing.type: Easing.OutCubic } }
                        Behavior on opacity { NumberAnimation { duration: mainContent.zoomDuration; easing.type: Easing.OutCubic } }

                        MouseArea {
                            anchors.fill: parent
                            enabled: !mainContent.ampSlot.occupied
                            propagateComposedEvents: true
                            onPressed: mouse.accepted = false
                            onPositionChanged: mouse.accepted = false
                            onReleased: mouse.accepted = false
                            onDoubleClicked: {
                                mainContent.enterAmpCabZoom()
                            }
                        }
                    }

                    // Cab slot outline
                    Rectangle {
                        x: mainContent.cabSlot.x * mainContent.contentScale + mainContent.contentOffsetX
                        y: mainContent.cabSlot.y * mainContent.contentScale + mainContent.contentOffsetY
                        width: (mainContent.ampCabZoomed ? mainContent.zoomedCabWidth : 201) * mainContent.contentScale
                        height: (mainContent.ampCabZoomed ? mainContent.zoomedCabHeight : 136) * mainContent.contentScale
                        radius: 3
                        color: mainContent.cabSlot.occupied ? "transparent" : "#B18F6026"
                        border.width: 2.5
                        border.color: mainContent.cabSlot.occupied ? "transparent" : "#B18F60"
                        opacity: mainContent.singleFxZoomed ? 0.0 : (mainContent.preFxZoomed || mainContent.postFxZoomed) ? 0.0 : 0.1

                        Behavior on x { NumberAnimation { duration: mainContent.zoomDuration; easing.type: Easing.OutCubic } }
                        Behavior on y { NumberAnimation { duration: mainContent.zoomDuration; easing.type: Easing.OutCubic } }
                        Behavior on opacity { NumberAnimation { duration: mainContent.zoomDuration; easing.type: Easing.OutCubic } }

                        MouseArea {
                            anchors.fill: parent
                            enabled: !mainContent.cabSlot.occupied
                            propagateComposedEvents: true
                            onPressed: mouse.accepted = false
                            onPositionChanged: mouse.accepted = false
                            onReleased: mouse.accepted = false
                            onDoubleClicked: {
                                mainContent.enterAmpCabZoom()
                            }
                        }
                    }

                    // Drag preview
                    Rectangle {
                        visible: mainContent.dragPreview !== null
                        x: mainContent.dragPreview ? mainContent.dragPreview.x * mainContent.contentScale + mainContent.contentOffsetX : 0
                        y: mainContent.dragPreview ? mainContent.dragPreview.y * mainContent.contentScale + mainContent.contentOffsetY : 0
                        width: {
                            if (!mainContent.dragPreview) return 0
                            var baseWidth = 72
                            if (mainContent.dragPreview.componentType === "AMPS") baseWidth = 530
                            if (mainContent.dragPreview.componentType === "CABINETS") baseWidth = 201
                            return baseWidth * mainContent.contentScale
                        }
                        height: {
                            if (!mainContent.dragPreview) return 0
                            var baseHeight = 112
                            if (mainContent.dragPreview.componentType === "AMPS") baseHeight = 221
                            if (mainContent.dragPreview.componentType === "CABINETS") baseHeight = 136
                            return baseHeight * mainContent.contentScale
                        }
                        radius: mainContent.dragPreview && mainContent.dragPreview.componentType === "AMPS" ? 9.4 : 3
                        color: "#2C2C2C"
                        opacity: 0.6
                        border.width: 2.5
                        border.color: {
                            if (!mainContent.dragPreview) return "transparent"
                            if (mainContent.dragPreview.componentType === "PRE-FX") return "#33964D"
                            if (mainContent.dragPreview.componentType === "AMPS") return "#CF6C42"
                            if (mainContent.dragPreview.componentType === "CABINETS") return "#B18F60"
                            if (mainContent.dragPreview.componentType === "POST-FX") return "#30768F"
                            return "#FFFFFF"
                        }
                        z: 10

                        Text {
                            anchors.centerIn: parent
                            text: mainContent.dragPreview ? mainContent.dragPreview.text : ""
                            color: "#FFFFFF"
                            font.pixelSize: 14 * mainContent.contentScale
                            font.weight: Font.DemiBold
                            opacity: 0.8
                        }
                    }

                    // Placed components
                    Repeater {
                        model: placedComponents
                        delegate: Item {
                            property var previewPos: mainContent.shiftPreview && mainContent.shiftPreview[model.slotIndex]
                            property real targetX: previewPos ? previewPos.x : model.x
                            property real targetY: previewPos ? previewPos.y : model.y
                            
                            x: targetX * mainContent.contentScale + mainContent.contentOffsetX
                            y: targetY * mainContent.contentScale + mainContent.contentOffsetY
                            width: {
                                if (mainContent.singleFxZoomed && (model.componentType === "PRE-FX" || model.componentType === "POST-FX") && model.componentType === mainContent.singleFxZoomType) {
                                    return mainContent.singleFxWidth * mainContent.contentScale
                                }
                                if (model.componentType === "PRE-FX") {
                                    return (mainContent.preFxZoomed ? mainContent.zoomedFxWidth : mainContent.normalFxWidth) * mainContent.contentScale
                                }
                                if (model.componentType === "POST-FX") {
                                    return (mainContent.postFxZoomed ? mainContent.zoomedFxWidth : mainContent.normalFxWidth) * mainContent.contentScale
                                }
                                if (model.componentType === "AMPS") return (mainContent.ampCabZoomed ? mainContent.zoomedAmpWidth : 530) * mainContent.contentScale
                                if (model.componentType === "CABINETS") return (mainContent.ampCabZoomed ? mainContent.zoomedCabWidth : 201) * mainContent.contentScale
                                return 72 * mainContent.contentScale
                            }
                            height: {
                                if (mainContent.singleFxZoomed && (model.componentType === "PRE-FX" || model.componentType === "POST-FX") && model.componentType === mainContent.singleFxZoomType) {
                                    return mainContent.singleFxHeight * mainContent.contentScale
                                }
                                if (model.componentType === "PRE-FX") {
                                    return (mainContent.preFxZoomed ? mainContent.zoomedFxHeight : mainContent.normalFxHeight) * mainContent.contentScale
                                }
                                if (model.componentType === "POST-FX") {
                                    return (mainContent.postFxZoomed ? mainContent.zoomedFxHeight : mainContent.normalFxHeight) * mainContent.contentScale
                                }
                                if (model.componentType === "AMPS") return (mainContent.ampCabZoomed ? mainContent.zoomedAmpHeight : 221) * mainContent.contentScale
                                if (model.componentType === "CABINETS") return (mainContent.ampCabZoomed ? mainContent.zoomedCabHeight : 136) * mainContent.contentScale
                                return 112 * mainContent.contentScale
                            }
                            z: 1
                            opacity: {
                                // Hide if this is the component being dragged
                                if (mainContent.draggedComponent &&
                                    mainContent.draggedComponent.componentType === model.componentType &&
                                    mainContent.draggedComponent.slotIndex === model.slotIndex) {
                                    return 0.0
                                }
                                // Dim preview for new component being inserted
                                // Only dim if at preview slot AND not an existing component being shifted
                                if (mainContent.dragPreview &&
                                    mainContent.dragPreview.componentType === model.componentType &&
                                    mainContent.dragPreview.slotIndex === model.slotIndex &&
                                    mainContent.shiftPreview !== null) {
                                    // Check if this component is in the shift preview (existing component)
                                    if (mainContent.shiftPreview[model.slotIndex] !== undefined) {
                                        return 1.0  // Full opacity for components being shifted
                                    }
                                    return 0.4  // Preview opacity only for the new component position
                                }
                                return 1.0
                            }

                            Behavior on x { 
                                enabled: !mainContent.disableAnimations
                                NumberAnimation { duration: mainContent.zoomDuration; easing.type: Easing.OutCubic } 
                            }
                            Behavior on y { 
                                enabled: !mainContent.disableAnimations
                                NumberAnimation { duration: mainContent.zoomDuration; easing.type: Easing.OutCubic } 
                            }
                            Behavior on width { NumberAnimation { duration: mainContent.zoomDuration; easing.type: Easing.OutCubic } }
                            Behavior on height { NumberAnimation { duration: mainContent.zoomDuration; easing.type: Easing.OutCubic } }

                            Rectangle {
                                anchors.fill: parent
                                radius: model.componentType === "AMPS" ? 9.4 : 3
                                color: "#2C2C2C"
                                border.width: 2.5
                                border.color: {
                                    if (model.componentType === "PRE-FX") return "#33964D"
                                    if (model.componentType === "AMPS") return "#CF6C42"
                                    if (model.componentType === "CABINETS") return "#B18F60"
                                    if (model.componentType === "POST-FX") return "#30768F"
                                    return "#FFFFFF"
                                }
                            }

                            Text {
                                anchors.centerIn: parent
                                text: model.text
                                color: "#FFFFFF"
                                font.pixelSize: 14 * mainContent.contentScale
                                font.weight: Font.DemiBold
                                
                                Behavior on font.pixelSize { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
                            }

                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton
                                pressAndHoldInterval: 700
                                propagateComposedEvents: true
                                
                                property bool isDragging: false
                                property var dragProxy: null
                                property real pressX: 0
                                property real pressY: 0
                                property real dragOffsetX: 0
                                property real dragOffsetY: 0

                                // Pinch detection properties
                                property bool pinchActive: false
                                property real initialPinchDistance: 0
                                property bool pinchHandled: false

                                onPressed: {
                                    pressX = mouse.x
                                    pressY = mouse.y
                                    mouse.accepted = false
                                }

                                onDoubleClicked: {
                                    // Double-click to zoom into single FX view
                                    if (model.componentType === "PRE-FX" || model.componentType === "POST-FX") {
                                        mainContent.enterSingleFxZoom(model.componentType, model.slotIndex)
                                    } else if (model.componentType === "AMPS" || model.componentType === "CABINETS") {
                                        mainContent.enterAmpCabZoom()
                                    }
                                    mouse.accepted = true
                                }

                                onPressAndHold: {
                                    // Start rearranging after 0.7s hold
                                    mainContent.draggedComponent = {
                                        componentType: model.componentType,
                                        slotIndex: model.slotIndex,
                                        text: model.text
                                    }
                                    
                                    var origin = parent.mapToItem(root.dragSurface, 0, 0)
                                    dragProxy = dragProxyComponent.createObject(root.dragSurface, {
                                        x: origin.x,
                                        y: origin.y,
                                        width: parent.width,
                                        height: parent.height,
                                        componentText: model.text,
                                        componentType: model.componentType,
                                        opacity: 0.7,
                                        z: 1000
                                    })

                                    if (dragProxy) {
                                        dragOffsetX = pressX
                                        dragOffsetY = pressY
                                        isDragging = true
                                        parent.opacity = 0.3
                                        mouse.accepted = true
                                    }
                                }

                                onPositionChanged: {
                                    if (isDragging && dragProxy) {
                                        var mapped = mapToItem(root.dragSurface, mouse.x, mouse.y)
                                        dragProxy.x = mapped.x - dragOffsetX
                                        dragProxy.y = mapped.y - dragOffsetY

                                        // Check if over removal zones
                                        var isOverSidebar = (dragProxy.x + dragProxy.width / 2) < root.sidebarWidth
                                        var proxyCenter = dragProxy.mapToItem(root, dragProxy.width / 2, dragProxy.height / 2)
                                        var edgeThreshold = 50
                                        var isAtEdge = proxyCenter.x < edgeThreshold || 
                                                      proxyCenter.x > (root.width - edgeThreshold) ||
                                                      proxyCenter.y < edgeThreshold ||
                                                      proxyCenter.y > (root.height - edgeThreshold)
                                        
                                        // Visual feedback: dim when over removal zone
                                        if (isOverSidebar || isAtEdge) {
                                            dragProxy.opacity = 0.3
                                        } else {
                                            dragProxy.opacity = 0.7
                                        }

                                        // Update preview
                                        var centerInTarget = dragProxy.mapToItem(mainContent.dropArea, dragProxy.width / 2, dragProxy.height / 2)
                                        if (mainContent.dropController && mainContent.dropController.updateDragPreview) {
                                            mainContent.dropController.updateDragPreview(model.componentType, centerInTarget.x, centerInTarget.y, model.text, isOverSidebar, true)
                                        }
                                    } else {
                                        mouse.accepted = false
                                    }
                                }

                                onReleased: {
                                    if (isDragging && dragProxy) {
                                        var isOverSidebar = (dragProxy.x + dragProxy.width / 2) < root.sidebarWidth
                                        
                                        // Check if dragged to screen edges (left, right, top, or bottom)
                                        var proxyCenter = dragProxy.mapToItem(root, dragProxy.width / 2, dragProxy.height / 2)
                                        var edgeThreshold = 50
                                        var isAtEdge = proxyCenter.x < edgeThreshold || 
                                                      proxyCenter.x > (root.width - edgeThreshold) ||
                                                      proxyCenter.y < edgeThreshold ||
                                                      proxyCenter.y > (root.height - edgeThreshold)
                                        
                                        // Remove component if dropped on sidebar or at screen edge
                                        if (isOverSidebar || isAtEdge) {
                                            mainContent.removeComponentAtSlot(model.componentType, model.slotIndex)
                                            
                                            dragProxy.destroy()
                                            dragProxy = null
                                            isDragging = false
                                            parent.opacity = 1.0
                                            
                                            mainContent.draggedComponent = null
                                            if (mainContent.dropController && mainContent.dropController.clearDragPreview) {
                                                mainContent.dropController.clearDragPreview()
                                            }
                                        } else {
                                            // Normal drop - rearrange component
                                            var center = dragProxy.mapToItem(mainContent.dropArea, dragProxy.width / 2, dragProxy.height / 2)
                                            mainContent.handleDrop({
                                                text: model.text,
                                                componentType: model.componentType,
                                                localPoint: center,
                                                isOverSidebar: false,
                                                isRearranging: true,
                                                originalSlotIndex: model.slotIndex
                                            })

                                            dragProxy.destroy()
                                            dragProxy = null
                                            isDragging = false
                                            parent.opacity = 1.0
                                        }
                                    } else {
                                        mouse.accepted = false
                                    }
                                }

                                onCanceled: {
                                    if (dragProxy) {
                                        dragProxy.destroy()
                                        dragProxy = null
                                    }
                                    isDragging = false
                                    parent.opacity = 1.0

                                    mainContent.draggedComponent = null
                                    if (mainContent.dropController && mainContent.dropController.clearDragPreview) {
                                        mainContent.dropController.clearDragPreview()
                                    }
                                }
                            }
                        }
                    }  // Repeater (placedComponents)

                    // Swipe detection (inside PinchArea so it receives events)
                    MouseArea {
                        anchors.fill: parent
                        z: -1  // Behind components so it doesn't block drag/drop
                        property real swipeStartY: 0
                        property real swipeStartX: 0
                        property bool swiping: false
                        readonly property real swipeThreshold: 60

                        onPressed: {
                            swipeStartY = mouse.y
                            swipeStartX = mouse.x
                            swiping = true
                        }

                        onPositionChanged: {
                            if (!swiping) return
                            var deltaY = mouse.y - swipeStartY
                            var deltaX = mouse.x - swipeStartX

                            if (mainContent.singleFxZoomed) {
                                // In single FX zoom: horizontal swipes page through FX
                                if (Math.abs(deltaX) > swipeThreshold && Math.abs(deltaY) < Math.abs(deltaX)) {
                                    swiping = false
                                    if (deltaX < 0) {
                                        mainContent.pageSingleFx(1)
                                    } else {
                                        mainContent.pageSingleFx(-1)
                                    }
                                }
                            } else if (mainContent.ampCabZoomed) {
                                // In amp/cab zoom: swipe down → Pre-FX, swipe up → Post-FX
                                if (Math.abs(deltaY) > swipeThreshold && Math.abs(deltaX) < Math.abs(deltaY)) {
                                    swiping = false
                                    mainContent.ampCabZoomed = false
                                    if (deltaY > 0) {
                                        mainContent.preFxZoomed = true
                                        mainContent.postFxZoomed = false
                                    } else {
                                        mainContent.preFxZoomed = false
                                        mainContent.postFxZoomed = true
                                    }
                                    mainContent.applyZoomState()
                                }
                            } else {
                                // Normal/focused views: vertical swipes for zoom
                                if (Math.abs(deltaY) > swipeThreshold && Math.abs(deltaX) < Math.abs(deltaY)) {
                                    swiping = false
                                    if (deltaY > 0) {
                                        if (mainContent.postFxZoomed) {
                                            mainContent.togglePostFxZoom()
                                        } else if (!mainContent.preFxZoomed) {
                                            mainContent.togglePreFxZoom()
                                        }
                                    } else if (deltaY < 0) {
                                        if (mainContent.preFxZoomed) {
                                            mainContent.togglePreFxZoom()
                                        } else if (!mainContent.postFxZoomed) {
                                            mainContent.togglePostFxZoom()
                                        }
                                    }
                                }
                            }
                        }

                        onReleased: {
                            swiping = false
                        }
                    }
                    }  // workspaceBackground
                }  // PinchArea
            }  // contentFrame
        }
    }

    Component {
        id: dragProxyComponent
        Item {
            property string componentText: ""
            property string componentType: ""

            Rectangle {
                anchors.fill: parent
                radius: componentType === "AMPS" ? 9.4 : 3
                color: "#2C2C2C"
                border.width: 2.5
                border.color: {
                    if (componentType === "PRE-FX") return "#33964D"
                    if (componentType === "AMPS") return "#CF6C42"
                    if (componentType === "CABINETS") return "#B18F60"
                    if (componentType === "POST-FX") return "#30768F"
                    return "#FFFFFF"
                }
            }

            Text {
                anchors.centerIn: parent
                text: componentText
                color: "#FFFFFF"
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
        }
    }

    Item {
        id: dragLayer
        anchors.fill: parent
        z: 999
    }
}
