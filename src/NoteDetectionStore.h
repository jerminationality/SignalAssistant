#pragma once

#include "NoteDetectionConfig.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct NoteDetectionParameterSetAtomic {
    std::array<std::atomic<float>, 6> noiseGate;
    std::array<std::atomic<float>, 6> attackSensitivity;
    std::array<std::atomic<float>, 6> triggerGuardMs;
    std::array<std::atomic<float>, 6> noteOnThreshold;
    std::array<std::atomic<float>, 6> noteOffRatio;
    std::array<std::atomic<float>, 6> calibrationGainMultiplier;
    std::array<std::atomic<float>, 6> repitchThreshold;
    std::array<std::atomic<float>, 6> repitchConfirmFrames;
    std::array<std::atomic<float>, 6> repitchMinConfidence;
    std::array<std::atomic<float>, 6> pitchConfidence;
    std::array<std::atomic<float>, 6> retriggerDeltaRatio;

    void store(const NoteDetectionParameterSet& source);
};

class NoteDetectionStore {
public:
    static NoteDetectionStore& instance();

    const NoteDetectionParameterSet& defaults() const { return m_defaults; }
    const NoteDetectionParameterSet& current() const { return m_current; }
    const NoteDetectionParameterSet& committed() const { return m_committed; }

    float activeValue(NoteParameter id, int stringIdx) const;
    void setValue(NoteParameter id, int stringIdx, float value);

    // Coalesce multiple rapid changes (e.g. slider drag) into a single undo entry.
    void beginBatchEdit();
    void endBatchEdit();

    void undo();
    void redo();
    void revert();
    void clearHistory();
    void commit();

    void saveState(const std::string& name);
    bool loadState(const std::string& name);
    std::vector<std::string> availableStates() const;
    std::map<std::string, NoteDetectionParameterSet> savedStatesSnapshot() const;
    void replaceSavedStates(const std::map<std::string, NoteDetectionParameterSet>& states);

    NoteDetectionParameterSet snapshotCurrent() const;
    NoteDetectionParameterSet snapshotCommitted() const;
    void applyCommittedSnapshot(const NoteDetectionParameterSet& set);
    void applyCurrentSnapshot(const NoteDetectionParameterSet& set);

    void resetToDefaults();

    void setValueFromKey(const std::string& key, int stringIdx, float value);
    float currentValueFromKey(const std::string& key, int stringIdx) const;
    float committedValueFromKey(const std::string& key, int stringIdx) const;

    std::uint64_t activeGeneration() const {
        return m_activeGeneration.load(std::memory_order_acquire);
    }

    void setCompareBaseline(bool enabled) { m_compareBaseline.store(enabled); }
    bool compareBaseline() const { return m_compareBaseline.load(); }

private:
    NoteDetectionStore();

    float* access(NoteDetectionParameterSet& set, NoteParameter id, int stringIdx);
    const float* access(const NoteDetectionParameterSet& set, NoteParameter id, int stringIdx) const;
    static std::optional<NoteParameter> parameterFromKey(const std::string& key);
    void pushUndo();
    void syncActive();

    int m_batchEditDepth {0};
    bool m_batchUndoPushed {false};

    NoteDetectionParameterSet m_defaults;
    NoteDetectionParameterSet m_current;
    NoteDetectionParameterSet m_committed;
    NoteDetectionParameterSetAtomic m_active;
    std::vector<NoteDetectionParameterSet> m_undoStack;
    std::vector<NoteDetectionParameterSet> m_redoStack;
    std::map<std::string, NoteDetectionParameterSet> m_savedStates;
    mutable std::mutex m_mutex;
    std::atomic<std::uint64_t> m_activeGeneration {1};
    std::atomic<bool> m_compareBaseline {false};
};
