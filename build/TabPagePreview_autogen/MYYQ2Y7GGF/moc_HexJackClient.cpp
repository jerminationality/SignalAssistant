/****************************************************************************
** Meta object code from reading C++ file 'HexJackClient.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/audio/HexJackClient.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'HexJackClient.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN13HexJackClientE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN13HexJackClientE = QtMocHelpers::stringData(
    "HexJackClient",
    "bufferConfigChanged",
    "",
    "sampleRate",
    "bufferSize",
    "xrunsChanged",
    "count",
    "hexMetersSnapshot",
    "HexMeterArray",
    "meters",
    "calibrationStarted",
    "calibrationStepChanged",
    "stringIndex",
    "capturing",
    "calibrationFinished",
    "std::array<float,6>",
    "averages",
    "peaks",
    "calibrationBaselineFloorCaptured",
    "noiseFloor"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN13HexJackClientE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
       7,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       7,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    2,   56,    2, 0x06,    1 /* Public */,
       5,    1,   61,    2, 0x06,    4 /* Public */,
       7,    1,   64,    2, 0x06,    6 /* Public */,
      10,    0,   67,    2, 0x06,    8 /* Public */,
      11,    2,   68,    2, 0x06,    9 /* Public */,
      14,    2,   73,    2, 0x06,   12 /* Public */,
      18,    1,   78,    2, 0x06,   15 /* Public */,

 // signals: parameters
    QMetaType::Void, QMetaType::Int, QMetaType::Int,    3,    4,
    QMetaType::Void, QMetaType::Int,    6,
    QMetaType::Void, 0x80000000 | 8,    9,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int, QMetaType::Bool,   12,   13,
    QMetaType::Void, 0x80000000 | 15, 0x80000000 | 15,   16,   17,
    QMetaType::Void, QMetaType::Float,   19,

       0        // eod
};

Q_CONSTINIT const QMetaObject HexJackClient::staticMetaObject = { {
    QMetaObject::SuperData::link<AudioEngine::staticMetaObject>(),
    qt_meta_stringdata_ZN13HexJackClientE.offsetsAndSizes,
    qt_meta_data_ZN13HexJackClientE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN13HexJackClientE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<HexJackClient, std::true_type>,
        // method 'bufferConfigChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'xrunsChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'hexMetersSnapshot'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const HexMeterArray &, std::false_type>,
        // method 'calibrationStarted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'calibrationStepChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'calibrationFinished'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const std::array<float,6> &, std::false_type>,
        QtPrivate::TypeAndForceComplete<const std::array<float,6> &, std::false_type>,
        // method 'calibrationBaselineFloorCaptured'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<float, std::false_type>
    >,
    nullptr
} };

void HexJackClient::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<HexJackClient *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->bufferConfigChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 1: _t->xrunsChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 2: _t->hexMetersSnapshot((*reinterpret_cast< std::add_pointer_t<HexMeterArray>>(_a[1]))); break;
        case 3: _t->calibrationStarted(); break;
        case 4: _t->calibrationStepChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        case 5: _t->calibrationFinished((*reinterpret_cast< std::add_pointer_t<std::array<float,6>>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<std::array<float,6>>>(_a[2]))); break;
        case 6: _t->calibrationBaselineFloorCaptured((*reinterpret_cast< std::add_pointer_t<float>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (HexJackClient::*)(int , int );
            if (_q_method_type _q_method = &HexJackClient::bufferConfigChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _q_method_type = void (HexJackClient::*)(int );
            if (_q_method_type _q_method = &HexJackClient::xrunsChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _q_method_type = void (HexJackClient::*)(const HexMeterArray & );
            if (_q_method_type _q_method = &HexJackClient::hexMetersSnapshot; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _q_method_type = void (HexJackClient::*)();
            if (_q_method_type _q_method = &HexJackClient::calibrationStarted; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
        {
            using _q_method_type = void (HexJackClient::*)(int , bool );
            if (_q_method_type _q_method = &HexJackClient::calibrationStepChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 4;
                return;
            }
        }
        {
            using _q_method_type = void (HexJackClient::*)(const std::array<float,6> & , const std::array<float,6> & );
            if (_q_method_type _q_method = &HexJackClient::calibrationFinished; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 5;
                return;
            }
        }
        {
            using _q_method_type = void (HexJackClient::*)(float );
            if (_q_method_type _q_method = &HexJackClient::calibrationBaselineFloorCaptured; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 6;
                return;
            }
        }
    }
}

const QMetaObject *HexJackClient::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *HexJackClient::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN13HexJackClientE.stringdata0))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "HexAudioClient"))
        return static_cast< HexAudioClient*>(this);
    return AudioEngine::qt_metacast(_clname);
}

int HexJackClient::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = AudioEngine::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 7)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 7;
    }
    return _id;
}

// SIGNAL 0
void HexJackClient::bufferConfigChanged(int _t1, int _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void HexJackClient::xrunsChanged(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void HexJackClient::hexMetersSnapshot(const HexMeterArray & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void HexJackClient::calibrationStarted()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void HexJackClient::calibrationStepChanged(int _t1, bool _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void HexJackClient::calibrationFinished(const std::array<float,6> & _t1, const std::array<float,6> & _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void HexJackClient::calibrationBaselineFloorCaptured(float _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}
QT_WARNING_POP
