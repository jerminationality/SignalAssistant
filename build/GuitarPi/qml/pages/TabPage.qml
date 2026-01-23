import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../components"

Item {
    id: root
    implicitWidth: 1280
    implicitHeight: 720

    property var bridge: (typeof AppController !== "undefined" && AppController)
                         ? AppController.tabBridge
                         : ((typeof TabBridge !== "undefined" && TabBridge) ? TabBridge : null)
    property bool monitorSwitchGuard: false
    readonly property var stringLabels: ["E", "A", "D", "G", "B", "e"]
    property string lastTestPlaybackState: ""

    readonly property real baseWidth: 1280
    readonly property real baseHeight: 720
    readonly property real scaleFactor: Math.min(width / baseWidth, height / baseHeight)

    Rectangle {
        id: backgroundContainer
        anchors.fill: parent
        color: "transparent"
       
        
        // Gradient base layer - fills entire display
        Image {
            id: gradientLayer
            anchors.fill: parent
            source: "../assets/gradientLayer.svg"
            fillMode: Image.Stretch
            z: -99
        }

        // Color overlay with 50% opacity - fills entire display
        Rectangle {
            id: colorOverlay
            anchors.fill: parent
            color: "#1d2c38"
            opacity: 1
             z: -100
        }
    }
    function handleMonitorToggle(enabled) {
        if (!bridge)
            return;
        bridge.setRecording(enabled);
    }

    function gainColor(value) {
        if (value >= 0.85)
            return "#ff5252";
        if (value >= 0.6)
            return "#ffd84d";
        return "#5ad45a";
    }

    function meterDisplayLevel(value) {
        var normalized = Math.max(0, Math.min(1, value));
        var ceiling = 0.35; // compress usable range so low inputs render taller
        return Math.min(1, normalized / ceiling);
    }

    function calibrationStepColor(state) {
        switch (state) {
        case 4:
            return "#6b6b73"; // Medium-dark grey for fading noise capture
        case 3:
            return "#56d56e";
        case 2:
            return "#ff9f5a";
        case 1:
            return "#ffd84d";
        default:
            return "#3b3b43";
        }
    }

    function tuningDeviationAt(index) {
        if (!bridge || !bridge.tuningDeviation)
            return 0;
        if (index < 0 || index >= bridge.tuningDeviation.length)
            return 0;
        var dev = Number(bridge.tuningDeviation[index]);
        if (bridge.tuningModeEnabled && Math.abs(dev) > 0.01) {
            console.log("tuning-deviation", "string", index, "cents", dev.toFixed(2));
        }
        return dev;
    }

    function tuningColorFromDeviation(cents) {
        var absCents = Math.abs(cents);
        
        // Color gradient based on cents deviation:
        // 0-3 cents: solid green (in tune)
        // 3-20 cents: fade from green through yellow to red
        // 20+ cents: solid red (out of tune)
        
        var green = {r: 0.337, g: 0.835, b: 0.431};   // #56d56e
        var yellow = {r: 1.0, g: 0.843, b: 0.302};    // #ffd84d
        var red = {r: 0.867, g: 0.243, b: 0.262};     // #dd3e43
        
        if (absCents <= 3) {
            // Solid green - in tune
            return Qt.rgba(green.r, green.g, green.b, 1);
        } else if (absCents <= 20) {
            // Interpolate from green through yellow to red (3-20 cents range)
            // Map 3-20 cents to 0-1 ratio
            var ratio = (absCents - 3) / 17.0;
            
            if (ratio <= 0.5) {
                // Green to yellow (3-11.5 cents)
                var r1 = ratio * 2.0;
                return Qt.rgba(
                    green.r + (yellow.r - green.r) * r1,
                    green.g + (yellow.g - green.g) * r1,
                    green.b + (yellow.b - green.b) * r1,
                    1
                );
            } else {
                // Yellow to red (11.5-20 cents)
                var r2 = (ratio - 0.5) * 2.0;
                return Qt.rgba(
                    yellow.r + (red.r - yellow.r) * r2,
                    yellow.g + (red.g - yellow.g) * r2,
                    yellow.b + (red.b - yellow.b) * r2,
                    1
                );
            }
        } else {
            // Solid red - out of tune
            return Qt.rgba(red.r, red.g, red.b, 1);
        }
    }

    function getActiveTuningString() {
        if (!bridge || !bridge.hexMeters || !bridge.tuningModeEnabled)
            return -1;
        
        var maxLevel = 0;
        var maxIndex = -1;
        var threshold = 0.02; // Minimum signal level to consider string active
        
        for (var i = 0; i < bridge.hexMeters.length; i++) {
            var level = Number(bridge.hexMeters[i]);
            if (level > maxLevel && level > threshold) {
                maxLevel = level;
                maxIndex = i;
            }
        }
        return maxIndex;
    }

    function tuningCircleColor(state, index) {
        if (bridge && bridge.tuningModeEnabled && !bridge.calibrationRunning) {
            var activeString = getActiveTuningString();
            var deviation = tuningDeviationAt(index);
            
            // Show color only for the currently active string
            if (activeString >= 0 && activeString === index) {
                // Always show tuning color for active string, regardless of deviation value
                return tuningColorFromDeviation(deviation);
            }
            // All other strings show neutral gray
            return "#3b3b43";
        }
        return calibrationStepColor(state);
    }

    Item {
        id: canvas
        width: baseWidth
        height: baseHeight
        anchors.centerIn: parent
        transformOrigin: Item.Center
        scale: scaleFactor

        Image {
            id: tabCaptureLabel
            source: "../assets/TabPage/TabCaptureLabel.svg"
            anchors.left: parent.left
            anchors.leftMargin: 48
            anchors.top: parent.top
            anchors.topMargin: 40
            smooth: true
            z: -5
            visible: false
        }

        Image {
            id: tabGrid
            source: "../assets/TabPage/TabGrid.svg"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: neckStage.top
            anchors.bottomMargin: 32
            fillMode: Image.Stretch
            smooth: true
            z: -4
            visible: false
        }

        Item {
            id: neckStage
            x: 16
            y: 468
            width: parent.width - 32
            height: 250
            z: -2

            Item {
                id: neckSection
                readonly property real baseFretWidth: 1205
                readonly property real baseStringHeight: 109
                readonly property real baseNeckWidth: 1185
                readonly property real baseNeckHeight: 134
                readonly property real baseStringsWidth: 1221
                readonly property real stringsHorizontalExpansion: baseStringsWidth - baseFretWidth
                readonly property real stringsVerticalOffset: (baseNeckHeight - baseStringHeight) / 2
                readonly property real fretDetectionOffset: 6
                readonly property real fretZeroOverlayOffset: 8
                property bool playbackActive: false
                property int playbackIndex: 0
                property var playbackEvents: []
                width: fretsImage.width > 0 ? fretsImage.width : baseFretWidth
                height: fretsImage.height > 0 ? fretsImage.height : 165
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                z: 1

                readonly property var baseFretBoundaries: [
                    0,
                    26.0342,
                    115.471,
                    198.308,
                    276.854,
                    350.779,
                    420.415,
                    486.42,
                    548.795,
                    607.869,
                    663.314,
                    715.458,
                    764.632,
                    811.495,
                    855.389,
                    896.642,
                    935.915,
                    973.208,
                    1007.86,
                    1041.19,
                    1072.22,
                    1101.59,
                    1129.31,
                    1155.38,
                    1179.8,
                    1203.57
                ]

                readonly property var baseStringOffsets: [0.25293, 20.4473, 40.6436, 60.8398, 81.0342, 101.229]
                readonly property var normalizedFretBoundaries: baseFretBoundaries.map(function(value) {
                    return value / baseFretWidth;
                })
                readonly property var normalizedStringOffsets: baseStringOffsets.map(function(value) {
                    return value / baseStringHeight;
                })

                property bool allowMouseOverlay: true
                property string overlaySource: ""
                property int activeString: -1
                property int activeFret: -1
                property bool liveNotesActive: liveNoteModel.count > 0

                ListModel {
                    id: liveNoteModel
                }

                Timer {
                    id: liveNoteSweepTimer
                    interval: 40
                    repeat: true
                    running: true
                    onTriggered: neckSection.pruneLiveNotes()
                }

                function stringCenter(index) {
                    if (index < 0 || index >= normalizedStringOffsets.length) {
                        return stringsImage.y + stringsImage.height / 2;
                    }
                    var scale = stringsImage.height > 0 ? stringsImage.height : neckSection.baseStringHeight;
                    return stringsImage.y + normalizedStringOffsets[index] * scale;
                }

                function fretCenter(index) {
                    var bounds = normalizedFretBoundaries;
                    if (index < 0 || index + 1 >= bounds.length) {
                        return fretsImage.x + fretsImage.width / 2;
                    }
                    var left = bounds[index] * fretsImage.width;
                    var right = bounds[index + 1] * fretsImage.width;
                    var center = fretsImage.x + (left + right) / 2;
                    if (index === 0)
                        center -= neckSection.fretZeroOverlayOffset;
                    return center;
                }

                function stringIndexFor(y) {
                    var count = normalizedStringOffsets.length;
                    var stringsHeight = stringsImage.height > 0 ? stringsImage.height : neckSection.baseStringHeight;
                    for (var i = 0; i < count; i++) {
                        var currentCenter = stringCenter(i);
                        var lower = i === 0 ? stringsImage.y : (stringCenter(i - 1) + currentCenter) / 2;
                        var upper = i === count - 1 ? (stringsImage.y + stringsHeight) : (currentCenter + stringCenter(i + 1)) / 2;
                        if (y >= lower && y < upper) {
                            return i;
                        }
                    }
                    return -1;
                }

                function fretIndexFor(x) {
                    var boardWidth = fretsImage.width > 0 ? fretsImage.width : neckSection.baseFretWidth;
                    var localX = x - fretsImage.x;
                    if (localX < 0 || localX > boardWidth) {
                        return -1;
                    }
                    var bounds = normalizedFretBoundaries;
                    var detectionOffset = neckSection.fretDetectionOffset;
                    for (var i = 0; i < bounds.length - 1; i++) {
                        var left = bounds[i] * boardWidth - detectionOffset;
                        var right = bounds[i + 1] * boardWidth - detectionOffset;
                        if (i === bounds.length - 2)
                            right = boardWidth;
                        var within = (i === bounds.length - 2)
                                      ? (localX >= left && localX <= right)
                                      : (localX >= left && localX < right);
                        if (within) {
                            return i;
                        }
                    }
                    if (localX >= boardWidth) {
                        return bounds.length - 2;
                    }
                    return -1;
                }

                // Allows mouse testing and future audio triggers to light up a single fret/string location.
                function activateOverlay(stringIndex, fretIndex, source) {
                    if (fretIndex < 0 || fretIndex >= normalizedFretBoundaries.length - 1)
                        return deactivateOverlay(source);

                    if (stringIndex < 0 || stringIndex >= normalizedStringOffsets.length)
                        return deactivateOverlay(source);

                    var resolvedSource = source || (overlaySource.length ? overlaySource : "external");
                    overlaySource = resolvedSource;

                    activeFret = fretIndex;
                    activeString = stringIndex;

                    hoverMarker.x = fretCenter(fretIndex) - hoverMarker.width / 2;
                    hoverMarker.y = stringCenter(stringIndex) - hoverMarker.height / 2;
                    hoverMarker.visible = true;
                }

                function deactivateOverlay(source) {
                    if (source && overlaySource.length && overlaySource !== source)
                        return;

                    if (activeFret === -1 && activeString === -1 && !hoverMarker.visible)
                        return;

                    overlaySource = "";
                    activeFret = -1;
                    activeString = -1;
                    hoverMarker.visible = false;
                }

                function addLiveNoteOverlay(rawStringIndex, fretIndex, velocity) {
                    if (fretIndex < 0 || fretIndex >= normalizedFretBoundaries.length - 1)
                        return;
                    if (rawStringIndex < 0 || rawStringIndex >= normalizedStringOffsets.length)
                        return;
                    var overlayString = 5 - rawStringIndex;
                    if (overlayString < 0 || overlayString >= normalizedStringOffsets.length)
                        return;

                    var energy = Math.max(0, Math.min(1, velocity === undefined ? 0.6 : velocity));
                    var updated = false;
                    for (var i = 0; i < liveNoteModel.count; ++i) {
                        var entry = liveNoteModel.get(i);
                        if (entry.overlayString === overlayString && entry.fretIndex === fretIndex) {
                            liveNoteModel.set(i, {
                                                  overlayString: overlayString,
                                                  fretIndex: fretIndex,
                                                  velocity: Math.max(entry.velocity, energy),
                                                  envelope: energy
                                              });
                            updated = true;
                            console.log("[QML-OVERLAY]  Updated S" + rawStringIndex + " F" + fretIndex + " energy=" + energy.toFixed(2));
                            break;
                        }
                    }
                    if (!updated) {
                        liveNoteModel.append({
                                                 overlayString: overlayString,
                                                 fretIndex: fretIndex,
                                                 velocity: energy,
                                                 envelope: energy
                                             });
                        console.log("[QML-OVERLAY]  Added S" + rawStringIndex + " F" + fretIndex + " energy=" + energy.toFixed(2));
                    }
                    allowMouseOverlay = false;
                }

                function removeLiveNoteOverlay(rawStringIndex, fretIndex) {
                    if (rawStringIndex < 0 || rawStringIndex >= 6)
                        return;
                    var overlayString = 5 - rawStringIndex;
                    if (overlayString < 0 || overlayString >= normalizedStringOffsets.length)
                        return;
                    
                    for (var i = liveNoteModel.count - 1; i >= 0; --i) {
                        var entry = liveNoteModel.get(i);
                        if (entry.overlayString === overlayString && entry.fretIndex === fretIndex) {
                            liveNoteModel.remove(i);
                            console.log("[QML-OVERLAY]  Removed S" + rawStringIndex + " F" + fretIndex);
                            break;
                        }
                    }
                    
                    if (liveNoteModel.count === 0 && !playbackActive)
                        allowMouseOverlay = true;
                }

                function updateLiveNoteEnvelope(rawStringIndex, envelope) {
                    if (rawStringIndex < 0 || rawStringIndex >= 6)
                        return;
                    var overlayString = 5 - rawStringIndex;
                    if (overlayString < 0 || overlayString >= normalizedStringOffsets.length)
                        return;
                    
                    for (var i = 0; i < liveNoteModel.count; ++i) {
                        var entry = liveNoteModel.get(i);
                        if (entry.overlayString === overlayString) {
                            liveNoteModel.setProperty(i, "envelope", Math.max(0, Math.min(1, envelope)));
                            break;
                        }
                    }
                }

                function pruneLiveNotes() {
                    // No longer needed - overlays are removed explicitly on note-off
                    // Keeping function for compatibility but it does nothing
                }

                function clearLiveNotes() {
                    liveNoteModel.clear();
                    if (!playbackActive)
                        allowMouseOverlay = true;
                }

                function updateHover(x, y) {
                    if (!allowMouseOverlay)
                        return;

                    var boardWidth = fretsImage.width > 0 ? fretsImage.width : neckSection.baseFretWidth;
                    var stringsHeight = stringsImage.height > 0 ? stringsImage.height : neckSection.baseStringHeight;
                    var withinHorizontal = x >= fretsImage.x && x <= fretsImage.x + boardWidth;
                    var withinVertical = y >= stringsImage.y && y <= stringsImage.y + stringsHeight;

                    if (!withinHorizontal || !withinVertical) {
                        deactivateOverlay("mouse");
                        return;
                    }

                    var fretIndex = fretIndexFor(x);
                    var stringIndex = stringIndexFor(y);

                    if (fretIndex === -1 || stringIndex === -1) {
                        deactivateOverlay("mouse");
                        return;
                    }

                    activateOverlay(stringIndex, fretIndex, "mouse");
                }

                function stopPlayback() {
                    playbackActive = false;
                    playbackEvents = [];
                    playbackIndex = 0;
                    playbackEventTimer.stop();
                    playbackReleaseTimer.stop();
                    allowMouseOverlay = (liveNoteModel.count === 0);
                    deactivateOverlay("playback");
                }

                function finishPlayback() {
                    playbackActive = false;
                    playbackEvents = [];
                    playbackIndex = 0;
                    playbackEventTimer.stop();
                    playbackReleaseTimer.stop();
                    allowMouseOverlay = (liveNoteModel.count === 0);
                    deactivateOverlay("playback");
                }

                function playDetectedEvents(events) {
                    stopPlayback();
                    if (!events || events.length === 0)
                        return;

                    playbackEvents = events.slice().sort(function(a, b) {
                        var startA = (a && a.start) ? a.start : 0;
                        var startB = (b && b.start) ? b.start : 0;
                        return startA - startB;
                    });

                    playbackIndex = 0;
                    playbackActive = true;
                    allowMouseOverlay = false;
                    scheduleNextPlaybackEvent();
                }

                function scheduleNextPlaybackEvent() {
                    playbackEventTimer.stop();
                    if (!playbackActive) {
                        return;
                    }
                    if (playbackIndex >= playbackEvents.length) {
                        if (!playbackReleaseTimer.running)
                            finishPlayback();
                        return;
                    }

                    var ev = playbackEvents[playbackIndex];
                    if (!ev)
                        return finishPlayback();

                    var delay = 0;
                    if (playbackIndex === 0) {
                        delay = 0;
                    } else {
                        var prev = playbackEvents[playbackIndex - 1];
                        var prevStart = prev && prev.start !== undefined ? prev.start : 0;
                        var curStart = ev.start !== undefined ? ev.start : 0;
                        delay = Math.max(0, curStart - prevStart);
                    }

                    if (delay <= 0) {
                        showPlaybackEvent(ev);
                    } else {
                        playbackEventTimer.interval = Math.max(1, Math.round(delay * 1000));
                        playbackEventTimer.start();
                    }
                }

                function showPlaybackEvent(ev) {
                    if (!playbackActive || !ev)
                        return;

                    var stringIdx = Math.round(Number(ev.string));
                    var fretIdx = Math.round(Number(ev.fret));
                    if (!(stringIdx >= 0 && stringIdx < normalizedStringOffsets.length)) {
                        playbackIndex += 1;
                        scheduleNextPlaybackEvent();
                        return;
                    }
                    if (!(fretIdx >= 0 && fretIdx < normalizedFretBoundaries.length)) {
                        playbackIndex += 1;
                        scheduleNextPlaybackEvent();
                        return;
                    }

                    activateOverlay(stringIdx, fretIdx, "playback");

                    var endVal = (ev.end !== undefined) ? ev.end : (ev.start || 0);
                    var startVal = (ev.start !== undefined) ? ev.start : 0;
                    var durationMs = Math.max(80, Math.round(Math.max(endVal - startVal, 0.08) * 1000));
                    playbackReleaseTimer.interval = durationMs;
                    playbackReleaseTimer.start();

                    playbackIndex += 1;
                    scheduleNextPlaybackEvent();
                }

                function handlePlaybackRelease() {
                    if (overlaySource === "playback")
                        deactivateOverlay("playback");
                    if (!playbackActive && !playbackEventTimer.running)
                        allowMouseOverlay = (liveNoteModel.count === 0);
                    if (playbackActive && playbackIndex >= playbackEvents.length && !playbackEventTimer.running)
                        finishPlayback();
                }

                Image {
                    id: fretsImage
                    source: "../assets/TabPage/NeckDisplay/Frets.svg"
                    sourceSize.width: neckSection.baseFretWidth
                    sourceSize.height: 165
                    width: neckSection.baseFretWidth
                    height: 165
                    fillMode: Image.Stretch
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: -3
                    smooth: true
                    z: 2
                }

                Image {
                    id: neckImage
                    source: "../assets/TabPage/NeckDisplay/Neck.png"
                    sourceSize.width: neckSection.baseNeckWidth
                    sourceSize.height: neckSection.baseNeckHeight
                    width: neckSection.baseNeckWidth
                    height: neckSection.baseNeckHeight
                    fillMode: Image.Stretch
                    // Anchor neck to the parent so it does not move when frets are adjusted.
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.horizontalCenterOffset: 14
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: -19
                    smooth: true
                    z: 0
                }

                Image {
                    id: stringsImage
                    source: "../assets/TabPage/NeckDisplay/Strings.svg"
                    sourceSize.width: neckSection.baseStringsWidth
                    sourceSize.height: neckSection.baseStringHeight
                    width: fretsImage.width + neckSection.stringsHorizontalExpansion
                    height: neckSection.baseStringHeight
                    fillMode: Image.Stretch
                    anchors.horizontalCenter: fretsImage.horizontalCenter
                    anchors.horizontalCenterOffset: -4
                    anchors.top: parent.top
                    anchors.topMargin: neckSection.stringsVerticalOffset - 3
                    smooth: true
                    z: 1
                }

                Image {
                    id: stringLabels
                    source: "../assets/TabPage/NeckDisplay/StringLabels.svg"
                    sourceSize.height: neckSection.baseNeckHeight
                    height: neckSection.baseNeckHeight
                    anchors.right: fretsImage.left
                    anchors.rightMargin: 22
                    anchors.verticalCenter: neckImage.verticalCenter
                    anchors.verticalCenterOffset: neckSection.stringsVerticalOffset + 2 + neckSection.baseStringHeight / 2 - neckSection.baseNeckHeight / 2
                    smooth: true
                    z: 2
                }

                Image {
                    id: hoverMarker
                    source: "../assets/TabPage/overlayMarker.svg"
                    width: 14
                    height: 14
                    visible: false
                    smooth: true
                    z: 3
                }

                Repeater {
                    id: liveNoteRepeater
                    model: liveNoteModel
                    delegate: Rectangle {
                        width: 44
                        height: 18
                        radius: 6
                        color: Qt.rgba(0.96, 0.58, 0.32, 0.85)
                        border.color: "#ffd8a2"
                        border.width: 1
                        x: neckSection.fretCenter(fretIndex) - width / 2
                        y: neckSection.stringCenter(overlayString) - height / 2
                        // Opacity linked to envelope for smooth decay visualization
                        // Minimum 0.2 opacity to keep visible, max at full envelope
                        opacity: Math.max(0.2, Math.min(1.0, envelope * 1.2))
                        z: 2.5
                        
                        Behavior on opacity {
                            NumberAnimation {
                                duration: 40
                                easing.type: Easing.OutQuad
                            }
                        }
                    }
                }

                Rectangle {
                    id: hoverBadge
                    height: neckSection.activeFret >= 0 ? 24 : 0
                    color: "#2A231DEE"
                    radius: 12
                    border.color: "#F2E8D544"
                    border.width: 1
                    visible: neckSection.activeFret >= 0
                    anchors.top: parent.top
                    anchors.topMargin: 8
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    implicitWidth: hoverText.implicitWidth + 20
                    z: 4

                    Text {
                        id: hoverText
                        anchors.centerIn: parent
                        color: "#F2E8D5"
                        font.pixelSize: 12
                        text: neckSection.activeFret >= 0 ? ("S" + (neckSection.activeString + 1) + "  F" + neckSection.activeFret) : ""
                    }
                }

                MouseArea {
                    id: hoverArea
                    anchors.fill: fretsImage
                    hoverEnabled: true
                    acceptedButtons: Qt.NoButton
                    onPositionChanged: {
                        if (neckSection.allowMouseOverlay)
                            neckSection.updateHover(hoverArea.mouseX, hoverArea.mouseY);
                    }
                    onEntered: {
                        if (neckSection.allowMouseOverlay)
                            neckSection.updateHover(hoverArea.mouseX, hoverArea.mouseY);
                    }
                    onExited: {
                        if (neckSection.allowMouseOverlay)
                            neckSection.deactivateOverlay("mouse");
                    }
                }

                Timer {
                    id: playbackEventTimer
                    interval: 150
                    repeat: false
                    onTriggered: {
                        if (!neckSection.playbackActive)
                            return;
                        if (neckSection.playbackIndex >= neckSection.playbackEvents.length) {
                            neckSection.finishPlayback();
                            return;
                        }
                        neckSection.showPlaybackEvent(neckSection.playbackEvents[neckSection.playbackIndex]);
                    }
                }

                Timer {
                    id: playbackReleaseTimer
                    interval: 100
                    repeat: false
                    onTriggered: neckSection.handlePlaybackRelease()
                }

            }

        }

        Item {
            id: hexMeterStrip
            // Map the desired root-space offsets back into the scaled canvas coordinates
            property real canvasBottomY: canvas.mapFromItem(root, 0, root.height - 2).y
            property real canvasRightX: canvas.mapFromItem(root, root.width - 28, 0).x
            property int meterHeight: 64
            x: canvasRightX - width
            y: canvasBottomY - height - 4
            height: metersRow.implicitHeight
            width: metersRow.implicitWidth
            visible: bridge !== null
            Row {
                id: metersRow
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                spacing: 2
                Repeater {
                    model: 6
                    delegate: Column {
                        spacing: 2
                        width: 16
                        property real level: (bridge && bridge.hexMeters && bridge.hexMeters.length > index)
                                            ? Math.max(0, Math.min(1, bridge.hexMeters[index]))
                                            : 0
                        property real displayLevel: meterDisplayLevel(level)
                        Rectangle {
                            width: 10
                            height: hexMeterStrip.meterHeight
                            radius: 4
                            color: "#15161a"
                            border.color: "#2a2c31"
                            border.width: 1
                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: parent.height * displayLevel
                                radius: 3
                                color: gainColor(displayLevel)
                            }
                        }
                    }
                }
            }
        }
        
        // RMS Numeric Display
        Column {
            id: rmsNumericDisplay
            spacing: 1
            anchors.right: hexMeterStrip.left
            anchors.rightMargin: 10
            anchors.bottom: hexMeterStrip.bottom
            visible: bridge !== null
            z: 10
            
            Repeater {
                model: root.stringLabels ? root.stringLabels.length : 0
                delegate: Row {
                    spacing: 3
                    property int stringIndex: root.stringLabels && root.stringLabels.length > 0 ? (root.stringLabels.length - 1 - index) : 0
                    property string stringLabel: root.stringLabels && root.stringLabels.length > stringIndex ? root.stringLabels[stringIndex] : ""
                    
                    Label {
                        text: stringLabel
                        font.pixelSize: 9
                        color: "#94a3b8"
                        width: 10
                    }
                    Label {
                        text: {
                            var raw = (bridge && bridge.rawMeters && bridge.rawMeters.length > stringIndex) 
                                ? bridge.rawMeters[stringIndex] : 0.0
                            var cal = (bridge && bridge.hexMeters && bridge.hexMeters.length > stringIndex) 
                                ? bridge.hexMeters[stringIndex] : 0.0
                            return raw.toFixed(3) + "   " + cal.toFixed(3)
                        }
                        font.family: "Monospace"
                        font.pixelSize: 9
                        color: "#cbd5e1"
                    }
                }
            }
        }

        Switch {
            id: liveHexMonitorSwitch
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.leftMargin: 4
            anchors.bottomMargin: 8
            visible: AppController && !AppController.testMode
            checked: AppController ? AppController.liveHexMonitorEnabled : false
            onToggled: if (AppController) AppController.setLiveHexMonitorEnabled(checked)
        }

        Switch {
            id: monitorSwitch
            anchors.top: parent.top
            anchors.topMargin: 32
            anchors.right: parent.right
            anchors.rightMargin: 32
            implicitHeight: 44
            implicitWidth: indicatorFrame.implicitWidth
            width: implicitWidth
            focusPolicy: Qt.NoFocus
            hoverEnabled: true
            contentItem: Item {}
            indicator: Rectangle {
                id: indicatorFrame
                implicitWidth: 82
                implicitHeight: 36
                radius: height / 2
                color: monitorSwitch.checked ? "#B9393F" : "#3C3027"
                border.color: monitorSwitch.checked ? "#F2B1A6" : "#7A6957"
                border.width: 1
                Behavior on color { ColorAnimation { duration: 180 } }
                Behavior on border.color { ColorAnimation { duration: 180 } }

                Rectangle {
                    width: 30
                    height: 30
                    radius: height / 2
                    anchors.verticalCenter: parent.verticalCenter
                    x: monitorSwitch.checked ? parent.width - width - 3 : 3
                    color: "#F2E8D5"
                    border.color: "#B59879"
                    border.width: 1
                    Behavior on x { NumberAnimation { duration: 160; easing.type: Easing.InOutQuad } }
                }
            }

            onCheckedChanged: {
                if (root.monitorSwitchGuard)
                    return;
                root.handleMonitorToggle(checked);
            }
        }

        // Live thresholds display
        Column {
            id: thresholdsTable
            anchors.right: calibrationPanel.left
            anchors.rightMargin: 16
            anchors.bottom: calibrationPanel.bottom
            spacing: 0
            visible: bridge && bridge.thresholds !== undefined && hexMeterStrip.visible
            
            // Header row
            Row {
                spacing: 3
                Label {
                    text: ""
                    width: 10
                    font.pixelSize: 8
                }
                Label {
                    text: "Baseline"
                    width: 45
                    font.pixelSize: 8
                    color: "#7d8694"
                    font.family: "Monospace"
                    horizontalAlignment: Text.AlignHCenter
                }
                Label {
                    text: "Env"
                    width: 45
                    font.pixelSize: 8
                    color: "#7d8694"
                    font.family: "Monospace"
                    horizontalAlignment: Text.AlignHCenter
                }
                Label {
                    text: "Gate"
                    width: 45
                    font.pixelSize: 8
                    color: "#7d8694"
                    font.family: "Monospace"
                    horizontalAlignment: Text.AlignHCenter
                }
                Label {
                    text: "Sust"
                    width: 45
                    font.pixelSize: 8
                    color: "#7d8694"
                    font.family: "Monospace"
                    horizontalAlignment: Text.AlignHCenter
                }
            }
            
            Repeater {
                model: 6
                delegate: Row {
                    spacing: 3
                    property int stringIndex: index
                    property string stringLabel: root.stringLabels[stringIndex]
                    property var thresholdData: (bridge && bridge.thresholds && bridge.thresholds.length > stringIndex) 
                        ? bridge.thresholds[stringIndex] : null
                    
                    Label {
                        text: stringLabel
                        font.pixelSize: 9
                        color: "#94a3b8"
                        width: 10
                    }
                    Label {
                        text: (thresholdData && typeof thresholdData.baseline === 'number') ? thresholdData.baseline.toFixed(4) : "-.----"
                        width: 45
                        font.family: "Monospace"
                        font.pixelSize: 9
                        color: "#cbd5e1"
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Label {
                        text: (thresholdData && typeof thresholdData.envFloor === 'number') ? thresholdData.envFloor.toFixed(4) : "-.----"
                        width: 45
                        font.family: "Monospace"
                        font.pixelSize: 9
                        color: "#cbd5e1"
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Label {
                        text: (thresholdData && typeof thresholdData.gateThreshold === 'number') ? thresholdData.gateThreshold.toFixed(4) : "-.----"
                        width: 45
                        font.family: "Monospace"
                        font.pixelSize: 9
                        color: "#cbd5e1"
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Label {
                        text: (thresholdData && typeof thresholdData.sustainFloor === 'number') ? thresholdData.sustainFloor.toFixed(4) : "-.----"
                        width: 45
                        font.family: "Monospace"
                        font.pixelSize: 9
                        color: "#cbd5e1"
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }

        Item {
            id: calibrationPanel
            property int groupSpacing: 12
            width: calibrationCluster.implicitWidth
            height: calibrationCluster.implicitHeight
            x: Math.max(24, hexMeterStrip.x - groupSpacing - width - 88)
            y: hexMeterStrip.y + hexMeterStrip.height - height + 4
            visible: hexMeterStrip.visible
            
            Row {
                    id: calibrationCluster
                    spacing: 12
                    Button {
                        id: calibrationButton
                        leftPadding: 2
                        rightPadding: 2
                        topPadding: 6
                        bottomPadding: 6
                        implicitWidth: calibrateLabel.implicitWidth + leftPadding + rightPadding + 16
                        text: {
                            if (!bridge)
                                return qsTr("Calibrate Input");
                            if (bridge.calibrationRunning)
                                return qsTr("Calibrating...");
                            return bridge.calibrationReady ? qsTr("Recalibrate") : qsTr("Calibrate Input");
                        }
                        enabled: bridge && !bridge.calibrationRunning
                        onClicked: if (bridge) bridge.startCalibration()
                        contentItem: Text {
                            id: calibrateLabel
                            text: calibrationButton.text
                            color: calibrationButton.enabled ? "#fdfdfd" : "#9c9ea6"
                            font.pixelSize: 14
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 4
                            color: calibrationButton.enabled ? "#2b2c32" : "#1d1e22"
                            border.color: calibrationButton.down ? "#fdfdfd" : "#4b4d55"
                            border.width: 1
                        }
                    }
                    Row {
                        id: calibrationIndicatorsRow
                        spacing: 8
                        anchors.verticalCenter: parent.verticalCenter
                        Row {
                            id: calibrationStatusRow
                            spacing: 4
                            visible: bridge && bridge.calibrationSteps && bridge.calibrationSteps.length === root.stringLabels.length
                            opacity: bridge && bridge.calibrationRunning ? 1 : 0.65
                            Repeater {
                                model: bridge && bridge.calibrationSteps ? bridge.calibrationSteps : []
                                delegate: Item {
                                    width: 26
                                    height: indicatorColumn.implicitHeight
                                    // Reactive properties for tuning mode - accessing these forces binding updates
                                    readonly property var tuningDev: bridge ? bridge.tuningDeviation : []
                                    readonly property var hexMeterLevels: bridge ? bridge.hexMeters : []
                                    Column {
                                        id: indicatorColumn
                                        spacing: 2
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        Rectangle {
                                            width: 20
                                            height: 20
                                            radius: width / 2
                                            // Reference parent's reactive properties to trigger re-evaluation
                                            color: {
                                                var _ = parent.parent.tuningDev;
                                                var __ = parent.parent.hexMeterLevels;
                                                return tuningCircleColor(Number(modelData), index);
                                            }
                                            border.color: Number(modelData) >= 2 ? "#fefefe" : "#b0b3bb"
                                            border.width: 1
                                            opacity: Number(modelData) === 0 ? 0.7 : 1
                                            
                                            Behavior on color {
                                                ColorAnimation {
                                                    duration: bridge && bridge.tuningModeEnabled ? 80 : 2000
                                                    easing.type: Easing.InOutQuad
                                                }
                                            }
                                        }
                                        Text {
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                            text: root.stringLabels[index]
                                            font.pixelSize: 10
                                            color: "#dfe3ea"
                                            opacity: Number(modelData) >= 3 ? 1 : 0.8
                                        }
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        enabled: bridge && !bridge.calibrationRunning
                                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                        onClicked: if (bridge) bridge.recalibrateString(index)
                                    }
                                }
                            }
                        }
                        Item {
                            width: -8
                            height: 1
                        }
                        ToolButton {
                            id: tunerToggle
                            checkable: true
                            implicitWidth: 40
                            implicitHeight: 40
                            enabled: !(bridge && bridge.calibrationRunning)
                            background: Rectangle {
                                anchors.fill: parent
                                color: "transparent"
                            }
                            contentItem: Image {
                                anchors.centerIn: parent
                                source: Qt.resolvedUrl("../assets/icons/tuningFork.svg")
                                width: 28
                                height: 28
                                fillMode: Image.PreserveAspectFit
                                opacity: tunerToggle.checked ? 1.0 : 0.12 
                            }
                            onClicked: if (bridge) bridge.setTuningModeEnabled(!bridge.tuningModeEnabled)
                            Component.onCompleted: {
                                if (bridge)
                                    checked = bridge.tuningModeEnabled;
                            }
                            Connections {
                                target: bridge
                                function onTuningModeEnabledChanged() {
                                    if (bridge) tunerToggle.checked = bridge.tuningModeEnabled
                                }
                            }
                        }
                        ToolButton {
                            id: tuningPanelToggle
                            checkable: true
                            implicitWidth: 44
                            implicitHeight: 44
                            y: -6
                            x: -14
                            Component.onCompleted: {
                                var w = ApplicationWindow.window
                                    if (w && typeof w.tuningPanelVisible !== 'undefined') {
                                        checked = Qt.binding(function() { return w.tuningPanelVisible })
                                        console.log("qml", "tuning-panel-toggle", "connected to main window")
                                    } else {
                                        console.log("qml", "tuning-panel-toggle", "failed to find main window")
                                    }
                                }
                                background: Rectangle {
                                    anchors.fill: parent
                                color: "transparent"
                            }
                            contentItem: Image {
                                anchors.centerIn: parent
                                source: Qt.resolvedUrl("../assets/icons/lucide-settings.svg")
                                width: 28
                                height: 28
                                fillMode: Image.PreserveAspectFit
                                opacity: tuningPanelToggle.checked ? 1.0 : 0.12
                            }
                            onClicked: {
                                var w = ApplicationWindow.window
                                if (w && typeof w.tuningPanelVisible !== 'undefined') {
                                    w.tuningPanelVisible = !w.tuningPanelVisible
                                }
                            }
                        }
                        // Exit Button
                        ToolButton {
                            id: exitButton
                            implicitWidth: 44
                            implicitHeight: 44
                            y: -4
                            ToolTip.visible: hovered
                            ToolTip.text: "Exit Application"
                            ToolTip.delay: 500
                            background: Rectangle {
                                anchors.fill: parent
                                color: "transparent"
                            }
                            contentItem: Image {
                                anchors.centerIn: parent
                                source: Qt.resolvedUrl("../assets/icons/lucide-x.svg")
                                width: 28
                                height: 28
                                fillMode: Image.PreserveAspectFit
                                opacity: 1.0
                            }
                            onClicked: {
                                Qt.quit()
                            }
                        }
                    }
                }
        }
    }

    Connections {
        target: bridge
        function onRecordingChanged() {
            if (!bridge)
                return;
            if (root.monitorSwitchGuard)
                return;
            root.monitorSwitchGuard = true;
            monitorSwitch.checked = bridge.recording;
            root.monitorSwitchGuard = false;
            if (!bridge.recording && neckSection) {
                neckSection.clearLiveNotes();
                neckSection.deactivateOverlay("live");
            }
        }
        function onLiveNoteTriggered(stringIndex, fretIndex, velocity) {
            console.log("[QML-NOTE-ON]  S" + stringIndex + " F" + fretIndex + " vel=" + velocity.toFixed(2));
            if (stringIndex === undefined || fretIndex === undefined)
                return;
            if (!neckSection)
                return;
            if (stringIndex < 0 || fretIndex < 0)
                return;
            neckSection.addLiveNoteOverlay(stringIndex, fretIndex, velocity);
        }
        function onLiveNoteEnded(stringIndex, fretIndex) {
            console.log("[QML-NOTE-OFF] S" + stringIndex + " F" + fretIndex);
            if (stringIndex === undefined || fretIndex === undefined)
                return;
            if (!neckSection)
                return;
            if (stringIndex < 0 || fretIndex < 0)
                return;
            neckSection.removeLiveNoteOverlay(stringIndex, fretIndex);
        }
        function onLiveNoteEnvelopeUpdated(stringIndex, envelope) {
            if (stringIndex === undefined || envelope === undefined)
                return;
            if (!neckSection)
                return;
            if (stringIndex < 0)
                return;
            neckSection.updateLiveNoteEnvelope(stringIndex, envelope);
        }
    }

    Connections {
        target: AppController
        function onTestPlaybackChanged() {
            if (!neckSection)
                return;
            if (!AppController || !AppController.testMode) {
                if (root.lastTestPlaybackState.length > 0)
                    neckSection.stopPlayback();
                root.lastTestPlaybackState = "";
                return;
            }

            var state = AppController.testPlaybackState;
            if (state === root.lastTestPlaybackState)
                return;
            root.lastTestPlaybackState = state;

            if (state === "Playing") {
                var events = bridge && bridge.events ? bridge.events : [];
                neckSection.playDetectedEvents(events);
            } else if (state === "Stopped" || state === "Idle" || state === "Paused" || state === "Complete") {
                neckSection.stopPlayback();
            }
        }
    }

    Component.onCompleted: {
        if (bridge) {
            root.monitorSwitchGuard = true;
            monitorSwitch.checked = bridge.recording;
            root.monitorSwitchGuard = false;
            bridge.requestRefresh();
        }
    }

    onBridgeChanged: {
        if (bridge) {
            root.monitorSwitchGuard = true;
            monitorSwitch.checked = bridge.recording;
            root.monitorSwitchGuard = false;
            bridge.requestRefresh();
        } else {
            root.monitorSwitchGuard = true;
            monitorSwitch.checked = false;
            root.monitorSwitchGuard = false;
            neckSection.stopPlayback();
        }
    }

    Component.onDestruction: {
        if (bridge)
            bridge.setRecording(false);
    }
}
