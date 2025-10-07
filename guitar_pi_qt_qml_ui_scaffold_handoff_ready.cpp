//// README.md
# GuitarPi — Qt/QML UI Scaffold

Minimal, handoff‑ready Qt 6 + QML shell for the Raspberry Pi guitar processor project. It wires a sidebar navigation to five views (Home, Rig, Record, Tab, Settings), exposes an `AppController` to QML, and stubs an `AudioEngine` interface with a `CarlaClient` placeholder. All heavy lifting is left as TODOs with clear markers so GitHub Copilot can autocomplete implementations.

## Build (Desktop or Pi)
```bash
# deps: Qt 6 (Quick, QuickControls2), CMake ≥3.16, a C++20 compiler
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
./GuitarPi
```

## Project goals for Copilot
- Fill in `audio/CarlaClient.*` to manage JACK/Carla graph connections.
- Implement settings persistence in `AppController`.
- Replace dummy meters with real audio levels via signals/slots.
- Hook up file pickers for IRs/presets; add tab export stubs.

## See also (roadmap + inline guidance)
- **TASKS.md (Roadmap for Copilot):** full checklist for Carla/JACK graph, presets, IR management, recording, and packaging.
- **Todo Breadcrumbs Patch:** file-by-file `// TODO(Copilot)` edits to paste into this scaffold so Copilot jumps to the right spots.

---

//// CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(GuitarPi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 COMPONENTS Quick QuickControls2 Qml REQUIRED)

qt_add_executable(GuitarPi
    src/main.cpp
    src/AppController.cpp
    src/audio/AudioEngine.cpp
    src/audio/CarlaClient.cpp
)

qt_add_qml_module(GuitarPi
    URI GuitarPi
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/pages/HomePage.qml
        qml/pages/RigPage.qml
        qml/pages/RecordPage.qml
        qml/pages/TabPage.qml
        qml/pages/SettingsPage.qml
        qml/components/NavButton.qml
        qml/components/Meter.qml
        qml/components/LatencyBadge.qml
)

target_link_libraries(GuitarPi
    PRIVATE
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Qml
)

# Install rules (optional)
install(TARGETS GuitarPi RUNTIME DESTINATION bin)

---

//// src/main.cpp
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "AppController.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;
    AppController controller;

    engine.rootContext()->setContextProperty("AppController", &controller);
    const QUrl url(u"qrc:/GuitarPi/qml/Main.qml"_qs);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app,
                     [url](QObject *obj, const QUrl &objUrl) {
                         if (!obj && url == objUrl) QCoreApplication::exit(-1);
                     }, Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}

---

//// src/AppController.h
#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentPreset READ currentPreset WRITE setCurrentPreset NOTIFY currentPresetChanged)
    Q_PROPERTY(QString latencyText READ latencyText NOTIFY latencyTextChanged)
public:
    explicit AppController(QObject* parent=nullptr);

    QString currentPreset() const { return m_currentPreset; }
    void setCurrentPreset(const QString& p);

    QString latencyText() const { return m_latencyText; }

    Q_INVOKABLE QStringList availablePresets() const;          // TODO: populate from disk
    Q_INVOKABLE void savePreset(const QString& name);           // TODO
    Q_INVOKABLE void loadPreset(const QString& name);           // TODO
    Q_INVOKABLE void setBufferSize(int frames);                 // TODO: plumb to audio engine
    Q_INVOKABLE void setSampleRate(int sr);                     // TODO
    Q_INVOKABLE void startAudio();                              // TODO
    Q_INVOKABLE void stopAudio();                               // TODO

signals:
    void currentPresetChanged();
    void latencyTextChanged();

private:
    QString m_currentPreset {"Default"};
    QString m_latencyText {"—"};
};

---

//// src/AppController.cpp
#include "AppController.h"

AppController::AppController(QObject* parent): QObject(parent) {}

void AppController::setCurrentPreset(const QString& p) {
    if (p == m_currentPreset) return;
    m_currentPreset = p;
    emit currentPresetChanged();
}

QStringList AppController::availablePresets() const {
    // TODO: scan preset directory; for now return mock list
    return {"Default", "Crunch", "Chime", "Lead"};
}

void AppController::savePreset(const QString& /*name*/) {
    // TODO: serialize chain + params to YAML/JSON
}

void AppController::loadPreset(const QString& name) {
    setCurrentPreset(name);
    // TODO: deserialize preset and push params to audio engine
}

void AppController::setBufferSize(int /*frames*/) {
    // TODO: forward to audio engine + emit latencyTextChanged when changed
}

void AppController::setSampleRate(int /*sr*/) {
    // TODO
}

void AppController::startAudio() {
    // TODO
}

void AppController::stopAudio() {
    // TODO
}

---

//// src/audio/AudioEngine.h
#pragma once
#include <QObject>

class AudioEngine : public QObject {
    Q_OBJECT
public:
    explicit AudioEngine(QObject* parent=nullptr) : QObject(parent) {}
    virtual ~AudioEngine() = default;

    virtual bool start() = 0;              // RT thread(s) up, graph ready
    virtual void stop() = 0;               // tear down RT safely
    virtual void setBufferSize(int frames) = 0;
    virtual void setSampleRate(int sr) = 0;
};

---

//// src/audio/AudioEngine.cpp
// placeholder TU so CMake has a file; real logic will live in concrete engines

---

//// src/audio/CarlaClient.h
#pragma once
#include "AudioEngine.h"
#include <QObject>

// TODO: Implement Carla graph control via JACK/MIDI/OSC as needed
class CarlaClient : public AudioEngine {
    Q_OBJECT
public:
    explicit CarlaClient(QObject* parent=nullptr) : AudioEngine(parent) {}
    bool start() override;        // TODO
    void stop() override;         // TODO
    void setBufferSize(int frames) override; // TODO
    void setSampleRate(int sr) override;     // TODO

signals:
    void xrunsChanged(int count);
    void metersChanged(float inL, float inR, float outL, float outR); // poll or subscribe
};

---

//// src/audio/CarlaClient.cpp
#include "CarlaClient.h"

bool CarlaClient::start() {
    // TODO: Connect to JACK, instantiate Carla rack/graph, load default chain
    return true;
}

void CarlaClient::stop() {
    // TODO: disconnect graph, free resources (must be RT-safe)
}

void CarlaClient::setBufferSize(int /*frames*/) {
    // TODO: request JACK buffer change; update internal latency text via signal
}

void CarlaClient::setSampleRate(int /*sr*/) {
    // TODO
}

---

//// qml/Main.qml
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

---

//// qml/components/NavButton.qml
import QtQuick 2.15
import QtQuick.Controls 2.15

Button {
    id: btn
    text: "?"
    width: 56
    height: 56
    font.pixelSize: 22
    background: Rectangle {
        radius: 16
        color: btn.down ? "#334155" : "#1f2937"
        border.color: "#475569"
    }
}

---

//// qml/components/Meter.qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root
    property real level: 0.0 // 0..1
    width: 16; height: 120
    Rectangle {
        anchors.fill: parent
        color: "#111827"
        border.color: "#374151"
        radius: 6
    }
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Math.max(4, parent.height * level)
        radius: 6
        color: "#22c55e"
    }
}

---

//// qml/components/LatencyBadge.qml
import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: badge
    property string text: AppController.latencyText
    color: "#0ea5e9"
    radius: 8
    height: 28
    width: implicitWidth
    implicitWidth: label.implicitWidth + 16
    Text {
        id: label
        anchors.centerIn: parent
        text: badge.text.length ? badge.text : "latency: —"
        color: "white"
    }
}

---

//// qml/pages/HomePage.qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    anchors.fill: parent
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

---

//// qml/pages/RigPage.qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    anchors.fill: parent
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

---

//// qml/pages/RecordPage.qml
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

---

//// qml/pages/TabPage.qml
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

---

//// qml/pages/SettingsPage.qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    anchors.fill: parent
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
