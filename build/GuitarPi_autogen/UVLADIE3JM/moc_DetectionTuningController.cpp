/****************************************************************************
** Meta object code from reading C++ file 'DetectionTuningController.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/DetectionTuningController.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'DetectionTuningController.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN25DetectionTuningControllerE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN25DetectionTuningControllerE = QtMocHelpers::stringData(
    "DetectionTuningController",
    "revisionChanged",
    "",
    "savedStatesChanged",
    "compareBaselineChanged",
    "categories",
    "QVariantList",
    "stringLabels",
    "parameterValue",
    "key",
    "stringIndex",
    "baselineValue",
    "setParameterValue",
    "value",
    "beginBatchEdit",
    "endBatchEdit",
    "undo",
    "redo",
    "revert",
    "resetToDefaults",
    "commit",
    "saveState",
    "name",
    "loadState",
    "deleteState",
    "revision",
    "savedStates",
    "compareBaseline"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN25DetectionTuningControllerE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
      18,   14, // methods
       3,  160, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       3,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    0,  122,    2, 0x06,    4 /* Public */,
       3,    0,  123,    2, 0x06,    5 /* Public */,
       4,    0,  124,    2, 0x06,    6 /* Public */,

 // methods: name, argc, parameters, tag, flags, initial metatype offsets
       5,    0,  125,    2, 0x102,    7 /* Public | MethodIsConst  */,
       7,    0,  126,    2, 0x102,    8 /* Public | MethodIsConst  */,
       8,    2,  127,    2, 0x102,    9 /* Public | MethodIsConst  */,
      11,    2,  132,    2, 0x102,   12 /* Public | MethodIsConst  */,
      12,    3,  137,    2, 0x02,   15 /* Public */,
      14,    0,  144,    2, 0x02,   19 /* Public */,
      15,    0,  145,    2, 0x02,   20 /* Public */,
      16,    0,  146,    2, 0x02,   21 /* Public */,
      17,    0,  147,    2, 0x02,   22 /* Public */,
      18,    0,  148,    2, 0x02,   23 /* Public */,
      19,    0,  149,    2, 0x02,   24 /* Public */,
      20,    0,  150,    2, 0x02,   25 /* Public */,
      21,    1,  151,    2, 0x02,   26 /* Public */,
      23,    1,  154,    2, 0x02,   28 /* Public */,
      24,    1,  157,    2, 0x02,   30 /* Public */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

 // methods: parameters
    0x80000000 | 6,
    QMetaType::QStringList,
    QMetaType::Double, QMetaType::QString, QMetaType::Int,    9,   10,
    QMetaType::Double, QMetaType::QString, QMetaType::Int,    9,   10,
    QMetaType::Void, QMetaType::QString, QMetaType::Int, QMetaType::Double,    9,   10,   13,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   22,
    QMetaType::Void, QMetaType::QString,   22,
    QMetaType::Void, QMetaType::QString,   22,

 // properties: name, type, flags, notifyId, revision
      25, QMetaType::Int, 0x00015001, uint(0), 0,
      26, QMetaType::QStringList, 0x00015001, uint(1), 0,
      27, QMetaType::Bool, 0x00015103, uint(2), 0,

       0        // eod
};

Q_CONSTINIT const QMetaObject DetectionTuningController::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_ZN25DetectionTuningControllerE.offsetsAndSizes,
    qt_meta_data_ZN25DetectionTuningControllerE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN25DetectionTuningControllerE_t,
        // property 'revision'
        QtPrivate::TypeAndForceComplete<int, std::true_type>,
        // property 'savedStates'
        QtPrivate::TypeAndForceComplete<QStringList, std::true_type>,
        // property 'compareBaseline'
        QtPrivate::TypeAndForceComplete<bool, std::true_type>,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<DetectionTuningController, std::true_type>,
        // method 'revisionChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'savedStatesChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'compareBaselineChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'categories'
        QtPrivate::TypeAndForceComplete<QVariantList, std::false_type>,
        // method 'stringLabels'
        QtPrivate::TypeAndForceComplete<QStringList, std::false_type>,
        // method 'parameterValue'
        QtPrivate::TypeAndForceComplete<double, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'baselineValue'
        QtPrivate::TypeAndForceComplete<double, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'setParameterValue'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<double, std::false_type>,
        // method 'beginBatchEdit'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'endBatchEdit'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'undo'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'redo'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'revert'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'resetToDefaults'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'commit'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'saveState'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'loadState'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'deleteState'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>
    >,
    nullptr
} };

void DetectionTuningController::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<DetectionTuningController *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->revisionChanged(); break;
        case 1: _t->savedStatesChanged(); break;
        case 2: _t->compareBaselineChanged(); break;
        case 3: { QVariantList _r = _t->categories();
            if (_a[0]) *reinterpret_cast< QVariantList*>(_a[0]) = std::move(_r); }  break;
        case 4: { QStringList _r = _t->stringLabels();
            if (_a[0]) *reinterpret_cast< QStringList*>(_a[0]) = std::move(_r); }  break;
        case 5: { double _r = _t->parameterValue((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])));
            if (_a[0]) *reinterpret_cast< double*>(_a[0]) = std::move(_r); }  break;
        case 6: { double _r = _t->baselineValue((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])));
            if (_a[0]) *reinterpret_cast< double*>(_a[0]) = std::move(_r); }  break;
        case 7: _t->setParameterValue((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<double>>(_a[3]))); break;
        case 8: _t->beginBatchEdit(); break;
        case 9: _t->endBatchEdit(); break;
        case 10: _t->undo(); break;
        case 11: _t->redo(); break;
        case 12: _t->revert(); break;
        case 13: _t->resetToDefaults(); break;
        case 14: _t->commit(); break;
        case 15: _t->saveState((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 16: _t->loadState((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 17: _t->deleteState((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _q_method_type = void (DetectionTuningController::*)();
            if (_q_method_type _q_method = &DetectionTuningController::revisionChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _q_method_type = void (DetectionTuningController::*)();
            if (_q_method_type _q_method = &DetectionTuningController::savedStatesChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _q_method_type = void (DetectionTuningController::*)();
            if (_q_method_type _q_method = &DetectionTuningController::compareBaselineChanged; *reinterpret_cast<_q_method_type *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< int*>(_v) = _t->revision(); break;
        case 1: *reinterpret_cast< QStringList*>(_v) = _t->savedStates(); break;
        case 2: *reinterpret_cast< bool*>(_v) = _t->compareBaseline(); break;
        default: break;
        }
    }
    if (_c == QMetaObject::WriteProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 2: _t->setCompareBaseline(*reinterpret_cast< bool*>(_v)); break;
        default: break;
        }
    }
}

const QMetaObject *DetectionTuningController::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *DetectionTuningController::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN25DetectionTuningControllerE.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int DetectionTuningController::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 18)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 18;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 18)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 18;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    }
    return _id;
}

// SIGNAL 0
void DetectionTuningController::revisionChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void DetectionTuningController::savedStatesChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void DetectionTuningController::compareBaselineChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}
QT_WARNING_POP
