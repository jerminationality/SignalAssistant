import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import "components"
import "pages"

ApplicationWindow {
    id: root
    visible: true
    visibility: Window.FullScreen
    width: 1280
    height: 720
    title: "GuitarPi"
    property bool tuningPanelVisible: false
    property bool testOverlayHidden: false
    property var currentBridge: pageLoader.item ? pageLoader.item.bridge : null
    onTuningPanelVisibleChanged: console.log("qml", "tuning-panel-visible", tuningPanelVisible ? "open" : "closed")
    Component.onCompleted: {
        console.log("qml", "ApplicationWindow completed")
        AppController.startAudio()
    }
    Component.onDestruction: AppController.stopAudio()

    Shortcut {
        id: liveRecordShortcut
        context: Qt.ApplicationShortcut
        sequence: "Ctrl+R"
        enabled: AppController && !AppController.testMode
        onActivated: AppController && AppController.toggleLiveRecording()
    }
    Shortcut {
        id: testOverlayShortcut
        context: Qt.ApplicationShortcut
        sequence: "Ctrl+S"
        enabled: AppController && AppController.testMode
        onActivated: root.testOverlayHidden = !root.testOverlayHidden
    }
    Shortcut {
        id: tuningPanelShortcut
        context: Qt.ApplicationShortcut
        sequence: "Ctrl+T"
        enabled: !!AppController
        onActivated: root.tuningPanelVisible = !root.tuningPanelVisible
    }

    Shortcut {
        context: Qt.ApplicationShortcut
        sequence: "Escape"
        enabled: root.tuningPanelVisible
        onActivated: root.tuningPanelVisible = false
    }

    Loader {
        id: pageLoader
        anchors.fill: parent
        sourceComponent: tabPageComponent
    }

    Component {
        id: rigPageComponent
        RigPage {
            anchors.fill: parent
            onShowTabCaptureRequested: pageLoader.sourceComponent = tabPageComponent
        }
    }

    Component {
        id: tabPageComponent
        TabPage {
            anchors.fill: parent
            bridge: AppController ? AppController.tabBridge : (typeof TabBridge !== "undefined" ? TabBridge : null)
        }
    }

    TestModeOverlay {
        id: testOverlay
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: 24
        anchors.bottomMargin: 4
        visible: AppController && AppController.testMode && !root.testOverlayHidden
        sessionName: AppController ? AppController.testSessionName : ""
        playbackState: AppController ? AppController.testPlaybackState : ""
        progress: AppController ? AppController.testPlaybackProgress : 0
        duration: AppController ? AppController.testPlaybackDuration : 0
        position: AppController ? AppController.testPlaybackPosition : 0
        hexAudioEnabled: AppController ? AppController.testHexAudioEnabled : false
        loopEnabled: AppController ? AppController.testLoopEnabled : false
        onPlayRequested: AppController && AppController.testPlay()
        onPauseRequested: AppController && AppController.testPause()
        onStopRequested: AppController && AppController.testStop()
        onHexMonitorToggled: function(enabled) { AppController && AppController.setTestHexAudioEnabled(enabled) }
        onSeekRequested: function(value) { AppController && AppController.testSeekToProgress(value) }
        onLoopToggled: function(enabled) { AppController && AppController.setTestLoopEnabled(enabled) }
    }

    RecordingOverlay {
        id: recordingOverlay
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 24
        visible: AppController && !AppController.testMode && AppController.tabBridge && AppController.tabBridge.recording
        onStopRequested: AppController && AppController.toggleLiveRecording()
    }

    Dialog {
        id: recordingLabelDialog
        modal: true
        focus: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        title: "Label Recording"
        anchors.centerIn: parent
        contentItem: Column {
            spacing: 8
            width: 320
            Label {
                text: "Enter a label for this session"
                color: "#E0E0E0"
                wrapMode: Text.WordWrap
            }
            TextField {
                id: recordingLabelField
                placeholderText: "e.g. Chorus take"
                focus: true
            }
        }
        onAccepted: {
            if (AppController)
                AppController.submitLiveRecordingLabel(recordingLabelField.text)
            recordingLabelField.text = ""
        }
        onRejected: {
            if (AppController)
                AppController.cancelLiveRecordingLabel()
            recordingLabelField.text = ""
        }
    }

    Connections {
        target: AppController
        function onLiveRecordingLabelRequested() {
            if (!recordingLabelDialog.visible)
                recordingLabelDialog.open()
            recordingLabelField.selectAll()
            recordingLabelField.forceActiveFocus()
        }
        function onTestSessionChanged() {
            if (!(AppController && AppController.testMode))
                root.testOverlayHidden = false
        }
    }

    TuningPanel {
        id: tuningPanel
        x: 24
        y: 24
        width: root.width - 48
        z: 50
        controller: AppController ? AppController.tuningController : null
        bridge: root.currentBridge
        visible: root.tuningPanelVisible
        enabled: root.tuningPanelVisible
        onCloseRequested: root.tuningPanelVisible = false
    }
}
