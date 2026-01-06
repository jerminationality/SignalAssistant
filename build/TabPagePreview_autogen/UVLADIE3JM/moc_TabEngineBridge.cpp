/****************************************************************************
** Meta object code from reading C++ file 'TabEngineBridge.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/TabEngineBridge.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'TabEngineBridge.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN15TabEngineBridgeE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN15TabEngineBridgeE = QtMocHelpers::stringData(
    "TabEngineBridge",
    "eventsChanged",
    "",
    "recordingChanged",
    "liveNoteTriggered",
    "stringIndex",
    "fretIndex",
    "velocity",
    "liveNoteEnded",
    "liveNoteEnvelopeUpdated",
    "envelope",
    "hexMetersChanged",
    "rawMetersChanged",
    "thresholdsChanged",
    "calibrationStatusChanged",
    "tuningModeEnabledChanged",
    "tuningDeviationChanged",
    "calibrationGainsChanged",
    "updateLiveMeters",
    "std::array<float,6>",
    "meters",
    "handleCalibrationStarted",
    "handleCalibrationStepChanged",
    "capturing",
    "handleCalibrationFinished",
    "averages",
    "peaks",
    "handleCalibrationBaselineFloorCaptured",
    "noiseFloor",
    "handleCalibrationFadeComplete",
    "requestRefresh",
    "clear",
    "seedMockSession",
    "setRecording",
    "value",
    "startCalibration",
    "recalibrateString",
    "setTuningModeEnabled",
    "enabled",
    "setCalibrationGain",
    "gain",
    "updateCalibrationMultipliers",
    "events",
    "QVariantList",
    "eventsJson",
    "recording",
    "hexMeters",
    "rawMeters",
    "thresholds",
    "calibrationRunning",
    "calibrationMessage",
    "calibrationSteps",
    "calibrationReady",
    "tuningModeEnabled",
    "tuningDeviation",
    "calibrationGains"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN15TabEngineBridgeE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
      27,   14, // methods
      13,  239, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
      12,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    0,  176,    2, 0x06,   14 /* Public */,
       3,    0,  177,    2, 0x06,   15 /* Public */,
       4,    3,  178,    2, 0x06,   16 /* Public */,
       8,    2,  185,    2, 0x06,   20 /* Public */,
       9,    2,  190,    2, 0x06,   23 /* Public */,
      11,    0,  195,    2, 0x06,   26 /* Public */,
      12,    0,  196,    2, 0x06,   27 /* Public */,
      13,    0,  197,    2, 0x06,   28 /* Public */,
      14,    0,  198,    2, 0x06,   29 /* Public */,
      15,    0,  199,    2, 0x06,   30 /* Public */,
      16,    0,  200,    2, 0x06,   31 /* Public */,
      17,    0,  201,    2, 0x06,   32 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
      18,    1,  202,    2, 0x0a,   33 /* Public */,
      21,    0,  205,    2, 0x0a,   35 /* Public */,
      22,    2,  206,    2, 0x0a,   36 /* Public */,
      24,    2,  211,    2, 0x0a,   39 /* Public */,
      27,    1,  216,    2, 0x0a,   42 /* Public */,
      29,    0,  219,    2, 0x0a,   44 /* Public */,

 // methods: name, argc, parameters, tag, flags, initial metatype offsets
      30,    0,  220,    2, 0x02,   45 /* Public */,
      31,    0,  221,    2, 0x02,   46 /* Public */,
      32,    0,  222,    2, 0x02,   47 /* Public */,
      33,    1,  223,    2, 0x02,   48 /* Public */,
      35,    0,  226,    2, 0x02,   50 /* Public */,
      36,    1,  227,    2, 0x02,   51 /* Public */,
      37,    1,  230,    2, 0x02,   53 /* Public */,
      39,    2,  233,    2, 0x02,   55 /* Public */,
      41,    0,  238,    2, 0x02,   58 /* Public */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int, QMetaType::Int, QMetaType::Float,    5,    6,    7,
    QMetaType::Void, QMetaType::Int, QMetaType::Int,    5,    6,
    QMetaType::Void, QMetaType::Int, QMetaType::Float,    5,   10,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void, 0x80000000 | 19,   20,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int, QMetaType::Bool,    5,   23,
    QMetaType::Void, 0x80000000 | 19, 0x80000000 | 19,   25,   26,
    QMetaType::Void, QMetaType::Float,   28,
    QMetaType::Void,

 // methods: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool,   34,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,    5,
    QMetaType::Void, QMetaType::Bool,   38,
    QMetaType::Void, QMetaType::Int, QMetaType::Double,    5,   40,
    QMetaType::Void,

 // properties: name, type, flags, notifyId, revision
      42, 0x80000000 | 43, 0x00015009, uint(0), 0,
      44, QMetaType::QString, 0x00015001, uint(0), 0,
      45, QMetaType::Bool, 0x00015103, uint(1), 0,
      46, 0x80000000 | 43, 0x00015009, uint(5), 0,
      47, 0x80000000 | 43, 0x00015009, uint(6), 0,
      48, 0x80000000 | 43, 0x00015009, uint(7), 0,
      49, QMetaType::Bool, 0x00015001, uint(8), 0,
      50, QMetaType::QString, 0x00015001, uint(8), 0,
      51, 0x80000000 | 43, 0x00015009, uint(8), 0,
      52, QMetaType::Bool, 0x00015001, uint(8), 0,
      53, QMetaType::Bool, 0x00015103, uint(9), 0,
      54, 0x80000000 | 43, 0x00015009, uint(10), 0,
      55, 0x80000000 | 43, 0x00015009, uint(11), 0,

       0        // eod
};

Q_CONSTINIT const QMetaObject TabEngineBridge::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_ZN15TabEngineBridgeE.offsetsAndSizes,
    qt_meta_data_ZN15TabEngineBridgeE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN15TabEngineBridgeE_t,
        // property 'events'
        QtPrivate::TypeAndForceComplete<QVariantList, std::true_type>,
        // property 'eventsJson'
        QtPrivate::TypeAndForceComplete<QString, std::true_type>,
        // property 'recording'
        QtPrivate::TypeAndForceComplete<bool, std::true_type>,
        // property 'hexMeters'
        QtPrivate::TypeAndForceComplete<QVariantList, std::true_type>,
        // property 'rawMeters'
        QtPrivate::TypeAndForceComplete<QVariantList, std::true_type>,
        // property 'thresholds'
        QtPrivate::TypeAndForceComplete<QVariantList, std::true_type>,
        // property 'calibrationRunning'
        QtPrivate::TypeAndForceComplete<bool, std::true_type>,
        // property 'calibrationMessage'
        QtPrivate::TypeAndForceComplete<QString, std::true_type>,
        // property 'calibrationSteps'
        QtPrivate::TypeAndForceComplete<QVariantList, std::true_type>,
        // property 'calibrationReady'
        QtPrivate::TypeAndForceComplete<bool, std::true_type>,
        // property 'tuningModeEnabled'
        QtPrivate::TypeAndForceComplete<bool, std::true_type>,
        // property 'tuningDeviation'
        QtPrivate::TypeAndForceComplete<QVariantList, std::true_type>,
        // property 'calibrationGains'
        QtPrivate::TypeAndForceComplete<QVariantList, std::true_type>,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<TabEngineBridge, std::true_type>,
        // method 'eventsChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'recordingChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'liveNoteTriggered'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<float, std::false_type>,
        // method 'liveNoteEnded'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'liveNoteEnvelopeUpdated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<float, std::false_type>,
        // method 'hexMetersChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'rawMetersChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'thresholdsChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'calibrationStatusChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'tuningModeEnabledChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'tuningDeviationChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'calibrationGainsChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'updateLiveMeters'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const std::array<float,6> &, std::false_type>,
        // method 'handleCalibrationStarted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'handleCalibrationStepChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'handleCalibrationFinished'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const std::array<float,6> &, std::false_type>,
        QtPrivate::TypeAndForceComplete<const std::array<float,6> &, std::false_type>,
        // method 'handleCalibrationBaselineFloorCaptured'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<float, std::false_type>,
        // method 'handleCalibrationFadeComplete'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'requestRefresh'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'clear'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'seedMockSession'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'setRecording'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'startCalibration'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'recalibrateString'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'setTuningModeEnabled'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'setCalibrationGain'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<double, std::false_type>,
        // method 'updateCalibrationMultipliers'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void TabEngineBridge::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<TabEngineBridge *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->eventsChanged(); break;
        case 1: _t->recordingChanged(); break;
        case 2: _t->liveNoteTriggered((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<float>>(_a[3]))); break;
        case 3: _t->liveNoteEnded((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 4: _t->liveNoteEnvelopeUpdated((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<float>>(_a[2]))); break;
        case 5: _t->hexMetersChanged(); break;
        case 6: _t->rawMetersChanged(); break;
        case 7: _t->thresholdsChanged(); break;
        case 8: _t->calibrationStatusChanged(); break;
        case 9: _t->tuningModeEnabledChanged(); break;
        case 10: _t->tuningDeviationChanged(); break;
        case 11: _t->calibrationGainsChanged(); break;
        case 12: _t->updateLiveMeters((*reinterpret_cast< std::add_pointer_t<std::array<float,6>>>(_a[1]))); break;
        case 13: _t->handleCalibrationStarted(); break;
        case 14: _t->handleCalibrationStepChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        case 15: _t->handleCalibrationFinished((*reinterpret_cast< std::add_pointer_t<std::array<float,6>>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<std::array<float,6>>>(_a[2]))); break;
        case 16: _t->handleCalibrationBaselineFloorCaptured((*reinterpret_cast< std::add_pointer_t<float>>(_a[1]))); break;
        case 17: _t->handleCalibrationFadeComplete(); break;
        case 18: _t->requestRefresh(); break;
        case 19: _t->clear(); break;
        case 20: _t->seedMockSession(); break;
        case 21: _t->setRecording((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 22: _t->startCalibration(); break;
        case 23: _t->recalibrateString((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 24: _t->setTuningModeEnabled((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 25: _t->setCalibrationGain((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<double>>(_a[2]))); break;
        case 26: _t->updateCalibrationMultipliers(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (TabEngineBridge::*)();
            if (_q_method_type _q_method = &TabEngineBridge::eventsChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _q_method_type = void (TabEngineBridge::*)();
            if (_q_method_type _q_method = &TabEngineBridge::recordingChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _q_method_type = void (TabEngineBridge::*)(int , int , float );
            if (_q_method_type _q_method = &TabEngineBridge::liveNoteTriggered; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _q_method_type = void (TabEngineBridge::*)(int , int );
            if (_q_method_type _q_method = &TabEngineBridge::liveNoteEnded; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
        {
            using _q_method_type = void (TabEngineBridge::*)(int , float );
            if (_q_method_type _q_method = &TabEngineBridge::liveNoteEnvelopeUpdated; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 4;
                return;
            }
        }
        {
            using _q_method_type = void (TabEngineBridge::*)();
            if (_q_method_type _q_method = &TabEngineBridge::hexMetersChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 5;
                return;
            }
        }
        {
            using _q_method_type = void (TabEngineBridge::*)();
            if (_q_method_type _q_method = &TabEngineBridge::rawMetersChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 6;
                return;
            }
        }
        {
            using _q_method_type = void (TabEngineBridge::*)();
            if (_q_method_type _q_method = &TabEngineBridge::thresholdsChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 7;
                return;
            }
        }
        {
            using _q_method_type = void (TabEngineBridge::*)();
            if (_q_method_type _q_method = &TabEngineBridge::calibrationStatusChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 8;
                return;
            }
        }
        {
            using _q_method_type = void (TabEngineBridge::*)();
            if (_q_method_type _q_method = &TabEngineBridge::tuningModeEnabledChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 9;
                return;
            }
        }
        {
            using _q_method_type = void (TabEngineBridge::*)();
            if (_q_method_type _q_method = &TabEngineBridge::tuningDeviationChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 10;
                return;
            }
        }
        {
            using _q_method_type = void (TabEngineBridge::*)();
            if (_q_method_type _q_method = &TabEngineBridge::calibrationGainsChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 11;
                return;
            }
        }
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< QVariantList*>(_v) = _t->events(); break;
        case 1: *reinterpret_cast< QString*>(_v) = _t->eventsJson(); break;
        case 2: *reinterpret_cast< bool*>(_v) = _t->recording(); break;
        case 3: *reinterpret_cast< QVariantList*>(_v) = _t->hexMeters(); break;
        case 4: *reinterpret_cast< QVariantList*>(_v) = _t->rawMeters(); break;
        case 5: *reinterpret_cast< QVariantList*>(_v) = _t->thresholds(); break;
        case 6: *reinterpret_cast< bool*>(_v) = _t->calibrationRunning(); break;
        case 7: *reinterpret_cast< QString*>(_v) = _t->calibrationMessage(); break;
        case 8: *reinterpret_cast< QVariantList*>(_v) = _t->calibrationSteps(); break;
        case 9: *reinterpret_cast< bool*>(_v) = _t->calibrationReady(); break;
        case 10: *reinterpret_cast< bool*>(_v) = _t->tuningModeEnabled(); break;
        case 11: *reinterpret_cast< QVariantList*>(_v) = _t->tuningDeviation(); break;
        case 12: *reinterpret_cast< QVariantList*>(_v) = _t->calibrationGains(); break;
        default: break;
        }
    }
    if (_c == QMetaObject::WriteProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 2: _t->setRecording(*reinterpret_cast< bool*>(_v)); break;
        case 10: _t->setTuningModeEnabled(*reinterpret_cast< bool*>(_v)); break;
        default: break;
        }
    }
}

const QMetaObject *TabEngineBridge::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *TabEngineBridge::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN15TabEngineBridgeE.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int TabEngineBridge::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 27)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 27;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 27)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 27;
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
void TabEngineBridge::eventsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void TabEngineBridge::recordingChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void TabEngineBridge::liveNoteTriggered(int _t1, int _t2, float _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void TabEngineBridge::liveNoteEnded(int _t1, int _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void TabEngineBridge::liveNoteEnvelopeUpdated(int _t1, float _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void TabEngineBridge::hexMetersChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 5, nullptr);
}

// SIGNAL 6
void TabEngineBridge::rawMetersChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 6, nullptr);
}

// SIGNAL 7
void TabEngineBridge::thresholdsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 7, nullptr);
}

// SIGNAL 8
void TabEngineBridge::calibrationStatusChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 8, nullptr);
}

// SIGNAL 9
void TabEngineBridge::tuningModeEnabledChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 9, nullptr);
}

// SIGNAL 10
void TabEngineBridge::tuningDeviationChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 10, nullptr);
}

// SIGNAL 11
void TabEngineBridge::calibrationGainsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 11, nullptr);
}
QT_WARNING_POP
