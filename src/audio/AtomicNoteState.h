#pragma once
/**
 * AtomicNoteState.h - Lock-free note state for Tier 2 -> Tier 3 communication
 * 
 * The CQT Worker (Tier 2) writes detected notes here.
 * The UI Thread (Tier 3) reads the latest state for display.
 * 
 * Uses atomics to ensure:
 * - No locks (UI never blocks on CQT)
 * - UI always sees a consistent snapshot
 * - CQT can update at ~86fps, UI polls at 60fps
 */

#include <atomic>
#include <array>
#include <cstdint>

namespace audio {

/**
 * Per-string note state (atomically updated)
 */
struct StringNoteState {
    std::atomic<int> fret{-1};           // Current detected fret (-1 = none)
    std::atomic<float> energy{0.0f};     // Signal energy (RMS-equivalent)
    std::atomic<bool> isAttack{false};   // True on new note onset
    std::atomic<bool> isSustaining{false}; // True while note is held
    std::atomic<float> pitchHz{0.0f};    // Detected pitch frequency
    std::atomic<float> onsetThreshold{0.0f}; // Adaptive threshold that triggered onset
    std::atomic<uint32_t> onsetCounter{0}; // Increments on each new onset (never misses one)
    
    // Per-fret bin magnitudes for heatmap overlay (frets 0-24)
    static constexpr int NUM_FRETS = 25;
    std::array<std::atomic<float>, NUM_FRETS> binMagnitudes{};
    
    // Copy current state (for UI snapshot)
    void copyTo(int& outFret, float& outEnergy, bool& outAttack, bool& outSustain, 
                float& outPitch, float& outThreshold) const {
        outFret = fret.load(std::memory_order_acquire);
        outEnergy = energy.load(std::memory_order_relaxed);
        outAttack = isAttack.load(std::memory_order_relaxed);
        outSustain = isSustaining.load(std::memory_order_relaxed);
        outPitch = pitchHz.load(std::memory_order_relaxed);
        outThreshold = onsetThreshold.load(std::memory_order_relaxed);
    }
    
    // Update from CQT worker
    void update(int newFret, float newEnergy, bool attack, bool sustain, 
                float pitch, float threshold) {
        fret.store(newFret, std::memory_order_relaxed);
        energy.store(newEnergy, std::memory_order_relaxed);
        isAttack.store(attack, std::memory_order_relaxed);
        isSustaining.store(sustain, std::memory_order_relaxed);
        pitchHz.store(pitch, std::memory_order_relaxed);
        onsetThreshold.store(threshold, std::memory_order_release);
    }
    
    // Signal a new onset (increments counter so readers never miss it)
    void signalOnset() {
        onsetCounter.fetch_add(1, std::memory_order_release);
    }
    
    // Get current onset counter value
    uint32_t getOnsetCounter() const {
        return onsetCounter.load(std::memory_order_acquire);
    }
    
    // Update bin magnitude for a specific fret (called from CQT worker)
    void setBinMagnitude(int fretIdx, float magnitude) {
        if (fretIdx >= 0 && fretIdx < NUM_FRETS) {
            binMagnitudes[fretIdx].store(magnitude, std::memory_order_relaxed);
        }
    }
    
    // Read bin magnitude for a specific fret (called from UI thread)
    float getBinMagnitude(int fretIdx) const {
        if (fretIdx >= 0 && fretIdx < NUM_FRETS) {
            return binMagnitudes[fretIdx].load(std::memory_order_relaxed);
        }
        return 0.0f;
    }
    
    void clear() {
        fret.store(-1, std::memory_order_relaxed);
        energy.store(0.0f, std::memory_order_relaxed);
        isAttack.store(false, std::memory_order_relaxed);
        isSustaining.store(false, std::memory_order_relaxed);
        pitchHz.store(0.0f, std::memory_order_relaxed);
        onsetThreshold.store(0.0f, std::memory_order_relaxed);
        // Don't reset onsetCounter - it's monotonic
        for (auto& mag : binMagnitudes) {
            mag.store(0.0f, std::memory_order_relaxed);
        }
    }
};

/**
 * Global note state for all 6 strings
 * 
 * Usage:
 * - CQT Worker calls updateString() after processing each frame
 * - UI Thread calls snapshot() to get latest state for rendering
 */
class AtomicNoteState {
public:
    AtomicNoteState() {
        m_frameCounter.store(0);
        m_sampleRate.store(44100.0f);
    }
    
    /**
     * Update state for a single string (TIER 2 - CQT Worker)
     */
    void updateString(int stringIdx, int fret, float magnitude, 
                      bool isAttack, bool isSustaining, float pitchHz,
                      float onsetThreshold = 0.0f) {
        if (stringIdx < 0 || stringIdx >= 6) return;
        m_strings[stringIdx].update(fret, magnitude, isAttack, isSustaining, pitchHz, onsetThreshold);
    }
    
    /**
     * Increment frame counter after processing a complete CQT frame (TIER 2)
     */
    void advanceFrame() {
        m_frameCounter.fetch_add(1, std::memory_order_release);
    }
    
    /**
     * Set current sample rate (called when audio starts)
     */
    void setSampleRate(float sr) {
        m_sampleRate.store(sr, std::memory_order_release);
    }
    
    /**
     * Get current frame counter (TIER 3 - UI can check for updates)
     */
    std::uint64_t frameCounter() const {
        return m_frameCounter.load(std::memory_order_acquire);
    }
    
    /**
     * Get current sample rate
     */
    float sampleRate() const {
        return m_sampleRate.load(std::memory_order_acquire);
    }
    
    /**
     * Read state for a single string (TIER 3 - UI Thread)
     */
    void readString(int stringIdx, int& outFret, float& outMag, 
                    bool& outAttack, bool& outSustain, float& outPitch,
                    float& outThreshold) const {
        if (stringIdx < 0 || stringIdx >= 6) {
            outFret = -1;
            outMag = 0.0f;
            outAttack = false;
            outSustain = false;
            outPitch = 0.0f;
            outThreshold = 0.0f;
            return;
        }
        m_strings[stringIdx].copyTo(outFret, outMag, outAttack, outSustain, outPitch, outThreshold);
    }
    
    /**
     * Get onset counter for a string (for detecting missed onsets)
     */
    uint32_t getOnsetCounter(int stringIdx) const {
        if (stringIdx < 0 || stringIdx >= 6) return 0;
        return m_strings[stringIdx].getOnsetCounter();
    }
    
    /**
     * Signal onset on a string (called by YIN worker when entering ATTACK)
     */
    void signalOnset(int stringIdx) {
        if (stringIdx >= 0 && stringIdx < 6) {
            m_strings[stringIdx].signalOnset();
        }
    }
    
    /**
     * Read fret for a single string (simple accessor for UI)
     */
    int fret(int stringIdx) const {
        if (stringIdx < 0 || stringIdx >= 6) return -1;
        return m_strings[stringIdx].fret.load(std::memory_order_acquire);
    }
    
    /**
     * Read energy for a single string (simple accessor for UI)
     */
    float energy(int stringIdx) const {
        if (stringIdx < 0 || stringIdx >= 6) return 0.0f;
        return m_strings[stringIdx].energy.load(std::memory_order_acquire);
    }
    
    /**
     * Read onset threshold for a single string (simple accessor for UI)
     */
    float onsetThreshold(int stringIdx) const {
        if (stringIdx < 0 || stringIdx >= 6) return 0.0f;
        return m_strings[stringIdx].onsetThreshold.load(std::memory_order_acquire);
    }
    
    /**
     * Read bin magnitude for a specific string and fret (TIER 3 - UI Thread)
     * @param stringIdx 0-5 (Low E to High E)
     * @param fretIdx 0-24 (open to 24th fret)
     * @return Bin magnitude value (0.0 - 1.0+ range, RMS-equivalent)
     */
    float binMagnitude(int stringIdx, int fretIdx) const {
        if (stringIdx < 0 || stringIdx >= 6) return 0.0f;
        return m_strings[stringIdx].getBinMagnitude(fretIdx);
    }
    
    /**
     * Alias for binMagnitude (FretboardProvider compatibility)
     */
    float getBinMagnitude(int stringIdx, int fretIdx) const {
        return binMagnitude(stringIdx, fretIdx);
    }
    
    /**
     * Update bin magnitude for a specific string/fret (TIER 2 - CQT Worker)
     */
    void setBinMagnitude(int stringIdx, int fretIdx, float magnitude) {
        if (stringIdx >= 0 && stringIdx < 6) {
            m_strings[stringIdx].setBinMagnitude(fretIdx, magnitude);
        }
    }
    
    /**
     * Clear all string states
     */
    void clearAll() {
        for (auto& s : m_strings) {
            s.clear();
        }
        m_frameCounter.store(0, std::memory_order_release);
    }
    
    /**
     * Direct access to string state (for bulk operations)
     */
    const StringNoteState& string(int idx) const { return m_strings[idx]; }
    StringNoteState& string(int idx) { return m_strings[idx]; }
    
private:
    std::array<StringNoteState, 6> m_strings;
    std::atomic<std::uint64_t> m_frameCounter;
    std::atomic<float> m_sampleRate;
};

} // namespace audio
