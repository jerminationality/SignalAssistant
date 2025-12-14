import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../components"

Item {
    anchors.fill: parent
    // TODO(Copilot): Bind meters to AppController/App signals for real input/output levels
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        RowLayout {
            spacing: 16
            Meter { level: 0.3 }
            Meter { level: 0.28 }
            LatencyBadge {}
            ComboBox {
                id: presetBox
                model: AppController.availablePresets()
                onActivated: AppController.loadPreset(currentText)
            }
            Button { text: "Save Preset"; onClicked: AppController.savePreset(presetBox.currentText) }
        }

        Rectangle { Layout.fillWidth: true; Layout.fillHeight: true; color: "#0b1220"; radius: 12
            Text { anchors.centerIn: parent; text: "Home: tuner, big meters, quick actions"; color: "#93c5fd" }
        }
    }
}
