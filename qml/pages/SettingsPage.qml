import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    anchors.fill: parent
    // TODO(Copilot): Populate SR/buffer options from actual device caps via AppController
    ColumnLayout { anchors.fill: parent; anchors.margins: 24; spacing: 12
        Text { text: "Settings"; font.pixelSize: 24 }
        RowLayout {
            spacing: 12
            Label { text: "Sample Rate" }
            ComboBox { model: [44100, 48000, 96000]; onActivated: AppController.setSampleRate(currentText) }
            Label { text: "Buffer (frames)" }
            ComboBox { model: [64, 128, 256]; onActivated: AppController.setBufferSize(currentText) }
            Button { text: "Start"; onClicked: AppController.startAudio() }
            Button { text: "Stop"; onClicked: AppController.stopAudio() }
        }
        Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; color: "#0b1220"; radius: 12
            Text { anchors.centerIn: parent; text: "Hex mapping / gains (future)"; color: "#93c5fd" }
        }
    }
}
