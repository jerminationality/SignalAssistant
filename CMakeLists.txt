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

install(TARGETS GuitarPi RUNTIME DESTINATION bin)
