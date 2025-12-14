#include "NoteDetectionStore.h"

#include <algorithm>
#include <cstdio>
#include <cstdarg>

namespace {

void logStoreLookup(const char* stage, const std::string& key, int stringIdx, const float* value = nullptr) {
    std::fprintf(stderr,
                 "store %s key=%s string=%d",
                 stage,
                 key.c_str(),
                 stringIdx);
    if (value)
        std::fprintf(stderr, " value=%.6f", static_cast<double>(*value));
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
}

void logStoreDetail(const char* fmt, ...) {
    std::fprintf(stderr, "store ");
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
}

}

void NoteDetectionParameterSetAtomic::store(const NoteDetectionParameterSet& source) {
    const auto transfer = [](auto& destArr, const auto& srcArr) {
        for (std::size_t i = 0; i < destArr.size(); ++i)
            destArr[i].store(srcArr[i], std::memory_order_release);
    };
    transfer(onsetThresholdScale, source.onsetThresholdScale);
    transfer(baselineFloor, source.baselineFloor);
    transfer(envelopeFloor, source.envelopeFloor);
    transfer(gateRatio, source.gateRatio);
    transfer(sustainFloorScale, source.sustainFloorScale);
    transfer(retriggerGateScale, source.retriggerGateScale);
    transfer(peakReleaseRatio, source.peakReleaseRatio);
    transfer(pitchTolerance, source.pitchTolerance);
    transfer(targetRms, source.targetRms);
    transfer(calibrationGainMultiplier, source.calibrationGainMultiplier);
    transfer(lowCutMultiplier, source.lowCutMultiplier);
    transfer(highCutMultiplier, source.highCutMultiplier);
    transfer(aubioThresholdScale, source.aubioThresholdScale);
    transfer(onsetSilenceDb, source.onsetSilenceDb);
    transfer(pitchSilenceDb, source.pitchSilenceDb);
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
    logStoreDetail("access-enter param=%d string=%d", static_cast<int>(id), stringIdx);
    if (stringIdx < 0 || stringIdx >= kNumStrings) {
        logStoreDetail("access-string-out-of-range param=%d string=%d", static_cast<int>(id), stringIdx);
        return nullptr;
    }

    float* result = nullptr;
    switch (id) {
        case NoteParameter::OnsetThresholdScale: result = &set.onsetThresholdScale[static_cast<std::size_t>(stringIdx)]; break;
        case NoteParameter::BaselineFloor: result = &set.baselineFloor[static_cast<std::size_t>(stringIdx)]; break;
        case NoteParameter::EnvelopeFloor: result = &set.envelopeFloor[static_cast<std::size_t>(stringIdx)]; break;
        case NoteParameter::GateRatio: result = &set.gateRatio[static_cast<std::size_t>(stringIdx)]; break;
        case NoteParameter::SustainFloorScale: result = &set.sustainFloorScale[static_cast<std::size_t>(stringIdx)]; break;
        case NoteParameter::RetriggerGateScale: result = &set.retriggerGateScale[static_cast<std::size_t>(stringIdx)]; break;
        case NoteParameter::PeakReleaseRatio: result = &set.peakReleaseRatio[static_cast<std::size_t>(stringIdx)]; break;
        case NoteParameter::PitchTolerance: result = &set.pitchTolerance[static_cast<std::size_t>(stringIdx)]; break;
        case NoteParameter::TargetRms: result = &set.targetRms[static_cast<std::size_t>(stringIdx)]; break;
        case NoteParameter::CalibrationGainMultiplier: result = &set.calibrationGainMultiplier[static_cast<std::size_t>(stringIdx)]; break;
        case NoteParameter::LowCutMultiplier: result = &set.lowCutMultiplier[static_cast<std::size_t>(stringIdx)]; break;
        case NoteParameter::HighCutMultiplier: result = &set.highCutMultiplier[static_cast<std::size_t>(stringIdx)]; break;
        case NoteParameter::AubioThresholdScale: result = &set.aubioThresholdScale[static_cast<std::size_t>(stringIdx)]; break;
        case NoteParameter::OnsetSilenceDb: result = &set.onsetSilenceDb[static_cast<std::size_t>(stringIdx)]; break;
        case NoteParameter::PitchSilenceDb: result = &set.pitchSilenceDb[static_cast<std::size_t>(stringIdx)]; break;
    }

    if (!result)
        logStoreDetail("access-null param=%d string=%d", static_cast<int>(id), stringIdx);
    else
        logStoreDetail("access-success param=%d string=%d value=%.6f", static_cast<int>(id), stringIdx, static_cast<double>(*result));
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
        case NoteParameter::OnsetThresholdScale: return fetch(m_active.onsetThresholdScale);
        case NoteParameter::BaselineFloor: return fetch(m_active.baselineFloor);
        case NoteParameter::EnvelopeFloor: return fetch(m_active.envelopeFloor);
        case NoteParameter::GateRatio: return fetch(m_active.gateRatio);
        case NoteParameter::SustainFloorScale: return fetch(m_active.sustainFloorScale);
        case NoteParameter::RetriggerGateScale: return fetch(m_active.retriggerGateScale);
        case NoteParameter::PeakReleaseRatio: return fetch(m_active.peakReleaseRatio);
        case NoteParameter::PitchTolerance: return fetch(m_active.pitchTolerance);
        case NoteParameter::TargetRms: return fetch(m_active.targetRms);
        case NoteParameter::CalibrationGainMultiplier: return fetch(m_active.calibrationGainMultiplier);
        case NoteParameter::LowCutMultiplier: return fetch(m_active.lowCutMultiplier);
        case NoteParameter::HighCutMultiplier: return fetch(m_active.highCutMultiplier);
        case NoteParameter::AubioThresholdScale: return fetch(m_active.aubioThresholdScale);
        case NoteParameter::OnsetSilenceDb: return fetch(m_active.onsetSilenceDb);
        case NoteParameter::PitchSilenceDb: return fetch(m_active.pitchSilenceDb);
    }
    return 0.f;
}

void NoteDetectionStore::setValue(NoteParameter id, int stringIdx, float value) {
    std::lock_guard<std::mutex> guard(m_mutex);
    logStoreDetail("set-value-enter param=%d string=%d value=%.6f undo=%zu redo=%zu",
                   static_cast<int>(id),
                   stringIdx,
                   static_cast<double>(value),
                   m_undoStack.size(),
                   m_redoStack.size());
    if (m_batchEditDepth > 0) {
        if (!m_batchUndoPushed) {
            pushUndo();
            m_batchUndoPushed = true;
        }
    } else {
        pushUndo();
    }
    if (float* ptr = access(m_current, id, stringIdx)) {
        const double before = static_cast<double>(*ptr);
        *ptr = value;
        logStoreDetail("set-value-write param=%d string=%d before=%.6f after=%.6f",
                       static_cast<int>(id),
                       stringIdx,
                       before,
                       static_cast<double>(value));
    } else {
        logStoreDetail("set-value-null param=%d string=%d", static_cast<int>(id), stringIdx);
    }
    m_redoStack.clear();
    syncActive();
    logStoreDetail("set-value-exit param=%d string=%d undo=%zu redo=%zu",
                   static_cast<int>(id),
                   stringIdx,
                   m_undoStack.size(),
                   m_redoStack.size());
}

void NoteDetectionStore::beginBatchEdit() {
    std::lock_guard<std::mutex> guard(m_mutex);
    ++m_batchEditDepth;
    if (m_batchEditDepth == 1)
        m_batchUndoPushed = false;
    logStoreDetail("batch-begin depth=%d", m_batchEditDepth);
}

void NoteDetectionStore::endBatchEdit() {
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_batchEditDepth <= 0) {
        m_batchEditDepth = 0;
        m_batchUndoPushed = false;
        logStoreDetail("batch-end-underflow");
        return;
    }
    --m_batchEditDepth;
    if (m_batchEditDepth == 0)
        m_batchUndoPushed = false;
    logStoreDetail("batch-end depth=%d", m_batchEditDepth);
}

void NoteDetectionStore::setValueFromKey(const std::string& key, int stringIdx, float value) {
    logStoreDetail("set-value-from-key key=%s string=%d value=%.6f", key.c_str(), stringIdx, static_cast<double>(value));
    if (auto param = parameterFromKey(key))
        setValue(*param, stringIdx, value);
    else
        logStoreDetail("set-value-from-key-miss key=%s", key.c_str());
}

float NoteDetectionStore::currentValueFromKey(const std::string& key, int stringIdx) const {
    logStoreLookup("current-enter", key, stringIdx);
    if (auto param = parameterFromKey(key)) {
        logStoreDetail("current-param-resolved key=%s param=%d", key.c_str(), static_cast<int>(*param));
        if (const float* ptr = access(m_current, *param, stringIdx)) {
            logStoreDetail("current-access-success key=%s string=%d", key.c_str(), stringIdx);
            logStoreLookup("current-exit", key, stringIdx, ptr);
            return *ptr;
        }
        logStoreDetail("current-access-null key=%s string=%d", key.c_str(), stringIdx);
    }
    logStoreLookup("current-miss", key, stringIdx);
    return 0.f;
}

float NoteDetectionStore::committedValueFromKey(const std::string& key, int stringIdx) const {
    logStoreLookup("committed-enter", key, stringIdx);
    if (auto param = parameterFromKey(key)) {
        logStoreDetail("committed-param-resolved key=%s param=%d", key.c_str(), static_cast<int>(*param));
        if (const float* ptr = access(m_committed, *param, stringIdx)) {
            logStoreDetail("committed-access-success key=%s string=%d", key.c_str(), stringIdx);
            logStoreLookup("committed-exit", key, stringIdx, ptr);
            return *ptr;
        }
        logStoreDetail("committed-access-null key=%s string=%d", key.c_str(), stringIdx);
    }
    logStoreLookup("committed-miss", key, stringIdx);
    return 0.f;
}

void NoteDetectionStore::undo() {
    std::lock_guard<std::mutex> guard(m_mutex);
    logStoreDetail("undo-enter stack=%zu", m_undoStack.size());
    if (m_undoStack.empty())
        return;
    m_redoStack.push_back(m_current);
    m_current = m_undoStack.back();
    m_undoStack.pop_back();
    syncActive();
    logStoreDetail("undo-exit stack=%zu redo=%zu", m_undoStack.size(), m_redoStack.size());
}

void NoteDetectionStore::redo() {
    std::lock_guard<std::mutex> guard(m_mutex);
    logStoreDetail("redo-enter stack=%zu", m_redoStack.size());
    if (m_redoStack.empty())
        return;
    m_undoStack.push_back(m_current);
    m_current = m_redoStack.back();
    m_redoStack.pop_back();
    syncActive();
    logStoreDetail("redo-exit stack=%zu undo=%zu", m_redoStack.size(), m_undoStack.size());
}

void NoteDetectionStore::revert() {
    std::lock_guard<std::mutex> guard(m_mutex);
    logStoreDetail("revert-enter undo=%zu redo=%zu", m_undoStack.size(), m_redoStack.size());
    m_current = m_committed;
    clearHistory();
    syncActive();
    logStoreDetail("revert-exit undo=%zu redo=%zu", m_undoStack.size(), m_redoStack.size());
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
    logStoreDetail("param-enter key=%s", key.c_str());
    const auto& descriptors = parameterDescriptors();
    logStoreDetail("param-descriptors-ready size=%zu", descriptors.size());
    std::size_t idx = 0;
    for (const auto& desc : descriptors) {
        logStoreDetail("param-check idx=%zu desc=%s", idx, desc.key.c_str());
        if (desc.key == key) {
            logStoreDetail("param-match idx=%zu", idx);
            return desc.id;
        }
        ++idx;
    }
    logStoreDetail("param-miss key=%s", key.c_str());
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
