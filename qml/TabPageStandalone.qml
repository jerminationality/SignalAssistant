import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "pages"

ApplicationWindow {
    width: 960
    height: 640
    visible: true
    title: qsTr("Tab Page Preview (Mock)")

    QtObject {
        id: mockBridge
        property var events: []
        property string eventsJson: "[]"
        property bool recording: false

        function requestRefresh() {
            // no-op in mock
        }

        function setRecording(val) {
            recording = !!val;
        }

        function clear() {
            events = [];
            eventsJson = "[]";
        }

        function seedMockSession() {
            const data = [
                        { string: 5, fret: 0, start: 0.00, end: 1.40, velocity: 0.78, articulation: "" },
                        { string: 4, fret: 2, start: 0.45, end: 1.20, velocity: 0.65, articulation: "hammer" },
                        { string: 3, fret: 2, start: 1.00, end: 1.60, velocity: 0.62, articulation: "slide" },
                        { string: 3, fret: 4, start: 1.62, end: 2.10, velocity: 0.72, articulation: "slide" },
                        { string: 2, fret: 0, start: 2.20, end: 2.80, velocity: 0.35, articulation: "pm" }
                    ];
            events = data;
            eventsJson = JSON.stringify(data);
        }
    }

    TabPage {
        anchors.fill: parent
        bridge: mockBridge
    }

    Component.onCompleted: mockBridge.seedMockSession()
}
