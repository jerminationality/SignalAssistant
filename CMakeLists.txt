cmake_minimum_required(VERSION 3.16)
project(GuitarPi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 COMPONENTS Quick QuickControls2 Qml Multimedia REQUIRED)
find_package(PkgConfig QUIET)
find_package(Threads REQUIRED)

find_package(SndFile QUIET)
if (SndFile_FOUND)
    set(SNDFILE_INCLUDE_DIRS ${SNDFILE_INCLUDE_DIR})
    set(SNDFILE_LIBRARIES SndFile::sndfile)
else()
    if (PkgConfig_FOUND)
        pkg_check_modules(SNDFILE REQUIRED sndfile)
        set(SNDFILE_LIBRARIES ${SNDFILE_LINK_LIBRARIES})
    else()
        message(FATAL_ERROR "libsndfile development files not found. Install libsndfile1-dev or provide SndFile_DIR.")
    endif()
endif()

if(PkgConfig_FOUND)
    pkg_check_modules(AUBIO QUIET aubio)
endif()

qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

set(QT_IMPORT_QML_PLUGINS OFF CACHE BOOL "" FORCE)

set(QT_IMPORT_QML_PLUGINS OFF)

# Qt's auto-import step tries to link QML plugins even for dynamic builds, which
# generates warning spam because the plugin targets are not exported on Debian's
# Qt packages. Override the helper with a no-op so finalize_target stops trying
# to link them.
function(qt6_import_qml_plugins)
    # Intentionally empty – runtime plugin discovery via QQmlEngine handles it.
endfunction()

qt_add_executable(GuitarPi
    src/main.cpp
    src/AppController.cpp
    src/DetectionTuningController.cpp
    src/RecordedSessionPlayer.cpp
    src/TabEngineBridge.cpp
    src/audio/AudioEngine.cpp
    src/audio/CarlaClient.cpp
    src/audio/HexJackClient.cpp
    src/audio/JackMonitorSink.cpp
)

qt_add_executable(TabPagePreview
    src/TabPagePreview.cpp
    src/AppController.cpp
    src/DetectionTuningController.cpp
    src/RecordedSessionPlayer.cpp
    src/TabEngineBridge.cpp
    src/audio/AudioEngine.cpp
    src/audio/CarlaClient.cpp
    src/audio/HexJackClient.cpp
    src/audio/JackMonitorSink.cpp
)

file(GLOB_RECURSE GUITARPI_QML_ASSETS CONFIGURE_DEPENDS
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    qml/assets/*)

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
        qml/components/CoreAccordion.qml
        qml/components/CoreComponent.qml
        qml/components/NavButton.qml
        qml/components/Meter.qml
        qml/components/LatencyBadge.qml
        qml/components/ColorizedIcon.qml
        qml/components/MapOverview.qml
        qml/components/TestModeOverlay.qml
        qml/components/RecordingOverlay.qml
        qml/components/TuningPanel.qml
    RESOURCES
        ${GUITARPI_QML_ASSETS}
)

add_library(guitarpi_tab
    src/TabEngine.cpp
    src/TabEngine.h
    src/StringTracker.cpp
    src/StringTracker.h
    src/util.cpp
    src/util.h
    src/SessionLogger.cpp
    src/SessionLogger.h
    src/NoteDetectionConfig.cpp
    src/NoteDetectionConfig.h
    src/NoteDetectionStore.cpp
    src/NoteDetectionStore.h
)

target_include_directories(guitarpi_tab
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE
        ${SNDFILE_INCLUDE_DIRS}
)

target_link_libraries(guitarpi_tab
    PRIVATE
        ${SNDFILE_LIBRARIES}
    PUBLIC
        Threads::Threads
)

if (AUBIO_FOUND)
    target_compile_definitions(guitarpi_tab PRIVATE HAVE_AUBIO=1)
    target_include_directories(guitarpi_tab PRIVATE ${AUBIO_INCLUDE_DIRS})
    target_link_libraries(guitarpi_tab PRIVATE ${AUBIO_LIBRARIES})
else()
    message(WARNING "aubio not found; building guitarpi_tab with stubs (no real onset/pitch).")
endif()

add_executable(tab_module src/tab_module.cpp)
target_link_libraries(tab_module PRIVATE guitarpi_tab ${SNDFILE_LIBRARIES})
target_compile_definitions(tab_module PRIVATE BUILD_TAB_MODULE_TEST)

target_link_libraries(GuitarPi
    PRIVATE
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Qml
        Qt6::Multimedia
    guitarpi_tab
        carla_standalone2
        carla_utils
        jack
        /usr/lib/carla/libcarla_host-plugin.so
)

target_include_directories(GuitarPi
    PRIVATE
        /usr/include/carla
        /usr/include/carla/includes
)

target_link_libraries(TabPagePreview
    PRIVATE
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Qml
        Qt6::Multimedia
        guitarpi_tab
        carla_standalone2
        carla_utils
        jack
        /usr/lib/carla/libcarla_host-plugin.so
)

target_include_directories(TabPagePreview PRIVATE
    /usr/include/carla
    /usr/include/carla/includes
)

target_link_directories(TabPagePreview PRIVATE /usr/lib/carla)

target_link_directories(GuitarPi
    PRIVATE
        /usr/lib/carla
)

set_target_properties(GuitarPi PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    QT_IMPORT_QML_PLUGINS OFF
)
install(TARGETS GuitarPi RUNTIME DESTINATION bin)

set_target_properties(TabPagePreview PROPERTIES
    QT_IMPORT_QML_PLUGINS OFF
)

set(QT_IMPORT_QML_PLUGINS OFF CACHE BOOL "" FORCE)