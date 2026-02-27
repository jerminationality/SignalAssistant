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
    obj.insert("touchSensitivity", toJson(set.touchSensitivity));
    obj.insert("attackResponse", toJson(set.attackResponse));
    obj.insert("sustainTail", toJson(set.sustainTail));
    obj.insert("legatoSpeed", toJson(set.legatoSpeed));
    obj.insert("trackingStability", toJson(set.trackingStability));
    obj.insert("calibrationGainMultiplier", toJson(set.calibrationGainMultiplier));
    return obj;
}

// Deserialize a parameter set from JSON, with backward compatibility for legacy formats.
// If the JSON contains "touchSensitivity" we assume peak-first format; otherwise we migrate.
void deserializeParameterSet(const QJsonObject& obj, NoteDetectionParameterSet& set) {
    if (obj.contains("touchSensitivity")) {
        // ── Peak-first (Section 9A) format ──
        fromJson(obj.value("touchSensitivity"), set.touchSensitivity);
        fromJson(obj.value("attackResponse"), set.attackResponse);
        fromJson(obj.value("sustainTail"), set.sustainTail);
        fromJson(obj.value("legatoSpeed"), set.legatoSpeed);
        fromJson(obj.value("trackingStability"), set.trackingStability);
        if (obj.contains("calibrationGainMultiplier"))
            fromJson(obj.value("calibrationGainMultiplier"), set.calibrationGainMultiplier);
    } else if (obj.contains("noiseGate")) {
        // ── Legacy v2 (11-param) migration ──
        // Map old noteOnThreshold → touchSensitivity
        if (obj.contains("noteOnThreshold"))
            fromJson(obj.value("noteOnThreshold"), set.touchSensitivity);
        // Map old attackSensitivity → attackResponse
        if (obj.contains("attackSensitivity"))
            fromJson(obj.value("attackSensitivity"), set.attackResponse);
        // Map old noteOffRatio → sustainTail (approximate)
        if (obj.contains("noteOffRatio")) {
            std::array<float, 6> ratio {};
            fromJson(obj.value("noteOffRatio"), ratio);
            for (int i = 0; i < 6; ++i)
                set.sustainTail[static_cast<std::size_t>(i)] =
                    std::clamp(set.touchSensitivity[static_cast<std::size_t>(i)] * ratio[i], 0.005f, 0.1f);
        }
        // Map old repitchConfirmFrames → legatoSpeed
        if (obj.contains("repitchConfirmFrames"))
            fromJson(obj.value("repitchConfirmFrames"), set.legatoSpeed);
        // Map old pitchConfidence → trackingStability
        if (obj.contains("pitchConfidence"))
            fromJson(obj.value("pitchConfidence"), set.trackingStability);
        if (obj.contains("calibrationGainMultiplier"))
            fromJson(obj.value("calibrationGainMultiplier"), set.calibrationGainMultiplier);
    } else {
        // ── v1 (legacy 13-param) migration ──
        if (obj.contains("noteOnThreshold"))
            fromJson(obj.value("noteOnThreshold"), set.touchSensitivity);
        if (obj.contains("aubioThresholdScale"))
            fromJson(obj.value("aubioThresholdScale"), set.attackResponse);
        if (obj.contains("calibrationGainMultiplier"))
            fromJson(obj.value("calibrationGainMultiplier"), set.calibrationGainMultiplier);
        // Other fields keep defaults
    }
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
        {"detection", "Detection", {
            NoteParameter::TouchSensitivity,
            NoteParameter::AttackResponse,
            NoteParameter::SustainTail
        }},
        {"tracking", "Tracking", {
            NoteParameter::LegatoSpeed,
            NoteParameter::TrackingStability
        }},
        {"calibration", "Calibration", {
            NoteParameter::CalibrationGainMultiplier
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
                
                // Add per-string min/max if available
                if (desc->perStringMinMax) {
                    QVariantList perStringMin;
                    QVariantList perStringMax;
                    for (int i = 0; i < 6; ++i) {
                        perStringMin.append(desc->perStringMin[i]);
                        perStringMax.append(desc->perStringMax[i]);
                    }
                    item["perStringMin"] = perStringMin;
                    item["perStringMax"] = perStringMax;
                }
                
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
    return NoteDetectionStore::instance().currentValueFromKey(key.toStdString(), stringIndex);
}

double DetectionTuningController::baselineValue(const QString& key, int stringIndex) const {
    return NoteDetectionStore::instance().committedValueFromKey(key.toStdString(), stringIndex);
}

void DetectionTuningController::setParameterValue(const QString& key, int stringIndex, double value) {
    NoteDetectionStore::instance().setValueFromKey(key.toStdString(), stringIndex, static_cast<float>(value));
    bumpRevision();
}

void DetectionTuningController::beginBatchEdit() {
    NoteDetectionStore::instance().beginBatchEdit();
}

void DetectionTuningController::endBatchEdit() {
    NoteDetectionStore::instance().endBatchEdit();
}

void DetectionTuningController::undo() {
    NoteDetectionStore::instance().undo();
    bumpRevision();
}

void DetectionTuningController::redo() {
    NoteDetectionStore::instance().redo();
    bumpRevision();
}

void DetectionTuningController::revert() {
    NoteDetectionStore::instance().revert();
    bumpRevision();
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
    NoteDetectionParameterSet committed = makeDefaultNoteDetectionParameters();
    if (readParameterSet(commitPath(), committed)) {
        NoteDetectionStore::instance().applyCommittedSnapshot(committed);
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
                deserializeParameterSet(obj, set);
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
        deserializeParameterSet(obj, set);
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
    deserializeParameterSet(obj, set);
    outSet = set;
    return true;
}
