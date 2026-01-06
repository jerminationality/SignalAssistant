import QtQuick 2.15

Item {
    id: root
    property url source: ""
    property color tint: "#FFFFFF"
    property bool smooth: true
    property bool asynchronous: true

    implicitWidth: sourceImage.implicitWidth
    implicitHeight: sourceImage.implicitHeight
    width: implicitWidth > 0 ? implicitWidth : 24
    height: implicitHeight > 0 ? implicitHeight : 24

    Image {
        id: sourceImage
        source: root.source
        smooth: root.smooth
        asynchronous: root.asynchronous
        visible: false
        onStatusChanged: {
            if (status === Image.Ready) {
                root.implicitWidth = sourceSize.width
                root.implicitHeight = sourceSize.height
                colorCanvas.requestPaint()
            }
        }
        onSourceChanged: colorCanvas.requestPaint()
        onSourceSizeChanged: colorCanvas.requestPaint()
    }

    Canvas {
        id: colorCanvas
        anchors.fill: parent
        renderTarget: Canvas.FramebufferObject
        antialiasing: root.smooth

        onPaint: {
            if (sourceImage.status !== Image.Ready)
                return

            var ctx = getContext("2d")
            ctx.reset()
            ctx.imageSmoothingEnabled = root.smooth

            var w = width
            var h = height
            var sourceW = sourceImage.sourceSize.width
            var sourceH = sourceImage.sourceSize.height

            if (sourceW <= 0 || sourceH <= 0 || w <= 0 || h <= 0)
                return

            var scale = Math.min(w / sourceW, h / sourceH)
            var drawW = sourceW * scale
            var drawH = sourceH * scale
            var dx = (w - drawW) / 2
            var dy = (h - drawH) / 2

            ctx.drawImage(sourceImage, 0, 0, sourceW, sourceH, dx, dy, drawW, drawH)
            ctx.globalCompositeOperation = "source-in"
            ctx.fillStyle = root.tint
            ctx.fillRect(0, 0, w, h)
        }

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Component.onCompleted: requestPaint()
    }

    onTintChanged: colorCanvas.requestPaint()
}
