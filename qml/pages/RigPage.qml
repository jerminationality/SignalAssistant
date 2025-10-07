import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    anchors.fill: parent
    // TODO(Copilot): Replace placeholders with draggable nodes bound to real plugin params
    ColumnLayout { anchors.fill: parent; anchors.margins: 24; spacing: 12
        Text { text: "Rig"; font.pixelSize: 24 }
        RowLayout {
            spacing: 12
            Rectangle { width: 120; height: 64; radius: 12; color: "#1f2937"; Text { anchors.centerIn: parent; text: "Gate"; color: "white" } }
            Rectangle { width: 120; height: 64; radius: 12; color: "#1f2937"; Text { anchors.centerIn: parent; text: "EQ"; color: "white" } }
            Rectangle { width: 120; height: 64; radius: 12; color: "#1f2937"; Text { anchors.centerIn: parent; text: "Amp"; color: "white" } }
            Rectangle { width: 120; height: 64; radius: 12; color: "#1f2937"; Text { anchors.centerIn: parent; text: "Cab IR"; color: "white" } }
            Rectangle { width: 120; height: 64; radius: 12; color: "#1f2937"; Text { anchors.centerIn: parent; text: "Comp"; color: "white" } }
        }
        Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; color: "#0b1220"; radius: 12
            Text { anchors.centerIn: parent; text: "Drag/drop graph (future)"; color: "#93c5fd" }
        }
    }
}
