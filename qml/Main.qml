import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ApplicationWindow {
    id: root
    visible: true
    width: 1200
    height: 720
    title: "GuitarPi"

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Sidebar navigation
        Rectangle {
            id: sidebar
            color: "#0f172a" // slate-900
            width: 72
            Layout.fillHeight: true
            Column {
                spacing: 8
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 16
                NavButton { text: "🏠"; onClicked: stack.replace(homeComponent) }
                NavButton { text: "🎛"; onClicked: stack.replace(rigComponent) }
                NavButton { text: "⏺"; onClicked: stack.replace(recordComponent) }
                NavButton { text: "𝄞"; onClicked: stack.replace(tabComponent) }
                NavButton { text: "⚙"; onClicked: stack.replace(settingsComponent) }
            }
        }

        // Main content
        StackView {
            id: stack
            Layout.fillWidth: true
            Layout.fillHeight: true
            initialItem: homeComponent
        }
    }

    Component { id: homeComponent; HomePage {} }
    Component { id: rigComponent; RigPage {} }
    Component { id: recordComponent; RecordPage {} }
    Component { id: tabComponent; TabPage {} }
    Component { id: settingsComponent; SettingsPage {} }
}
