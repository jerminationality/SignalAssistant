#pragma once

#include <QObject>
#include <QStringList>

#include "NoteDetectionStore.h"

class DetectionTuningController : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(QStringList savedStates READ savedStates NOTIFY savedStatesChanged)
    Q_PROPERTY(bool compareBaseline READ compareBaseline WRITE setCompareBaseline NOTIFY compareBaselineChanged)
public:
    explicit DetectionTuningController(QObject* parent=nullptr);

    int revision() const { return m_revision; }
    QStringList savedStates() const;

    bool compareBaseline() const { return NoteDetectionStore::instance().compareBaseline(); }
    void setCompareBaseline(bool value);

    Q_INVOKABLE QVariantList categories() const;
    Q_INVOKABLE QStringList stringLabels() const;
    Q_INVOKABLE double parameterValue(const QString& key, int stringIndex) const;
    Q_INVOKABLE double baselineValue(const QString& key, int stringIndex) const;
    Q_INVOKABLE void setParameterValue(const QString& key, int stringIndex, double value);
    Q_INVOKABLE void beginBatchEdit();
    Q_INVOKABLE void endBatchEdit();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void revert();
    Q_INVOKABLE void resetToDefaults();
    Q_INVOKABLE void commit();
    Q_INVOKABLE void saveState(const QString& name);
    Q_INVOKABLE void loadState(const QString& name);
    Q_INVOKABLE void deleteState(const QString& name);

    void loadFromDisk();

signals:
    void revisionChanged();
    void savedStatesChanged();
    void compareBaselineChanged();

private:
    QString configDirectory() const;
    QString commitPath() const;
    QString statesPath() const;
    QString snapshotsDirectory() const;
    QString snapshotFileNameForLabel(const QString& label) const;
    QString sanitizeSnapshotName(const QString& raw) const;

    void bumpRevision();
    void persistSavedStates();
    void persistSnapshotsToDirectory(const std::map<std::string, NoteDetectionParameterSet>& snapshot) const;
    void persistLegacyStatesFile(const std::map<std::string, NoteDetectionParameterSet>& snapshot) const;
    void persistCommitted();
    void loadSnapshotsFromDirectory(std::map<std::string, NoteDetectionParameterSet>& states) const;
    bool writeParameterSet(const QString& path, const NoteDetectionParameterSet& set);
    bool readParameterSet(const QString& path, NoteDetectionParameterSet& outSet) const;

    int m_revision {0};
};
