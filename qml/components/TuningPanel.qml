import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

FocusScope {
    id: root
    height: 372
    focus: visible
    property var controller: null
    property var bridge: null
    property var stringLabels: []
    property var onCloseRequested: null
    property alias panelOpacity: background.opacity
    property var sliderRegistry: []
    property int sliderSyncCursor: 0
    readonly property int sliderBatchSize: 8
    property int pendingLocalSyncs: 0
    property bool shiftHeld: false
    onVisibleChanged: {
        if (visible && Qt.application && Qt.application.keyboardModifiers !== undefined) {
            shiftHeld = ((Qt.application.keyboardModifiers & Qt.ShiftModifier) !== 0)
        } else if (!visible) {
            shiftHeld = false
        }
    }
    Component.onCompleted: console.log("qml", "TuningPanel", "component-completed")
    Keys.onEscapePressed: {
        console.log("qml", "TuningPanel", "escape-pressed")
        if (typeof onCloseRequested === "function")
            onCloseRequested()
        event.accepted = true
    }

    Keys.onPressed: {
        if (!event)
            return
        const isShift = (event.key === Qt.Key_Shift) || ((event.modifiers & Qt.ShiftModifier) !== 0)
        if (isShift)
            shiftHeld = true
    }

    Keys.onReleased: {
        if (!event)
            return
        const mods = (event.modifiers !== undefined)
                ? event.modifiers
                : (Qt.application && Qt.application.keyboardModifiers !== undefined
                    ? Qt.application.keyboardModifiers
                    : 0)
        const stillHeld = ((mods & Qt.ShiftModifier) !== 0) && (event.key !== Qt.Key_Shift)
        shiftHeld = stillHeld
    }

    Timer {
        id: modifierPoller
        interval: 33
        repeat: true
        running: root.visible
        triggeredOnStart: true
        onTriggered: {
            if (Qt.application && Qt.application.keyboardModifiers !== undefined) {
                shiftHeld = ((Qt.application.keyboardModifiers & Qt.ShiftModifier) !== 0)
            }
        }
    }

    function shiftActive() {
        return shiftHeld
    }

    readonly property color panelColor: "#111827"
    readonly property color panelBorder: "#1f2937"
    readonly property color accentColor: "#38bdf8"
    readonly property int compactButtonWidth: 64
    readonly property int compactButtonHeight: 25
    readonly property int compactButtonFontSize: 12

    onControllerChanged: {
        console.log("qml", "TuningPanel", "controller", controller ? "connected" : "null")
        stringLabels = controller ? controller.stringLabels() : []
        if (compareSwitch)
            compareSwitch.syncWithController()
        sliderSyncTimer.restart()
    }

    Connections {
        target: controller
        function onRevisionChanged() {
            if (root.pendingLocalSyncs > 0) {
                root.pendingLocalSyncs--
                return
            }
            root.requestSyncAll()
        }
    }

    Connections {
        target: bridge
        function onCalibrationGainsChanged() {
            root.requestSyncAll()
        }
        function onCalibrationParametersUpdated() {
            console.log("qml", "TuningPanel", "calibration-complete-sync")
            root.requestSyncAll()
        }
    }

    Dialog {
        id: snapshotDialog
        parent: root
        modal: true
        focus: true
        title: qsTr("Save Tuning Snapshot")
        standardButtons: DialogButtonBox.NoButton
        closePolicy: Popup.CloseOnEscape
        x: (root.width - width) / 2
        y: (root.height - height) / 2
        contentItem: Column {
            spacing: 8
            width: 320
            Label {
                text: qsTr("Enter a label for this snapshot")
                color: "#e2e8f0"
                wrapMode: Text.WordWrap
                font.pixelSize: 13
            }
            TextField {
                id: snapshotLabelField
                placeholderText: qsTr("e.g. Lead tuning")
                selectByMouse: true
                onAccepted: {
                    if (text.trim().length > 0)
                        snapshotDialog.accept()
                }
            }
        }
        footer: DialogButtonBox {
            spacing: 8
            Button {
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                onClicked: snapshotDialog.reject()
            }
            Button {
                text: qsTr("Save")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                enabled: snapshotLabelField.text.trim().length > 0
                onClicked: snapshotDialog.accept()
            }
        }
        onOpened: {
            snapshotLabelField.text = ""
        }
        onAccepted: {
            var label = snapshotLabelField.text.trim()
            if (!controller || label.length === 0)
                return
            controller.saveState(label)
            snapshotLabelField.text = ""
        }
        onRejected: snapshotLabelField.text = ""
    }

    Timer {
        id: sliderSyncTimer
        interval: 100
        repeat: false
        onTriggered: root.requestSyncAll()
    }

    function requestSyncAll() {
        console.log("qml", "TuningPanel", "sync-all", stringLabels.length)
        syncAllTimer.restart()
    }

    Timer {
        id: syncAllTimer
        interval: 50
        repeat: false
        onTriggered: syncAllSliders()
    }

    function syncAllSliders() {
        if (!controller || sliderRegistry.length === 0) {
            console.log("qml", "TuningPanel", "sync-all-skip")
            return
        }
        sliderSyncCursor = 0
        syncBatchTimer.stop()
        console.log("qml", "TuningPanel", "sync-all-triggered", sliderRegistry.length)
        syncBatchTimer.start()
    }

    Timer {
        id: syncBatchTimer
        interval: 50
        repeat: true
        onTriggered: {
            console.log("qml", "TuningPanel", "sync-batch-tick", sliderSyncCursor, sliderRegistry.length)
            if (sliderSyncCursor >= sliderRegistry.length) {
                console.log("qml", "TuningPanel", "sync-complete", sliderRegistry.length)
                stop()
                return
            }

            var batchEnd = Math.min(sliderSyncCursor + root.sliderBatchSize, sliderRegistry.length)
            while (sliderSyncCursor < batchEnd) {
                var idx = sliderSyncCursor
                var sliderItem = sliderRegistry[sliderSyncCursor++]
                if (sliderItem && typeof sliderItem.doSync === "function") {
                    console.log("qml", "TuningPanel", "sync-calling", idx)
                    try {
                        sliderItem.doSync()
                        console.log("qml", "TuningPanel", "sync-done", idx)
                    } catch (e) {
                        console.log("qml", "TuningPanel", "sync-error", idx, e)
                    }
                }
            }
        }
    }

    function registerSlider(sliderItem) {
        sliderRegistry.push(sliderItem)
    }

    function noteLocalChange() {
        pendingLocalSyncs += 1
    }

    function applyGroupDelta(paramKey, sourceIndex, delta, originSlider) {
        if (!controller || !originSlider || Math.abs(delta) <= 0)
            return

        function commitValue(targetSlider) {
            noteLocalChange()
            controller.setParameterValue(paramKey, targetSlider.stringIndex, targetSlider.value)
        }

        commitValue(originSlider)

        for (var i = 0; i < sliderRegistry.length; ++i) {
            var sliderItem = sliderRegistry[i]
            if (!sliderItem || sliderItem === originSlider || sliderItem.paramKey !== paramKey)
                continue
            var nextValue = sliderItem.value + delta
            nextValue = Math.max(sliderItem.from, Math.min(sliderItem.to, nextValue))
            sliderItem.suppress = true
            sliderItem.value = nextValue
            sliderItem.lastDispatchedValue = nextValue
            sliderItem.suppress = false
            commitValue(sliderItem)
        }
    }

    function handleHistoryAction(actionKey) {
        console.log("qml", "TuningPanel", "history-request", actionKey, !!controller)
        if (!controller)
            return
        var handled = false
        switch (actionKey) {
        case "undo":
            controller.undo()
            handled = true
            break
        case "redo":
            controller.redo()
            handled = true
            break
        case "revert":
            controller.revert()
            handled = true
            break
        default:
            console.log("qml", "TuningPanel", "history-action-unknown", actionKey)
            return
        }
        console.log("qml", "TuningPanel", "history-action-dispatched", actionKey, handled)
        requestSyncAll()
    }

    function openSnapshotDialog() {
        if (!controller)
            return
        snapshotLabelField.text = ""
        snapshotDialog.open()
        Qt.callLater(function() {
            snapshotLabelField.forceActiveFocus()
            snapshotLabelField.selectAll()
        })
    }

    function unregisterSlider(entry) {
        var index = sliderRegistry.indexOf(entry)
        if (index >= 0)
            sliderRegistry.splice(index, 1)
    }

    MouseArea {
        id: wheelArea
        anchors.fill: parent
        z: 100
        hoverEnabled: true
        propagateComposedEvents: true
        acceptedButtons: Qt.NoButton
        
        onWheel: function(wheel) {
            var delta = wheel.angleDelta.y / 120
            flickable.flick(0, delta * 900)
            wheel.accepted = true
        }
        
        onPressed: function(mouse) {
            mouse.accepted = false
        }
    }

    Rectangle {
        id: background
        width: parent.width
        height: parent.height
        radius: 12
        color: panelColor
        border.color: panelBorder
        border.width: 1
        opacity: root.visible ? 0.95 : 0.0
        Behavior on opacity { NumberAnimation { duration: 150 } }
    }

    ColumnLayout {
        id: headerButtons
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        anchors.topMargin: 8
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 1
            RowLayout {
                spacing: 2
                Switch {
                    id: compareSwitch
                    text: ""
                    property bool syncing: false
                    function syncWithController() {
                        syncing = true
                        checked = controller ? controller.compareBaseline : false
                        syncing = false
                    }
                    Component.onCompleted: syncWithController()
                    onToggled: {
                        if (!controller || syncing)
                            return
                        controller.compareBaseline = checked
                    }
                }
                Label {
                    text: "Compare committed"
                    color: "#e2e8f0"
                    font.pixelSize: 13
                }
                Connections {
                    target: controller
                    function onCompareBaselineChanged() {
                        compareSwitch.syncWithController()
                    }
                }
            }
            Item { Layout.fillWidth: true }
            Button {
                text: "Commit"
                enabled: !!controller
                implicitWidth: root.compactButtonWidth
                implicitHeight: root.compactButtonHeight
                font.pixelSize: root.compactButtonFontSize
                onClicked: {
                    if (controller) {
                        controller.commit()
                        if (bridge) {
                            bridge.updateCalibrationMultipliers()
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 1
            Flow {
                spacing: 4
                Repeater {
                    model: [
                        { label: "Undo", key: "undo" },
                        { label: "Redo", key: "redo" },
                        { label: "Revert", key: "revert" }
                    ]
                    delegate: Button {
                        implicitWidth: root.compactButtonWidth
                        implicitHeight: root.compactButtonHeight
                        font.pixelSize: root.compactButtonFontSize
                        text: modelData.label
                        enabled: !!root.controller
                        onClicked: root.handleHistoryAction(modelData.key)
                    }
                }
            }
            Item { Layout.fillWidth: true }
            ComboBox {
                id: savedStatesBox
                Layout.preferredWidth: 180
                model: controller ? controller.savedStates : []
                enabled: controller && (controller.savedStates.length > 0)
            }
            Button {
                text: "Load"
                enabled: !!controller && savedStatesBox.currentText.length > 0
                implicitWidth: root.compactButtonWidth
                implicitHeight: root.compactButtonHeight
                font.pixelSize: root.compactButtonFontSize
                onClicked: controller && controller.loadState(savedStatesBox.currentText)
            }
            Button {
                text: "Delete"
                enabled: !!controller && savedStatesBox.currentText.length > 0
                implicitWidth: root.compactButtonWidth
                implicitHeight: root.compactButtonHeight
                font.pixelSize: root.compactButtonFontSize
                onClicked: controller && controller.deleteState(savedStatesBox.currentText)
            }
            Button {
                text: "Save"
                enabled: !!controller
                implicitWidth: root.compactButtonWidth
                implicitHeight: root.compactButtonHeight
                font.pixelSize: root.compactButtonFontSize
                onClicked: root.openSnapshotDialog()
            }
        }
    }

    Flickable {
        id: flickable
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: headerButtons.bottom
        anchors.bottom: parent.bottom
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        anchors.topMargin: 6
        anchors.bottomMargin: 10
        contentWidth: width
        contentHeight: categoryColumn.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        Column {
            id: categoryColumn
            width: parent.width
            spacing: 12
                Repeater {
                    model: controller ? controller.categories() : []
                    delegate: Column {
                        width: parent.width
                        spacing: 4

                        Label {
                            text: modelData.title
                            color: "#e2e8f0"
                            font.pixelSize: 15
                            font.bold: true
                            visible: modelData.title !== "YIN Detection"
                            height: visible ? implicitHeight : 0
                        }

                        Loader {
                            id: sectionLoader
                            width: parent.width
                            asynchronous: true
                            active: !!root.controller
                            sourceComponent: parameterListComponent
                            property var sectionData: modelData
                            onStatusChanged: {
                                if (status === Loader.Loading)
                                    console.log("qml", "TuningPanel", "section-loading", sectionData ? sectionData.title : "")
                                else if (status === Loader.Ready)
                                    console.log("qml", "TuningPanel", "section-ready", sectionData ? sectionData.title : "")
                            }
                            onLoaded: {
                                console.log("qml", "TuningPanel", "section-loaded", sectionData ? sectionData.title : "")
                                if (!item) {
                                    console.log("qml", "TuningPanel", "section-loaded-null-item")
                                    return
                                }
                                console.log("qml", "TuningPanel", "section-assign", "sectionData")
                                item.sectionData = sectionLoader.sectionData
                                console.log("qml", "TuningPanel", "section-assign", "accentColor")
                                item.accentColor = root.accentColor
                                console.log("qml", "TuningPanel", "section-assign", "panelRef")
                                item.panelRef = root
                                console.log("qml", "TuningPanel", "section-assign", "stringLabels")
                                item.stringLabels = Qt.binding(function() { return root.stringLabels })
                                console.log("qml", "TuningPanel", "section-assign", "controller")
                                item.controller = Qt.binding(function() { return root.controller })
                                console.log("qml", "TuningPanel", "section-init-complete")
                                Qt.callLater(function() {
                                    console.log("qml", "TuningPanel", "section-sync-request")
                                    root.requestSyncAll()
                                })
                            }
                        }

                        Item {
                            width: parent.width
                            height: sectionLoader.status === Loader.Loading ? 24 : 0
                            visible: sectionLoader.status === Loader.Loading
                            Behavior on height { NumberAnimation { duration: 120 } }
                            Label {
                                anchors.centerIn: parent
                                text: "Loading…"
                                color: "#94a3b8"
                                font.pixelSize: 12
                            }
                        } // Item (loading indicator)
                    } // Column (delegate)
                } // Repeater
            } // Column (categoryColumn)
        } // Flickable

    Component {
        id: parameterListComponent
        Column {
            id: paramListColumn
            width: parent ? parent.width : 0
            spacing: 12
            property var sectionData
            property var controller
            property var stringLabels: []
            property color accentColor: "#38bdf8"
            property var panelRef: root
            Component.onCompleted: console.log("qml", "TuningPanel", "section-ui-create", sectionData ? sectionData.title : "", sectionData && sectionData.parameters ? sectionData.parameters.length : 0)
            onSectionDataChanged: console.log("qml", "TuningPanel", "section-data-assigned", sectionData ? sectionData.title : "", sectionData && sectionData.parameters ? sectionData.parameters.length : 0)

            Flow {
                id: parameterFlow
                width: parent.width
                spacing: 10
                property real preferredCardWidth: 420
                property int desiredColumns: 3
                property real minimumCardWidth: 220
                property real cardWidth: {
                    if (width <= 0 || desiredColumns <= 0)
                        return preferredCardWidth
                    var totalSpacing = spacing * Math.max(0, desiredColumns - 1)
                    var available = Math.max(0, width - totalSpacing)
                    var target = available / desiredColumns
                    return Math.max(minimumCardWidth, Math.min(preferredCardWidth, target))
                }
                property int createdCards: 0

                Repeater {
                    model: sectionData && sectionData.parameters ? sectionData.parameters : []
                    delegate: Column {
                        id: parameterCard
                        width: parameterFlow.cardWidth
                        spacing: 2
                        readonly property int cardPadding: 6
                        property var param: modelData
                        property real valueColumnWidth: 0
                        Component.onCompleted: {
                            parameterFlow.createdCards += 1
                            console.log("qml", "TuningPanel", "param-card", sectionData ? sectionData.title : "", param ? param.key : "", parameterFlow.createdCards)
                        }
                        Text {
                            id: valueWidthProbe
                            visible: false
                            font.family: "Monospace"
                            font.pixelSize: 12
                        }
                        function registerValueSample(sample) {
                            if (!sample || valueWidthProbe.text === sample)
                                return
                            valueWidthProbe.text = sample
                            var measured = valueWidthProbe.implicitWidth
                            if (measured > valueColumnWidth)
                                valueColumnWidth = measured
                        }
                        Item { height: cardPadding }
                        Row {
                            spacing: 6
                            Label {
                                id: paramLabel
                                text: param.label
                                color: "#f1f5f9"
                                font.pixelSize: 14
                            }
                            Button {
                                id: commitButton
                                implicitWidth: paramLabel.height
                                implicitHeight: paramLabel.height
                                padding: 0
                                focusPolicy: Qt.NoFocus
                                anchors.verticalCenter: paramLabel.verticalCenter
                                ToolTip.visible: hovered
                                ToolTip.text: "Commit " + param.label
                                background: Rectangle {
                                    anchors.fill: parent
                                    radius: 3
                                    color: commitButton.down ? "#334155" : (commitButton.hovered ? "#1e293b" : "transparent")
                                    border.width: 1
                                    border.color: commitButton.hovered ? "#475569" : "#334155"
                                }
                                contentItem: Image {
                                    anchors.centerIn: parent
                                    source: Qt.resolvedUrl("../assets/icons/lucide-save.svg")
                                    sourceSize.width: paramLabel.height - 4
                                    sourceSize.height: paramLabel.height - 4
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                }
                                onClicked: {
                                    if (paramListColumn.controller) {
                                        paramListColumn.controller.commit()
                                        if (root.bridge) {
                                            root.bridge.updateCalibrationMultipliers()
                                        }
                                        console.log("qml", "TuningPanel", "param-commit", param.label)
                                    }
                                }
                            }
                        }
                        Text {
                            text: param.description
                            color: "#94a3b8"
                            wrapMode: Text.WordWrap
                            font.pixelSize: 12
                        }
                        Column {
                            width: parent.width
                            spacing: 4
                            Repeater {
                                model: stringLabels ? stringLabels.length : 0
                                delegate: Column {
                                    id: stringRow
                                    width: parent.width
                                    spacing: 4
                                    property int stringIndex: stringLabels && stringLabels.length > 0 ? (stringLabels.length - 1 - index) : index
                                    property double baselineValue: 0.0
                                    property string stringLabel: stringLabels && stringLabels.length > stringIndex ? stringLabels[stringIndex] : ""
                                    Component.onCompleted: console.log("qml", "TuningPanel", "string-row", param ? param.key : "", stringLabel, stringIndex)

                                    RowLayout {
                                        width: parent.width
                                        spacing: 6
                                        Label {
                                            text: stringLabel
                                            color: "#e2e8f0"
                                            font.pixelSize: 12
                                            Layout.preferredWidth: 22
                                        }
                                        Slider {
                                            id: slider
                                            Layout.fillWidth: true
                                            from: {
                                                if (param.perStringMin && param.perStringMin.length > stringIndex) {
                                                    return param.perStringMin[stringIndex]
                                                }
                                                return param.min
                                            }
                                            to: {
                                                if (param.perStringMax && param.perStringMax.length > stringIndex) {
                                                    return param.perStringMax[stringIndex]
                                                }
                                                return param.max
                                            }
                                            stepSize: param.step > 0 ? param.step : 0
                                            snapMode: param.step > 0 ? Slider.SnapOnRelease : Slider.NoSnap
                                            property bool suppress: false
                                            property var registryEntry: null
                                            property bool initialSyncDone: false
                                            property string paramKey: param ? param.key : ""
                                            property int stringIndex: stringRow.stringIndex
                                            property double lastDispatchedValue: 0
                                            property var panelRef: paramListColumn.panelRef
                                            readonly property real baselineNormalized: panelRef ? panelRef.normalized(stringRow.baselineValue, from, to) : 0
                                            function tryRegister() {
                                                if (registryEntry || !panelRef)
                                                    return
                                                console.log("qml", "TuningPanel", "slider-register", paramKey, stringIndex)
                                                panelRef.registerSlider(slider)
                                                registryEntry = slider // Just use slider reference
                                            }
                                            function doSync() {
                                                var ctrl = panelRef ? panelRef.controller : null
                                                if (!ctrl) {
                                                    console.log("qml", "TuningPanel", "slider-sync-skip", param ? param.key : "", stringIndex, "no-controller")
                                                    return
                                                }
                                                suppress = true
                                                console.log("qml", "TuningPanel", "slider-sync-start", param ? param.key : "", stringIndex)
                                                try {
                                                    var newValue = ctrl.parameterValue(param.key, stringIndex)
                                                    console.log("qml", "TuningPanel", "slider-sync-value", param ? param.key : "", stringIndex, newValue)
                                                    lastDispatchedValue = newValue
                                                    if (value !== newValue)
                                                        value = newValue
                                                    initialSyncDone = true
                                                    var baseline = ctrl.baselineValue(param.key, stringIndex)
                                                    stringRow.baselineValue = baseline
                                                    console.log("qml", "TuningPanel", "slider-sync-baseline", param ? param.key : "", stringIndex, baseline)
                                                } catch (e) {
                                                    console.log("qml", "TuningPanel", "slider-sync-error", param ? param.key : "", stringIndex, e)
                                                } finally {
                                                    suppress = false
                                                    console.log("qml", "TuningPanel", "slider-sync-complete", param ? param.key : "", stringIndex, value)
                                                }
                                            }
                                            Component.onCompleted: {
                                                console.log("qml", "TuningPanel", "slider-created", param ? param.key : "", stringRow.stringLabel, stringIndex)
                                                lastDispatchedValue = value
                                                Qt.callLater(tryRegister)
                                            }
                                            Component.onDestruction: {
                                                if (panelRef && registryEntry)
                                                    panelRef.unregisterSlider(registryEntry)
                                                registryEntry = null
                                            }
                                            Rectangle {
                                                id: baselineHandle
                                                width: 12
                                                height: 12
                                                radius: 6
                                                color: "#22c55e"
                                                opacity: 0.5
                                                border.color: "#14532d"
                                                border.width: 1
                                                anchors.verticalCenter: slider.verticalCenter
                                                x: slider.leftPadding + slider.baselineNormalized * slider.availableWidth - width / 2
                                                visible: controller && controller.compareBaseline
                                                enabled: false
                                            }
                                            Item {
                                                width: 10
                                                height: 10
                                                visible: controller && controller.compareBaseline
                                                enabled: false
                                                z: 1
                                                x: slider.leftPadding + slider.baselineNormalized * slider.availableWidth - width / 2
                                                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                                                Rectangle {
                                                    anchors.fill: parent
                                                    radius: 5
                                                    color: "#22c55e"
                                                    opacity: 0.55
                                                    border.color: "#14532d"
                                                }
                                            }
                                            onMoved: {
                                                if (!controller) {
                                                    console.log("qml", "TuningPanel", "slider-move-skipped", param ? param.key : "", stringIndex, "no-controller")
                                                    return
                                                }
                                                if (suppress) {
                                                    console.log("qml", "TuningPanel", "slider-move-skipped", param ? param.key : "", stringIndex, "suppressed")
                                                    return
                                                }
                                                var delta = value - lastDispatchedValue
                                                lastDispatchedValue = value
                                                var shiftEngaged = panelRef ? panelRef.shiftActive() : false
                                                if (shiftEngaged && Math.abs(delta) > 0) {
                                                    console.log("qml", "TuningPanel", "slider-move-group", param ? param.key : "", stringIndex, value, delta)
                                                    panelRef.applyGroupDelta(paramKey, stringIndex, delta, slider)
                                                } else {
                                                    if (panelRef)
                                                        panelRef.noteLocalChange()
                                                    console.log("qml", "TuningPanel", "slider-moved", param ? param.key : "", stringIndex, value)
                                                    controller.setParameterValue(paramKey, stringIndex, value)
                                                }
                                            }
                                            onPressedChanged: {
                                                if (pressed) {
                                                    lastDispatchedValue = value
                                                    if (controller)
                                                        controller.beginBatchEdit()
                                                } else {
                                                    if (controller)
                                                        controller.endBatchEdit()
                                                    if (panelRef)
                                                        panelRef.requestSyncAll()
                                                }
                                            }
                                        }
                                        Column {
                                            id: valueDisplay
                                            spacing: 2
                                            property bool editing: false

                                            function getDecimalPlaces(step) {
                                                // Calculate decimal places from step size
                                                if (step <= 0) return 4
                                                if (step >= 1) return 0
                                                
                                                // Count decimals: find first non-zero digit after decimal point
                                                var stepStr = step.toString()
                                                var decimalIndex = stepStr.indexOf('.')
                                                if (decimalIndex === -1) return 0
                                                
                                                // Count up to and including first non-zero digit
                                                var decimals = 0
                                                for (var i = decimalIndex + 1; i < stepStr.length; i++) {
                                                    decimals++
                                                    if (stepStr[i] !== '0') break
                                                }
                                                return decimals
                                            }

                                            function startEdit() {
                                                if (editing)
                                                    return
                                                var decimals = getDecimalPlaces(param.step)
                                                var rangeText = param.useDb 
                                                    ? ("Range: " + slider.from.toFixed(decimals) + " to " + slider.to.toFixed(decimals) + " dB")
                                                    : ("Range: " + slider.from.toFixed(decimals) + " to " + slider.to.toFixed(decimals))
                                                var currentFormatted = param.useDb 
                                                    ? slider.value.toFixed(decimals) 
                                                    : slider.value.toFixed(decimals)
                                                editing = true
                                                valueEditor.text = currentFormatted
                                                valueEditor.placeholderText = rangeText
                                                valueEditor.selectAll()
                                                valueEditor.forceActiveFocus()
                                            }

                                            function commitEdit() {
                                                if (!editing)
                                                    return
                                                var parsed = parseFloat(valueEditor.text)
                                                if (!isFinite(parsed)) {
                                                    editing = false
                                                    return
                                                }
                                                if (parsed < slider.from || parsed > slider.to) {
                                                    editing = false
                                                    return
                                                }
                                                var decimals = getDecimalPlaces(param.step)
                                                parsed = parseFloat(parsed.toFixed(decimals))
                                                editing = false
                                                if (Math.abs(parsed - slider.value) > 0.0000001) {
                                                    slider.value = parsed
                                                    slider.lastDispatchedValue = parsed
                                                    if (slider.panelRef)
                                                        slider.panelRef.noteLocalChange()
                                                    if (controller) {
                                                        console.log("qml", "TuningPanel", "value-commit", slider.paramKey, slider.stringIndex, parsed)
                                                        controller.setParameterValue(slider.paramKey, slider.stringIndex, parsed)
                                                    } else {
                                                        console.log("qml", "TuningPanel", "value-commit-skipped", slider.paramKey, slider.stringIndex, "no-controller")
                                                    }
                                                }
                                            }

                                            Item {
                                                visible: !valueDisplay.editing
                                                width: parameterCard.valueColumnWidth > 0 ? parameterCard.valueColumnWidth : valueLabel.implicitWidth
                                                height: valueLabel.implicitHeight
                                                Label {
                                                    id: valueLabel
                                                    text: {
                                                        var decimals = valueDisplay.getDecimalPlaces(param.step)
                                                        var formatted = slider.value.toFixed(decimals)
                                                        return param.useDb ? (formatted + " dB") : formatted
                                                    }
                                                    font.family: "Monospace"
                                                    font.pixelSize: 12
                                                    color: "#f8fafc"
                                                    onTextChanged: parameterCard.registerValueSample(text)
                                                    Component.onCompleted: parameterCard.registerValueSample(text)
                                                }
                                                MouseArea {
                                                    anchors.fill: parent
                                                    cursorShape: Qt.IBeamCursor
                                                    acceptedButtons: Qt.LeftButton
                                                    onDoubleClicked: valueDisplay.startEdit()
                                                }
                                            }

                                            TextField {
                                                id: valueEditor
                                                visible: valueDisplay.editing
                                                width: parameterCard.valueColumnWidth > 0 ? parameterCard.valueColumnWidth : implicitWidth
                                                selectByMouse: true
                                                font.family: "Monospace"
                                                font.pixelSize: 12
                                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                                
                                                // Enforce decimal precision based on parameter step
                                                validator: DoubleValidator {
                                                    bottom: slider.from
                                                    top: slider.to
                                                    decimals: valueDisplay.getDecimalPlaces(param.step)
                                                    notation: DoubleValidator.StandardNotation
                                                }
                                                
                                                onAccepted: valueDisplay.commitEdit()
                                                onEditingFinished: valueDisplay.commitEdit()
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        Rectangle {
                            width: parent.width
                            height: 1
                            color: "#1f2937"
                            opacity: 0.6
                        }
                        Item { height: cardPadding }
                    }
                }
            }
        } // Column (paramListColumn)
    } // Component

    function formatValue(value, useDb, step) {
        if (!isFinite(value))
            return "--"
        var decimals = useDb ? 1 : getDecimalPlaces(step || 0.001)
        return value.toFixed(decimals) + (useDb ? " dB" : "")
    }

    function getDecimalPlaces(step) {
        if (step <= 0) return 4
        var stepStr = step.toString()
        var decimalIndex = stepStr.indexOf('.')
        if (decimalIndex === -1) return 0
        return stepStr.length - decimalIndex - 1
    }

    function deltaLabel(current, baseline, useDb) {
        var delta = current - baseline
        if (Math.abs(delta) < 0.000001)
            return "= committed"
        var prefix = delta > 0 ? "↑ +" : "↓ "
        var decimals = useDb ? 1 : 3
        return prefix + Math.abs(delta).toFixed(decimals) + (useDb ? " dB" : "")
    }

    function deltaColor(delta) {
        if (Math.abs(delta) < 0.000001)
            return "#94a3b8"
        return delta > 0 ? "#34d399" : "#f97316"
    }

    function normalized(value, minValue, maxValue) {
        var span = maxValue - minValue
        if (span <= 0)
            return 0
        return Math.max(0, Math.min(1, (value - minValue) / span))
    }
}
