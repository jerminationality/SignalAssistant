A qml object has been made in components/BatchedFretboardBinOverlay.qml that allows you to overlay colored bins on a batched fretboard.

Create a "Bin Overlay" system that can be toggled that changes the current fret note overlay display to a "heatmap" fretboard filled with reactive cells that represent each of the bins.

Link the specific live magnitude values of each bin to the fill color of each corresponding cell using the color provider. Have the color range from green (low) to red (high) based on the magnitude value.

Instantiation example (Use in TabPage.qml)

import QtQuick
import "components"

Item {
    width: 1270
    height: 126

    BatchedFretboardBinOverlay {
        id: bins
        anchors.fill: parent

        // Option 1: Use a provider (recommended for “live” colors)
        colorProvider: function(s, f, id) {
            // Example: highlight 12th fret
            if (f === 12) return "#333333"
            return "#222222"
        }

        onBinClicked: (binId, s, f, x, y) => {
            console.log("Clicked", binId, "s", s, "f", f)

            // Option 2: override specific bins (fast toggles)
            bins.setBinColorById(binId, "#555555")
        }
    }
}

