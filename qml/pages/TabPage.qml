import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    anchors.fill: parent
    ColumnLayout { anchors.fill: parent; anchors.margins: 24; spacing: 12
        Text { text: "Tab"; font.pixelSize: 24 }
        RowLayout {
            spacing: 12
            Button { text: "Capture" }
            Button { text: "Quantize" }
            Button { text: "Export GPX" }
        }
        Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; color: "#0b1220"; radius: 12
            Text { anchors.centerIn: parent; text: "Live tab view / editor (future)"; color: "#93c5fd" }
        }
    }
}
