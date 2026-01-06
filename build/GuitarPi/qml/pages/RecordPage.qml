import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    anchors.fill: parent
    ColumnLayout { anchors.fill: parent; anchors.margins: 24; spacing: 12
        Text { text: "Record"; font.pixelSize: 24 }
        RowLayout {
            spacing: 12
            Button { text: "Arm DI" }
            Button { text: "Arm Bus" }
            Button { text: "Re-amp" }
        }
        Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; color: "#0b1220"; radius: 12
            Text { anchors.centerIn: parent; text: "Takes list / waveform (future)"; color: "#93c5fd" }
        }
    }
}
