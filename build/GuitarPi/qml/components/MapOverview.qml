import QtQuick 2.15

Item {
    id: root
    property real maxWidth: 166
    property real maxHeight: 136
    readonly property real workspaceAspect: {
        var aspect = workspaceWidth > 0 && workspaceHeight > 0 ? workspaceWidth / workspaceHeight : 1
        return aspect > 0 ? aspect : 1
    }
    readonly property real containerAspect: maxWidth / maxHeight
    readonly property real fittedWidth: workspaceAspect >= containerAspect ? maxWidth : Math.min(maxWidth, maxHeight * workspaceAspect)
    readonly property real fittedHeight: workspaceAspect >= containerAspect ? Math.min(maxHeight, maxWidth / workspaceAspect) : maxHeight
    width: fittedWidth
    height: fittedHeight

    property real workspaceWidth: 1
    property real workspaceHeight: 1
    property real viewWidth: 1
    property real viewHeight: 1
    property real contentX: 0
    property real contentY: 0
    property var itemsModel: null
    property real tileWidth: 1
    property real tileHeight: 1

    signal requestViewPosition(real x, real y)

    readonly property real frameBorder: 2
    readonly property real availableWidth: Math.max(0, width - frameBorder * 2)
    readonly property real availableHeight: Math.max(0, height - frameBorder * 2)
    readonly property real workspaceScale: {
        var w = workspaceWidth > 0 ? workspaceWidth : 1
        var h = workspaceHeight > 0 ? workspaceHeight : 1
        var scaleCandidate = Math.min(availableWidth / w, availableHeight / h)
        if (!isFinite(scaleCandidate) || scaleCandidate <= 0)
            scaleCandidate = 1
        return scaleCandidate
    }
    readonly property real effectiveWorkspaceWidth: workspaceWidth * workspaceScale
    readonly property real effectiveWorkspaceHeight: workspaceHeight * workspaceScale
    readonly property real viewportWidthRatio: workspaceWidth > 0 ? Math.min(1, viewWidth / workspaceWidth) : 1
    readonly property real viewportHeightRatio: workspaceHeight > 0 ? Math.min(1, viewHeight / workspaceHeight) : 1
    readonly property real viewportWidthPx: effectiveWorkspaceWidth * viewportWidthRatio
    readonly property real viewportHeightPx: effectiveWorkspaceHeight * viewportHeightRatio
    readonly property real workspaceOffsetX: frameBorder + (availableWidth - effectiveWorkspaceWidth) * 0.5
    readonly property real workspaceOffsetY: frameBorder + (availableHeight - effectiveWorkspaceHeight) * 0.5
    readonly property real normalizedContentX: {
        if (workspaceWidth <= 0)
            return 0
        var maxNorm = Math.max(0, 1 - viewportWidthRatio)
        var n = contentX / workspaceWidth
        if (!isFinite(n))
            n = 0
        return Math.min(Math.max(n, 0), maxNorm)
    }
    readonly property real normalizedContentY: {
        if (workspaceHeight <= 0)
            return 0
        var maxNorm = Math.max(0, 1 - viewportHeightRatio)
        var n = contentY / workspaceHeight
        if (!isFinite(n))
            n = 0
        return Math.min(Math.max(n, 0), maxNorm)
    }
    readonly property real viewportOffsetX: {
        var offset = normalizedContentX * effectiveWorkspaceWidth
        var maxOffset = Math.max(0, effectiveWorkspaceWidth - viewportWidthPx)
        return Math.min(Math.max(offset, 0), maxOffset)
    }
    readonly property real viewportOffsetY: {
        var offset = normalizedContentY * effectiveWorkspaceHeight
        var maxOffset = Math.max(0, effectiveWorkspaceHeight - viewportHeightPx)
        return Math.min(Math.max(offset, 0), maxOffset)
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.width: frameBorder
        border.color: "#FEFEFE"
        radius: 0
    }

    Rectangle {
        id: workspaceBackground
        x: workspaceOffsetX
        y: workspaceOffsetY
        width: effectiveWorkspaceWidth
        height: effectiveWorkspaceHeight
    color: "#1E1E1E"
    radius: 4
        border.width: 0
    clip: true
        antialiasing: true

        Repeater {
            model: root.itemsModel
            delegate: Rectangle {
                readonly property var tileData: typeof modelData === "object" && modelData !== null ? modelData : (model !== undefined ? model : null)
                readonly property real sourceX: tileData && tileData.x !== undefined ? tileData.x : 0
                readonly property real sourceY: tileData && tileData.y !== undefined ? tileData.y : 0
                width: root.tileWidth * root.workspaceScale
                height: root.tileHeight * root.workspaceScale
                x: sourceX * root.workspaceScale
                y: sourceY * root.workspaceScale
                radius: height * 0.2
                color: "#FFFFFF"
                opacity: 0.9
                antialiasing: true
            }
        }

        Rectangle {
            id: viewportRect
            x: root.viewportOffsetX
            y: root.viewportOffsetY
            width: viewportWidthPx
            height: viewportHeightPx
            color: "transparent"
            border.width: 3
            border.color: "#51798B"
            radius: 2
            visible: workspaceBackground.width > 0 && workspaceBackground.height > 0
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            hoverEnabled: true
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            onPressed: handlePointer(mouse.x, mouse.y)
            onPositionChanged: if (pressed) handlePointer(mouse.x, mouse.y)

            function handlePointer(localX, localY) {
                if (parent.width <= 0 || parent.height <= 0)
                    return

                var viewportWidth = Math.max(1, root.viewportWidthPx)
                var viewportHeight = Math.max(1, root.viewportHeightPx)
                var targetX = Math.max(0, Math.min(localX - viewportWidth * 0.5, parent.width - viewportWidth))
                var targetY = Math.max(0, Math.min(localY - viewportHeight * 0.5, parent.height - viewportHeight))
                var workspaceX = targetX / root.workspaceScale
                var workspaceY = targetY / root.workspaceScale
                root.requestViewPosition(workspaceX, workspaceY)
            }
        }
    }
}
