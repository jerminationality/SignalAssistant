#include "DetectionTuningController.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QDebug>

#include <algorithm>
#include <array>
#include <initializer_list>

namespace {

QJsonArray toJson(const std::array<float, 6>& arr) {
    QJsonArray json;
    for (float v : arr)
        json.append(v);
    return json;
}

void fromJson(const QJsonValue& value, std::array<float, 6>& arr) {
    const QJsonArray json = value.toArray();
    if (json.size() != 6)
        return;
    for (int i = 0; i < 6; ++i)
        arr[static_cast<std::size_t>(i)] = static_cast<float>(json[i].toDouble());
}

QJsonObject serializeParameterSetJson(const NoteDetectionParameterSet& set) {
    QJsonObject obj;
    obj.insert("onsetThresholdScale", toJson(set.onsetThresholdScale));
    obj.insert("baselineFloor", toJson(set.baselineFloor));
    obj.insert("envelopeFloor", toJson(set.envelopeFloor));
    obj.insert("gateRatio", toJson(set.gateRatio));
    obj.insert("sustainFloorScale", toJson(set.sustainFloorScale));
    obj.insert("retriggerGateScale", toJson(set.retriggerGateScale));
    obj.insert("peakReleaseRatio", toJson(set.peakReleaseRatio));
    obj.insert("pitchTolerance", toJson(set.pitchTolerance));
    obj.insert("targetRms", toJson(set.targetRms));
    // calibrationGainMultiplier is not saved to tuning settings
    // It is ONLY managed by the calibration profile
    obj.insert("lowCutMultiplier", toJson(set.lowCutMultiplier));
    obj.insert("highCutMultiplier", toJson(set.highCutMultiplier));
    obj.insert("aubioThresholdScale", toJson(set.aubioThresholdScale));
    obj.insert("onsetSilenceDb", toJson(set.onsetSilenceDb));
    obj.insert("pitchSilenceDb", toJson(set.pitchSilenceDb));
    return obj;
}

QVariantList buildCategories() {
    const auto& descriptors = parameterDescriptors();
    const auto lookup = [&descriptors](NoteParameter id) -> const ParameterDescriptor* {
        const auto it = std::find_if(descriptors.begin(), descriptors.end(), [id](const auto& desc) {
            return desc.id == id;
        });
        if (it == descriptors.end())
            return nullptr;
        return &(*it);
    };

    struct CategoryDef {
        const char* id;
        const char* title;
        std::initializer_list<NoteParameter> params;
    };

    const std::array<CategoryDef, 3> kCategoryDefs = {{
        {"envelope", "Envelope & Gate", {
            NoteParameter::OnsetThresholdScale,
            NoteParameter::BaselineFloor,
            NoteParameter::EnvelopeFloor,
            NoteParameter::GateRatio,
            NoteParameter::SustainFloorScale,
            NoteParameter::RetriggerGateScale,
            NoteParameter::PeakReleaseRatio
        }},
        {"pitch", "Pitch Tracking", {
            NoteParameter::PitchTolerance,
            NoteParameter::AubioThresholdScale,
            NoteParameter::OnsetSilenceDb,
            NoteParameter::PitchSilenceDb
        }},
        {"filters", "Calibration & Filters", {
            NoteParameter::TargetRms,
            NoteParameter::CalibrationGainMultiplier,
            NoteParameter::LowCutMultiplier,
            NoteParameter::HighCutMultiplier
        }}
    }};

    QVariantList categories;
    for (const auto& category : kCategoryDefs) {
        QVariantList params;
        for (NoteParameter paramId : category.params) {
            if (const auto* desc = lookup(paramId)) {
                QVariantMap item;
                item["key"] = QString::fromStdString(desc->key);
                item["label"] = QString::fromStdString(desc->label);
                item["description"] = QString::fromStdString(desc->description);
                item["min"] = desc->minValue;
                item["max"] = desc->maxValue;
                item["step"] = desc->step;
                item["useDb"] = desc->useDecibels;
                params.append(item);
            }
        }
        if (params.isEmpty())
            continue;
        QVariantMap entry;
        entry["id"] = QString::fromUtf8(category.id);
        entry["title"] = QString::fromUtf8(category.title);
        entry["parameters"] = params;
        categories.append(entry);
    }
    return categories;
}

} // namespace

DetectionTuningController::DetectionTuningController(QObject* parent)
    : QObject(parent) {
    loadFromDisk();
}

QStringList DetectionTuningController::savedStates() const {
    const auto names = NoteDetectionStore::instance().availableStates();
    QStringList list;
    for (const auto& name : names)
        list.append(QString::fromStdString(name));
    return list;
}

void DetectionTuningController::setCompareBaseline(bool value) {
    if (compareBaseline() == value)
        return;
    NoteDetectionStore::instance().setCompareBaseline(value);
    emit compareBaselineChanged();
}

QVariantList DetectionTuningController::categories() const {
    return buildCategories();
}

QStringList DetectionTuningController::stringLabels() const {
    QStringList labels;
    labels.reserve(kNumStrings);
    for (int i = 0; i < kNumStrings; ++i)
        labels.append(QString::fromStdString(defaultStringLabel(i)));
    return labels;
}

double DetectionTuningController::parameterValue(const QString& key, int stringIndex) const {
    qInfo() << "tuning" << "param-value-enter" << key << stringIndex;
    const double value = NoteDetectionStore::instance().currentValueFromKey(key.toStdString(), stringIndex);
    qInfo() << "tuning" << "param-value-exit" << key << stringIndex << value;
    return value;
}

double DetectionTuningController::baselineValue(const QString& key, int stringIndex) const {
    qInfo() << "tuning" << "baseline-value-enter" << key << stringIndex;
    const double value = NoteDetectionStore::instance().committedValueFromKey(key.toStdString(), stringIndex);
    qInfo() << "tuning" << "baseline-value-exit" << key << stringIndex << value;
    return value;
}

void DetectionTuningController::setParameterValue(const QString& key, int stringIndex, double value) {
    qInfo() << "tuning" << "set-value-enter" << key << stringIndex << value << m_revision;
    NoteDetectionStore::instance().setValueFromKey(key.toStdString(), stringIndex, static_cast<float>(value));
    bumpRevision();
    qInfo() << "tuning" << "set-value-exit" << key << stringIndex << value << m_revision;
}

void DetectionTuningController::beginBatchEdit() {
    NoteDetectionStore::instance().beginBatchEdit();
}

void DetectionTuningController::endBatchEdit() {
    NoteDetectionStore::instance().endBatchEdit();
}

void DetectionTuningController::undo() {
    qInfo() << "tuning" << "undo-enter" << m_revision;
    NoteDetectionStore::instance().undo();
    bumpRevision();
    qInfo() << "tuning" << "undo-exit" << m_revision;
}

void DetectionTuningController::redo() {
    qInfo() << "tuning" << "redo-enter" << m_revision;
    NoteDetectionStore::instance().redo();
    bumpRevision();
    qInfo() << "tuning" << "redo-exit" << m_revision;
}

void DetectionTuningController::revert() {
    qInfo() << "tuning" << "revert-enter" << m_revision;
    NoteDetectionStore::instance().revert();
    bumpRevision();
    qInfo() << "tuning" << "revert-exit" << m_revision;
}

void DetectionTuningController::resetToDefaults() {
    NoteDetectionStore::instance().resetToDefaults();
    bumpRevision();
}

void DetectionTuningController::commit() {
    NoteDetectionStore::instance().commit();
    persistCommitted();
    persistSavedStates();
    bumpRevision();
}

void DetectionTuningController::saveState(const QString& name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return;
    NoteDetectionStore::instance().saveState(trimmed.toStdString());
    persistSavedStates();
    emit savedStatesChanged();
    bumpRevision();
}

void DetectionTuningController::loadState(const QString& name) {
    if (NoteDetectionStore::instance().loadState(name.toStdString())) {
        bumpRevision();
    }
}

void DetectionTuningController::deleteState(const QString& name) {
    auto states = NoteDetectionStore::instance().savedStatesSnapshot();
    if (states.erase(name.toStdString()) > 0) {
        NoteDetectionStore::instance().replaceSavedStates(states);
        persistSavedStates();
        emit savedStatesChanged();
        bumpRevision();
    }
}

void DetectionTuningController::loadFromDisk() {
    // Save calibration multipliers before loading tuning settings
    std::array<float, 6> savedMultipliers;
    for (int s = 0; s < 6; ++s) {
        savedMultipliers[static_cast<std::size_t>(s)] = 
            NoteDetectionStore::instance().currentValueFromKey("calibrationGainMultiplier", s);
    }
    
    NoteDetectionParameterSet committed = makeDefaultNoteDetectionParameters();
    if (readParameterSet(commitPath(), committed)) {
        NoteDetectionStore::instance().applyCommittedSnapshot(committed);
    }
    
    // Restore calibration multipliers - they should only come from calibration profile
    for (int s = 0; s < 6; ++s) {
        NoteDetectionStore::instance().setValueFromKey("calibrationGainMultiplier", s, 
                                                       savedMultipliers[static_cast<std::size_t>(s)]);
    }

    std::map<std::string, NoteDetectionParameterSet> states;
    QFile statesFile(statesPath());
    if (statesFile.exists() && statesFile.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(statesFile.readAll());
        if (doc.isObject()) {
            const QJsonObject root = doc.object();
            for (auto it = root.begin(); it != root.end(); ++it) {
                NoteDetectionParameterSet set = makeDefaultNoteDetectionParameters();
                const QJsonObject obj = it.value().toObject();
                fromJson(obj.value("onsetThresholdScale"), set.onsetThresholdScale);
                fromJson(obj.value("baselineFloor"), set.baselineFloor);
                fromJson(obj.value("envelopeFloor"), set.envelopeFloor);
                fromJson(obj.value("gateRatio"), set.gateRatio);
                fromJson(obj.value("sustainFloorScale"), set.sustainFloorScale);
                fromJson(obj.value("retriggerGateScale"), set.retriggerGateScale);
                fromJson(obj.value("peakReleaseRatio"), set.peakReleaseRatio);
                fromJson(obj.value("pitchTolerance"), set.pitchTolerance);
                if (obj.contains("targetRms")) {
                    fromJson(obj.value("targetRms"), set.targetRms);
                } else if (obj.contains("calibrationLift")) {
                    // Legacy: convert calibrationLift to targetRms
                    std::array<float, 6> legacyLift;
                    fromJson(obj.value("calibrationLift"), legacyLift);
                    for (size_t i = 0; i < 6; ++i)
                        set.targetRms[i] = 0.0018f * legacyLift[i];
                }
                // calibrationGainMultiplier is not loaded from tuning settings
                // It is ONLY set by the calibration profile in TabEngineBridge::loadPersistentCalibration()
                // Keep default values (will be overwritten by calibration profile)
                fromJson(obj.value("lowCutMultiplier"), set.lowCutMultiplier);
                fromJson(obj.value("highCutMultiplier"), set.highCutMultiplier);
                fromJson(obj.value("aubioThresholdScale"), set.aubioThresholdScale);
                fromJson(obj.value("onsetSilenceDb"), set.onsetSilenceDb);
                fromJson(obj.value("pitchSilenceDb"), set.pitchSilenceDb);
                states[it.key().toStdString()] = set;
            }
        }
    }

    loadSnapshotsFromDirectory(states);
    NoteDetectionStore::instance().replaceSavedStates(states);
    emit savedStatesChanged();
    bumpRevision();
}

void DetectionTuningController::loadSnapshotsFromDirectory(std::map<std::string, NoteDetectionParameterSet>& states) const {
    QDir dir(snapshotsDirectory());
    if (!dir.exists())
        return;
    const QStringList files = dir.entryList(QStringList{QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString& fileName : files) {
        QFile snapshot(dir.filePath(fileName));
        if (!snapshot.open(QIODevice::ReadOnly))
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(snapshot.readAll());
        snapshot.close();
        if (!doc.isObject())
            continue;
        const QJsonObject obj = doc.object();
        NoteDetectionParameterSet set = makeDefaultNoteDetectionParameters();
        fromJson(obj.value("onsetThresholdScale"), set.onsetThresholdScale);
        fromJson(obj.value("baselineFloor"), set.baselineFloor);
        fromJson(obj.value("envelopeFloor"), set.envelopeFloor);
        fromJson(obj.value("gateRatio"), set.gateRatio);
        fromJson(obj.value("sustainFloorScale"), set.sustainFloorScale);
        fromJson(obj.value("retriggerGateScale"), set.retriggerGateScale);
        fromJson(obj.value("peakReleaseRatio"), set.peakReleaseRatio);
        fromJson(obj.value("pitchTolerance"), set.pitchTolerance);
        if (obj.contains("targetRms")) {
            fromJson(obj.value("targetRms"), set.targetRms);
        } else if (obj.contains("calibrationLift")) {
            // Legacy: convert calibrationLift to targetRms
            std::array<float, 6> legacyLift;
            fromJson(obj.value("calibrationLift"), legacyLift);
            for (size_t i = 0; i < 6; ++i)
                set.targetRms[i] = 0.0018f * legacyLift[i];
        }
        if (obj.contains("calibrationGainMultiplier")) {
            fromJson(obj.value("calibrationGainMultiplier"), set.calibrationGainMultiplier);
        } else {
            for (size_t i = 0; i < 6; ++i)
                set.calibrationGainMultiplier[i] = 1.0f;
        }
        fromJson(obj.value("lowCutMultiplier"), set.lowCutMultiplier);
        fromJson(obj.value("highCutMultiplier"), set.highCutMultiplier);
        fromJson(obj.value("aubioThresholdScale"), set.aubioThresholdScale);
        fromJson(obj.value("onsetSilenceDb"), set.onsetSilenceDb);
        fromJson(obj.value("pitchSilenceDb"), set.pitchSilenceDb);
        const QString label = obj.value(QStringLiteral("label")).toString(QFileInfo(fileName).completeBaseName());
        states[label.toStdString()] = set;
    }
}

QString DetectionTuningController::configDirectory() const {
    QDir dir(QDir::current());
    dir.mkpath("configs/note_detection");
    return dir.filePath("configs/note_detection");
}

QString DetectionTuningController::commitPath() const {
    return QDir(configDirectory()).filePath("committed.json");
}

QString DetectionTuningController::statesPath() const {
    return QDir(configDirectory()).filePath("states.json");
}

QString DetectionTuningController::snapshotsDirectory() const {
    QDir dir(QDir::home());
    dir.mkpath("snapshots/notetracker");
    return dir.filePath("snapshots/notetracker");
}

QString DetectionTuningController::sanitizeSnapshotName(const QString& raw) const {
    QString normalized = raw.trimmed().toLower();
    QString result;
    result.reserve(normalized.size());
    for (const QChar& ch : normalized) {
        if (ch.isLetterOrNumber()) {
            result.append(ch);
        } else if (ch.isSpace()) {
            result.append('_');
        } else if (ch == QLatin1Char('-') || ch == QLatin1Char('_')) {
            result.append(ch);
        }
    }
    if (result.isEmpty())
        result = QStringLiteral("snapshot");
    while (result.contains("__"))
        result.replace("__", "_");
    return result.left(48);
}

QString DetectionTuningController::snapshotFileNameForLabel(const QString& label) const {
    const QString base = sanitizeSnapshotName(label);
    const QByteArray hash = QCryptographicHash::hash(label.toUtf8(), QCryptographicHash::Sha1).toHex().left(8);
    return QStringLiteral("%1_%2.json").arg(base, QString::fromLatin1(hash));
}

void DetectionTuningController::bumpRevision() {
    m_revision = (m_revision + 1) % 1000000;
    emit revisionChanged();
}

void DetectionTuningController::persistSavedStates() {
    const auto snapshot = NoteDetectionStore::instance().savedStatesSnapshot();
    persistSnapshotsToDirectory(snapshot);
    persistLegacyStatesFile(snapshot);
}

void DetectionTuningController::persistSnapshotsToDirectory(const std::map<std::string, NoteDetectionParameterSet>& snapshot) const {
    QDir dir(snapshotsDirectory());
    QSet<QString> retained;
    for (const auto& entry : snapshot) {
        const QString label = QString::fromStdString(entry.first);
        const QString fileName = snapshotFileNameForLabel(label);
        retained.insert(fileName);
        QJsonObject obj = serializeParameterSetJson(entry.second);
        obj.insert(QStringLiteral("label"), label);
        QSaveFile file(dir.filePath(fileName));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qWarning() << "tuning" << "snapshot-save-open-failed" << file.fileName();
            continue;
        }
        file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
        if (!file.commit())
            qWarning() << "tuning" << "snapshot-save-commit-failed" << file.fileName();
    }

    const QStringList existing = dir.entryList(QStringList{QStringLiteral("*.json")}, QDir::Files);
    for (const QString& fileName : existing) {
        if (!retained.contains(fileName))
            QFile::remove(dir.filePath(fileName));
    }
}

void DetectionTuningController::persistLegacyStatesFile(const std::map<std::string, NoteDetectionParameterSet>& snapshot) const {
    QJsonObject root;
    for (const auto& entry : snapshot)
        root.insert(QString::fromStdString(entry.first), serializeParameterSetJson(entry.second));

    QFile file(statesPath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
    }
}

void DetectionTuningController::persistCommitted() {
    writeParameterSet(commitPath(), NoteDetectionStore::instance().snapshotCommitted());
}

bool DetectionTuningController::writeParameterSet(const QString& path, const NoteDetectionParameterSet& set) {
    const QJsonObject obj = serializeParameterSetJson(set);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool DetectionTuningController::readParameterSet(const QString& path, NoteDetectionParameterSet& outSet) const {
    QFile file(path);
    if (!file.exists())
        return false;
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject())
        return false;
    const QJsonObject obj = doc.object();
    NoteDetectionParameterSet set = outSet;
    fromJson(obj.value("onsetThresholdScale"), set.onsetThresholdScale);
    fromJson(obj.value("baselineFloor"), set.baselineFloor);
    fromJson(obj.value("envelopeFloor"), set.envelopeFloor);
    fromJson(obj.value("gateRatio"), set.gateRatio);
    fromJson(obj.value("sustainFloorScale"), set.sustainFloorScale);
    fromJson(obj.value("retriggerGateScale"), set.retriggerGateScale);
    fromJson(obj.value("peakReleaseRatio"), set.peakReleaseRatio);
    fromJson(obj.value("pitchTolerance"), set.pitchTolerance);
    if (obj.contains("targetRms")) {
        fromJson(obj.value("targetRms"), set.targetRms);
    } else if (obj.contains("calibrationLift")) {
        // Legacy: convert calibrationLift to targetRms
        std::array<float, 6> legacyLift;
        fromJson(obj.value("calibrationLift"), legacyLift);
        for (size_t i = 0; i < 6; ++i)
            set.targetRms[i] = 0.0018f * legacyLift[i];
    }
    if (obj.contains("calibrationGainMultiplier")) {
        fromJson(obj.value("calibrationGainMultiplier"), set.calibrationGainMultiplier);
    } else {
        for (size_t i = 0; i < 6; ++i)
            set.calibrationGainMultiplier[i] = 1.0f;
    }
    fromJson(obj.value("lowCutMultiplier"), set.lowCutMultiplier);
    fromJson(obj.value("highCutMultiplier"), set.highCutMultiplier);
    fromJson(obj.value("aubioThresholdScale"), set.aubioThresholdScale);
    fromJson(obj.value("onsetSilenceDb"), set.onsetSilenceDb);
    fromJson(obj.value("pitchSilenceDb"), set.pitchSilenceDb);
    outSet = set;
    return true;
}
