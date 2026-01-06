/****************************************************************************
** Meta object code from reading C++ file 'AppController.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/AppController.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'AppController.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.8.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN13AppControllerE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN13AppControllerE = QtMocHelpers::stringData(
    "AppController",
    "currentPresetChanged",
    "",
    "latencyTextChanged",
    "testSessionChanged",
    "testPlaybackChanged",
    "testPlaybackSettingsChanged",
    "liveRecordingLabelRequested",
    "liveHexMonitorChanged",
    "availablePresets",
    "savePreset",
    "name",
    "loadPreset",
    "setBufferSize",
    "frames",
    "setSampleRate",
    "sr",
    "startAudio",
    "stopAudio",
    "toggleLiveRecording",
    "submitLiveRecordingLabel",
    "label",
    "cancelLiveRecordingLabel",
    "testPlay",
    "testPause",
    "testStop",
    "testTogglePlayPause",
    "setTestHexAudioEnabled",
    "enabled",
    "testSeekToProgress",
    "normalized",
    "setTestLoopEnabled",
    "setLiveHexMonitorEnabled",
    "currentPreset",
    "latencyText",
    "testMode",
    "testSessionName",
    "testPlaybackState",
    "testPlaybackProgress",
    "testPlaybackDuration",
    "testPlaybackPosition",
    "testHexAudioEnabled",
    "testLoopEnabled",
    "liveHexMonitorEnabled",
    "tabBridge",
    "tuningController"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN13AppControllerE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
      25,   14, // methods
      13,  207, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       7,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    0,  164,    2, 0x06,   14 /* Public */,
       3,    0,  165,    2, 0x06,   15 /* Public */,
       4,    0,  166,    2, 0x06,   16 /* Public */,
       5,    0,  167,    2, 0x06,   17 /* Public */,
       6,    0,  168,    2, 0x06,   18 /* Public */,
       7,    0,  169,    2, 0x06,   19 /* Public */,
       8,    0,  170,    2, 0x06,   20 /* Public */,

 // methods: name, argc, parameters, tag, flags, initial metatype offsets
       9,    0,  171,    2, 0x102,   21 /* Public | MethodIsConst  */,
      10,    1,  172,    2, 0x02,   22 /* Public */,
      12,    1,  175,    2, 0x02,   24 /* Public */,
      13,    1,  178,    2, 0x02,   26 /* Public */,
      15,    1,  181,    2, 0x02,   28 /* Public */,
      17,    0,  184,    2, 0x02,   30 /* Public */,
      18,    0,  185,    2, 0x02,   31 /* Public */,
      19,    0,  186,    2, 0x02,   32 /* Public */,
      20,    1,  187,    2, 0x02,   33 /* Public */,
      22,    0,  190,    2, 0x02,   35 /* Public */,
      23,    0,  191,    2, 0x02,   36 /* Public */,
      24,    0,  192,    2, 0x02,   37 /* Public */,
      25,    0,  193,    2, 0x02,   38 /* Public */,
      26,    0,  194,    2, 0x02,   39 /* Public */,
      27,    1,  195,    2, 0x02,   40 /* Public */,
      29,    1,  198,    2, 0x02,   42 /* Public */,
      31,    1,  201,    2, 0x02,   44 /* Public */,
      32,    1,  204,    2, 0x02,   46 /* Public */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

 // methods: parameters
    QMetaType::QStringList,
    QMetaType::Void, QMetaType::QString,   11,
    QMetaType::Void, QMetaType::QString,   11,
    QMetaType::Void, QMetaType::Int,   14,
    QMetaType::Void, QMetaType::Int,   16,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   21,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool,   28,
    QMetaType::Void, QMetaType::QReal,   30,
    QMetaType::Void, QMetaType::Bool,   28,
    QMetaType::Void, QMetaType::Bool,   28,

 // properties: name, type, flags, notifyId, revision
      33, QMetaType::QString, 0x00015103, uint(0), 0,
      34, QMetaType::QString, 0x00015001, uint(1), 0,
      35, QMetaType::Bool, 0x00015001, uint(2), 0,
      36, QMetaType::QString, 0x00015001, uint(2), 0,
      37, QMetaType::QString, 0x00015001, uint(3), 0,
      38, QMetaType::QReal, 0x00015001, uint(3), 0,
      39, QMetaType::QReal, 0x00015001, uint(3), 0,
      40, QMetaType::QReal, 0x00015001, uint(3), 0,
      41, QMetaType::Bool, 0x00015103, uint(4), 0,
      42, QMetaType::Bool, 0x00015103, uint(4), 0,
      43, QMetaType::Bool, 0x00015103, uint(6), 0,
      44, QMetaType::QObjectStar, 0x00015401, uint(-1), 0,
      45, QMetaType::QObjectStar, 0x00015401, uint(-1), 0,

       0        // eod
};

Q_CONSTINIT const QMetaObject AppController::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_ZN13AppControllerE.offsetsAndSizes,
    qt_meta_data_ZN13AppControllerE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN13AppControllerE_t,
        // property 'currentPreset'
        QtPrivate::TypeAndForceComplete<QString, std::true_type>,
        // property 'latencyText'
        QtPrivate::TypeAndForceComplete<QString, std::true_type>,
        // property 'testMode'
        QtPrivate::TypeAndForceComplete<bool, std::true_type>,
        // property 'testSessionName'
        QtPrivate::TypeAndForceComplete<QString, std::true_type>,
        // property 'testPlaybackState'
        QtPrivate::TypeAndForceComplete<QString, std::true_type>,
        // property 'testPlaybackProgress'
        QtPrivate::TypeAndForceComplete<qreal, std::true_type>,
        // property 'testPlaybackDuration'
        QtPrivate::TypeAndForceComplete<qreal, std::true_type>,
        // property 'testPlaybackPosition'
        QtPrivate::TypeAndForceComplete<qreal, std::true_type>,
        // property 'testHexAudioEnabled'
        QtPrivate::TypeAndForceComplete<bool, std::true_type>,
        // property 'testLoopEnabled'
        QtPrivate::TypeAndForceComplete<bool, std::true_type>,
        // property 'liveHexMonitorEnabled'
        QtPrivate::TypeAndForceComplete<bool, std::true_type>,
        // property 'tabBridge'
        QtPrivate::TypeAndForceComplete<QObject*, std::true_type>,
        // property 'tuningController'
        QtPrivate::TypeAndForceComplete<QObject*, std::true_type>,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<AppController, std::true_type>,
        // method 'currentPresetChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'latencyTextChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'testSessionChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'testPlaybackChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'testPlaybackSettingsChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'liveRecordingLabelRequested'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'liveHexMonitorChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'availablePresets'
        QtPrivate::TypeAndForceComplete<QStringList, std::false_type>,
        // method 'savePreset'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'loadPreset'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'setBufferSize'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'setSampleRate'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'startAudio'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'stopAudio'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'toggleLiveRecording'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'submitLiveRecordingLabel'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'cancelLiveRecordingLabel'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'testPlay'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'testPause'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'testStop'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'testTogglePlayPause'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'setTestHexAudioEnabled'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'testSeekToProgress'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<qreal, std::false_type>,
        // method 'setTestLoopEnabled'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'setLiveHexMonitorEnabled'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>
    >,
    nullptr
} };

void AppController::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AppController *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->currentPresetChanged(); break;
        case 1: _t->latencyTextChanged(); break;
        case 2: _t->testSessionChanged(); break;
        case 3: _t->testPlaybackChanged(); break;
        case 4: _t->testPlaybackSettingsChanged(); break;
        case 5: _t->liveRecordingLabelRequested(); break;
        case 6: _t->liveHexMonitorChanged(); break;
        case 7: { QStringList _r = _t->availablePresets();
            if (_a[0]) *reinterpret_cast< QStringList*>(_a[0]) = std::move(_r); }  break;
        case 8: _t->savePreset((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 9: _t->loadPreset((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 10: _t->setBufferSize((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 11: _t->setSampleRate((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 12: _t->startAudio(); break;
        case 13: _t->stopAudio(); break;
        case 14: _t->toggleLiveRecording(); break;
        case 15: _t->submitLiveRecordingLabel((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 16: _t->cancelLiveRecordingLabel(); break;
        case 17: _t->testPlay(); break;
        case 18: _t->testPause(); break;
        case 19: _t->testStop(); break;
        case 20: _t->testTogglePlayPause(); break;
        case 21: _t->setTestHexAudioEnabled((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 22: _t->testSeekToProgress((*reinterpret_cast< std::add_pointer_t<qreal>>(_a[1]))); break;
        case 23: _t->setTestLoopEnabled((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 24: _t->setLiveHexMonitorEnabled((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (AppController::*)();
            if (_q_method_type _q_method = &AppController::currentPresetChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _q_method_type = void (AppController::*)();
            if (_q_method_type _q_method = &AppController::latencyTextChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _q_method_type = void (AppController::*)();
            if (_q_method_type _q_method = &AppController::testSessionChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _q_method_type = void (AppController::*)();
            if (_q_method_type _q_method = &AppController::testPlaybackChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
        {
            using _q_method_type = void (AppController::*)();
            if (_q_method_type _q_method = &AppController::testPlaybackSettingsChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 4;
                return;
            }
        }
        {
            using _q_method_type = void (AppController::*)();
            if (_q_method_type _q_method = &AppController::liveRecordingLabelRequested; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 5;
                return;
            }
        }
        {
            using _q_method_type = void (AppController::*)();
            if (_q_method_type _q_method = &AppController::liveHexMonitorChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 6;
                return;
            }
        }
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< QString*>(_v) = _t->currentPreset(); break;
        case 1: *reinterpret_cast< QString*>(_v) = _t->latencyText(); break;
        case 2: *reinterpret_cast< bool*>(_v) = _t->testMode(); break;
        case 3: *reinterpret_cast< QString*>(_v) = _t->testSessionName(); break;
        case 4: *reinterpret_cast< QString*>(_v) = _t->testPlaybackState(); break;
        case 5: *reinterpret_cast< qreal*>(_v) = _t->testPlaybackProgress(); break;
        case 6: *reinterpret_cast< qreal*>(_v) = _t->testPlaybackDuration(); break;
        case 7: *reinterpret_cast< qreal*>(_v) = _t->testPlaybackPosition(); break;
        case 8: *reinterpret_cast< bool*>(_v) = _t->testHexAudioEnabled(); break;
        case 9: *reinterpret_cast< bool*>(_v) = _t->testLoopEnabled(); break;
        case 10: *reinterpret_cast< bool*>(_v) = _t->liveHexMonitorEnabled(); break;
        case 11: *reinterpret_cast< QObject**>(_v) = _t->tabBridgeObject(); break;
        case 12: *reinterpret_cast< QObject**>(_v) = _t->tuningControllerObject(); break;
        default: break;
        }
    }
    if (_c == QMetaObject::WriteProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setCurrentPreset(*reinterpret_cast< QString*>(_v)); break;
        case 8: _t->setTestHexAudioEnabled(*reinterpret_cast< bool*>(_v)); break;
        case 9: _t->setTestLoopEnabled(*reinterpret_cast< bool*>(_v)); break;
        case 10: _t->setLiveHexMonitorEnabled(*reinterpret_cast< bool*>(_v)); break;
        default: break;
        }
    }
}

const QMetaObject *AppController::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AppController::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN13AppControllerE.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int AppController::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 25)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 25;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 25)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 25;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 13;
    }
    return _id;
}

// SIGNAL 0
void AppController::currentPresetChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void AppController::latencyTextChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void AppController::testSessionChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void AppController::testPlaybackChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void AppController::testPlaybackSettingsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}

// SIGNAL 5
void AppController::liveRecordingLabelRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 5, nullptr);
}

// SIGNAL 6
void AppController::liveHexMonitorChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 6, nullptr);
}
QT_WARNING_POP
