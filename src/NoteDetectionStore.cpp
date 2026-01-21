#include "NoteDetectionStore.h"

#include <algorithm>
#include <cstdio>
#include <cstdarg>

// ============================================================================
// PRE-CALCULATED THRESHOLD SCALING TABLE (Linear Growth Model)
// ============================================================================
// This table stores (Base_Multiplier + (0.35 * (Fret / 24))) for all 150 notes.
// It eliminates division and complex math in the high-speed audio thread.
// Note-OFF threshold = Note-ON threshold * 0.5 (50% hysteresis)
// ============================================================================
const float THRESHOLD_SCALING_TABLE[6][25] = {
    // S0 (Low E) - Base 0.52 -> Max 0.87
    {0.520f, 0.535f, 0.549f, 0.564f, 0.578f, 0.593f, 0.608f, 0.622f, 0.637f, 0.651f, 0.666f, 0.680f, 0.695f, 0.710f, 0.724f, 0.739f, 0.753f, 0.768f, 0.783f, 0.797f, 0.812f, 0.826f, 0.841f, 0.855f, 0.870f},
    // S1 (A) - Base 0.56 -> Max 0.91
    {0.560f, 0.575f, 0.589f, 0.604f, 0.618f, 0.633f, 0.648f, 0.662f, 0.677f, 0.691f, 0.706f, 0.720f, 0.735f, 0.750f, 0.764f, 0.779f, 0.793f, 0.808f, 0.823f, 0.837f, 0.852f, 0.866f, 0.881f, 0.895f, 0.910f},
    // S2 (D) - Base 0.60 -> Max 0.95
    {0.600f, 0.615f, 0.629f, 0.644f, 0.658f, 0.673f, 0.688f, 0.702f, 0.717f, 0.731f, 0.746f, 0.760f, 0.775f, 0.790f, 0.804f, 0.819f, 0.833f, 0.848f, 0.863f, 0.877f, 0.892f, 0.906f, 0.921f, 0.935f, 0.950f},
    // S3 (G) - Base 0.64 -> Max 0.99
    {0.640f, 0.655f, 0.669f, 0.684f, 0.698f, 0.713f, 0.728f, 0.742f, 0.757f, 0.771f, 0.786f, 0.800f, 0.815f, 0.830f, 0.844f, 0.859f, 0.873f, 0.888f, 0.903f, 0.917f, 0.932f, 0.946f, 0.961f, 0.975f, 0.990f},
    // S4 (B) - Base 0.68 -> Max 1.03
    {0.680f, 0.695f, 0.709f, 0.724f, 0.738f, 0.753f, 0.768f, 0.782f, 0.797f, 0.811f, 0.826f, 0.840f, 0.855f, 0.870f, 0.884f, 0.899f, 0.913f, 0.928f, 0.943f, 0.957f, 0.972f, 0.986f, 1.001f, 1.015f, 1.030f},
    // S5 (High E) - Base 0.72 -> Max 1.07
    {0.720f, 0.735f, 0.749f, 0.764f, 0.778f, 0.793f, 0.808f, 0.822f, 0.837f, 0.851f, 0.866f, 0.880f, 0.895f, 0.910f, 0.924f, 0.939f, 0.953f, 0.968f, 0.983f, 0.997f, 1.012f, 1.026f, 1.041f, 1.055f, 1.070f}
};

// Note-OFF hysteresis multiplier (50% of Note-ON threshold)
constexpr float NOTE_OFF_HYSTERESIS = 0.5f;

namespace {

void logStoreLookup(const char* stage, const std::string& key, int stringIdx, const float* value = nullptr) {
    // Logging disabled for performance
}

void logStoreDetail(const char* fmt, ...) {
    // Logging disabled for performance
}

}

void NoteDetectionParameterSetAtomic::store(const NoteDetectionParameterSet& source) {
    const auto transfer = [](auto& destArr, const auto& srcArr) {
        for (std::size_t i = 0; i < destArr.size(); ++i)
            destArr[i].store(srcArr[i], std::memory_order_release);
    };
    transfer(yinThreshold, source.yinThreshold);
    transfer(noiseGateRMS, source.noiseGateRMS);
    transfer(onsetSensitivity, source.onsetSensitivity);
    transfer(releaseRatio, source.releaseRatio);
    transfer(fretStabilityFrames, source.fretStabilityFrames);
    transfer(targetRms, source.targetRms);
    transfer(calibrationGainMultiplier, source.calibrationGainMultiplier);
    transfer(spatialWeight, source.spatialWeight);
}

NoteDetectionStore& NoteDetectionStore::instance() {
    static NoteDetectionStore store;
    return store;
}

NoteDetectionStore::NoteDetectionStore() {
    m_defaults = makeDefaultNoteDetectionParameters();
    m_current = m_defaults;
    m_committed = m_defaults;
    m_active.store(m_current);
}

float* NoteDetectionStore::access(NoteDetectionParameterSet& set, NoteParameter id, int stringIdx) {
    if (stringIdx < 0 || stringIdx >= kNumStrings) {
        return nullptr;
    }

    float* result = nullptr;
    switch (id) {
        case NoteParameter::YINThreshold: result = &set.yinThreshold[static_cast<std::size_t>(stringIdx)]; break;
        case NoteParameter::NoiseGateRMS: result = &set.noiseGateRMS[static_cast<std::size_t>(stringIdx)]; break;
        case NoteParameter::OnsetSensitivity: result = &set.onsetSensitivity[static_cast<std::size_t>(stringIdx)]; break;
        case NoteParameter::ReleaseRatio: result = &set.releaseRatio[static_cast<std::size_t>(stringIdx)]; break;
        case NoteParameter::TargetRms: result = &set.targetRms[static_cast<std::size_t>(stringIdx)]; break;
        case NoteParameter::CalibrationGainMultiplier: result = &set.calibrationGainMultiplier[static_cast<std::size_t>(stringIdx)]; break;
        case NoteParameter::SpatialWeight: result = &set.spatialWeight[static_cast<std::size_t>(stringIdx)]; break;
        case NoteParameter::FretStabilityFrames: return nullptr; // int type, handled separately
    }

    return result;
}

const float* NoteDetectionStore::access(const NoteDetectionParameterSet& set, NoteParameter id, int stringIdx) const {
    auto* self = const_cast<NoteDetectionStore*>(this);
    return self->access(const_cast<NoteDetectionParameterSet&>(set), id, stringIdx);
}

float NoteDetectionStore::activeValue(NoteParameter id, int stringIdx) const {
    if (stringIdx < 0 || stringIdx >= kNumStrings)
        return 0.f;
    const auto fetch = [stringIdx](const auto& arr) {
        return arr[static_cast<std::size_t>(stringIdx)].load(std::memory_order_acquire);
    };
    switch (id) {
        case NoteParameter::YINThreshold: return fetch(m_active.yinThreshold);
        case NoteParameter::NoiseGateRMS: return fetch(m_active.noiseGateRMS);
        case NoteParameter::OnsetSensitivity: return fetch(m_active.onsetSensitivity);
        case NoteParameter::ReleaseRatio: return fetch(m_active.releaseRatio);
        case NoteParameter::FretStabilityFrames: return static_cast<float>(m_active.fretStabilityFrames[static_cast<std::size_t>(stringIdx)].load(std::memory_order_acquire));
        case NoteParameter::TargetRms: return fetch(m_active.targetRms);
        case NoteParameter::CalibrationGainMultiplier: return fetch(m_active.calibrationGainMultiplier);
        case NoteParameter::SpatialWeight: return fetch(m_active.spatialWeight);
    }
    return 0.f;
}

void NoteDetectionStore::setValue(NoteParameter id, int stringIdx, float value) {
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_batchEditDepth > 0) {
        if (!m_batchUndoPushed) {
            pushUndo();
            m_batchUndoPushed = true;
        }
    } else {
        pushUndo();
    }
    if (id == NoteParameter::FretStabilityFrames) {
        // Handle int type separately
        m_current.fretStabilityFrames[static_cast<std::size_t>(stringIdx)] = static_cast<int>(value);
    } else if (float* ptr = access(m_current, id, stringIdx)) {
        *ptr = value;
    }
    m_redoStack.clear();
    syncActive();
}

void NoteDetectionStore::beginBatchEdit() {
    std::lock_guard<std::mutex> guard(m_mutex);
    ++m_batchEditDepth;
    if (m_batchEditDepth == 1)
        m_batchUndoPushed = false;
}

void NoteDetectionStore::endBatchEdit() {
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_batchEditDepth <= 0) {
        m_batchEditDepth = 0;
        m_batchUndoPushed = false;
        return;
    }
    --m_batchEditDepth;
    if (m_batchEditDepth == 0)
        m_batchUndoPushed = false;
}

void NoteDetectionStore::setValueFromKey(const std::string& key, int stringIdx, float value) {
    if (auto param = parameterFromKey(key))
        setValue(*param, stringIdx, value);
}

float NoteDetectionStore::currentValueFromKey(const std::string& key, int stringIdx) const {
    if (auto param = parameterFromKey(key)) {
        if (*param == NoteParameter::FretStabilityFrames) {
            if (stringIdx >= 0 && stringIdx < kNumStrings) {
                return static_cast<float>(m_current.fretStabilityFrames[static_cast<std::size_t>(stringIdx)]);
            }
        } else if (const float* ptr = access(m_current, *param, stringIdx)) {
            return *ptr;
        }
    }
    return 0.f;
}

float NoteDetectionStore::committedValueFromKey(const std::string& key, int stringIdx) const {
    if (auto param = parameterFromKey(key)) {
        if (*param == NoteParameter::FretStabilityFrames) {
            if (stringIdx >= 0 && stringIdx < kNumStrings) {
                return static_cast<float>(m_committed.fretStabilityFrames[static_cast<std::size_t>(stringIdx)]);
            }
        } else if (const float* ptr = access(m_committed, *param, stringIdx)) {
            return *ptr;
        }
    }
    return 0.f;
}

void NoteDetectionStore::undo() {
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_undoStack.empty())
        return;
    m_redoStack.push_back(m_current);
    m_current = m_undoStack.back();
    m_undoStack.pop_back();
    syncActive();
}

void NoteDetectionStore::redo() {
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_redoStack.empty())
        return;
    m_undoStack.push_back(m_current);
    m_current = m_redoStack.back();
    m_redoStack.pop_back();
    syncActive();
}

void NoteDetectionStore::revert() {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_current = m_committed;
    clearHistory();
    syncActive();
}

void NoteDetectionStore::clearHistory() {
    m_undoStack.clear();
    m_redoStack.clear();
}

void NoteDetectionStore::commit() {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_committed = m_current;
    clearHistory();
    syncActive();
}

void NoteDetectionStore::resetToDefaults() {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_current = m_defaults;
    clearHistory();
    syncActive();
}

void NoteDetectionStore::saveState(const std::string& name) {
    std::lock_guard<std::mutex> guard(m_mutex);
    if (name.empty())
        return;
    m_savedStates[name] = m_current;
}

bool NoteDetectionStore::loadState(const std::string& name) {
    std::lock_guard<std::mutex> guard(m_mutex);
    auto it = m_savedStates.find(name);
    if (it == m_savedStates.end())
        return false;
    m_current = it->second;
    clearHistory();
    syncActive();
    return true;
}

std::vector<std::string> NoteDetectionStore::availableStates() const {
    std::lock_guard<std::mutex> guard(m_mutex);
    std::vector<std::string> names;
    names.reserve(m_savedStates.size());
    for (const auto& entry : m_savedStates)
        names.push_back(entry.first);
    return names;
}
std::map<std::string, NoteDetectionParameterSet> NoteDetectionStore::savedStatesSnapshot() const {
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_savedStates;
}

void NoteDetectionStore::replaceSavedStates(const std::map<std::string, NoteDetectionParameterSet>& states) {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_savedStates = states;
}

NoteDetectionParameterSet NoteDetectionStore::snapshotCurrent() const {
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_current;
}

NoteDetectionParameterSet NoteDetectionStore::snapshotCommitted() const {
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_committed;
}

void NoteDetectionStore::applyCommittedSnapshot(const NoteDetectionParameterSet& set) {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_committed = set;
    m_current = set;
    clearHistory();
    syncActive();
}

void NoteDetectionStore::applyCurrentSnapshot(const NoteDetectionParameterSet& set) {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_current = set;
    syncActive();
}

std::optional<NoteParameter> NoteDetectionStore::parameterFromKey(const std::string& key) {
    const auto& descriptors = parameterDescriptors();
    for (const auto& desc : descriptors) {
        if (desc.key == key) {
            return desc.id;
        }
    }
    return std::nullopt;
}

void NoteDetectionStore::pushUndo() {
    m_undoStack.push_back(m_current);
    if (m_undoStack.size() > 32)
        m_undoStack.erase(m_undoStack.begin());
}

void NoteDetectionStore::syncActive() {
    m_active.store(m_current);
    m_activeGeneration.fetch_add(1, std::memory_order_acq_rel);
}

// no-op placeholder
