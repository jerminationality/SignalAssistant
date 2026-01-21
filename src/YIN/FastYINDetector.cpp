/**
 * FastYINDetector.cpp - Dual-buffer YIN with onset detection implementation
 * 
 * VERSION 3 - Fixes pitch detection failure during attack transients
 * 
 * Key insight: YIN often can't detect pitch during the attack transient
 * because the signal is non-periodic (noise burst). We need to:
 * 1. Trigger ATTACK on strong RMS rise (don't require pitch)
 * 2. Transition to SUSTAIN once pitch stabilizes
 * 3. Keep the note alive as long as RMS is above threshold
 */

#include "FastYINDetector.h"
#include <cstring>
#include <algorithm>
#include <cmath>

// Debug logging - set to 1 to enable state machine logging
#define FAST_YIN_STATE_DEBUG 1

#if FAST_YIN_STATE_DEBUG
#include "../SessionLogger.h"
#endif

namespace audio {

FastYINDetector::FastYINDetector(int sampleRate, int onsetBufferSize, 
                                   int pitchBufferSize, int stringIdx)
    : m_sampleRate(sampleRate)
    , m_onsetBufferSize(onsetBufferSize)
    , m_pitchBufferSize(std::min(pitchBufferSize, kMaxPitchBuffer))
    , m_stringIdx(stringIdx)
    , m_onsetYIN(sampleRate, onsetBufferSize, 0.15f)
    , m_pitchYIN(sampleRate, pitchBufferSize, 0.10f)
{
    m_ringBuffer.fill(0.0f);
    
    float minFreq = getStringMinFrequency(stringIdx);
    float maxFreq = getStringMaxFrequency(stringIdx);
    
    m_onsetYIN.setMinFrequency(minFreq);
    m_onsetYIN.setMaxFrequency(maxFreq);
    m_pitchYIN.setMinFrequency(minFreq);
    m_pitchYIN.setMaxFrequency(maxFreq);
    
    m_lastPitch = {0.0f, 0.0f, false, 0.0f};
}

void FastYINDetector::reset() {
    m_ringBuffer.fill(0.0f);
    m_writePos = 0;
    m_samplesAccumulated = 0;
    m_state = NoteState::IDLE;
    m_peakRMS = 0.0f;
    m_lastRMS = 0.0f;
    m_lastOnsetThreshold = 0.0f;
    m_currentFret = -1;
    m_candidateFret = -1;
    m_fretStabilityCount = 0;
    m_sustainFrameCount = 0;
    m_attackFrameCount = 0;
    m_lastPitch = {0.0f, 0.0f, false, 0.0f};
}

void FastYINDetector::setSampleRate(int sampleRate) {
    m_sampleRate = sampleRate;
    m_onsetYIN.setSampleRate(sampleRate);
    m_pitchYIN.setSampleRate(sampleRate);
}

float FastYINDetector::computeRMS(const float* buffer, int numSamples) {
    if (numSamples <= 0) return 0.0f;
    
    float sumSquares = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        sumSquares += buffer[i] * buffer[i];
    }
    
    return std::sqrt(sumSquares / static_cast<float>(numSamples));
}

FastYINResult FastYINDetector::process(const float* newSamples, int numSamples,
                                        const FastYINParams& params) {
    FastYINResult result;
    result.state = m_state;
    result.isOnset = false;
    result.isSustaining = false;
    result.currentRMS = 0.0f;
    result.peakRMS = m_peakRMS;
    result.onsetThreshold = 0.0f;
    result.detectedFret = -1;
    result.stableFret = m_currentFret;
    result.pitchResult = {0.0f, 0.0f, false, 0.0f};
    
    if (!newSamples || numSamples <= 0) {
        return result;
    }
    
    m_onsetYIN.setThreshold(params.yinThreshold);
    m_pitchYIN.setThreshold(params.yinThreshold);
    
    // Add new samples to ring buffer
    for (int i = 0; i < numSamples; ++i) {
        m_ringBuffer[m_writePos] = newSamples[i];
        m_writePos = (m_writePos + 1) % m_pitchBufferSize;
        m_samplesAccumulated = std::min(m_samplesAccumulated + 1, m_pitchBufferSize);
    }
    
    if (m_samplesAccumulated < m_onsetBufferSize) {
        return result;
    }
    
    // Extract linear buffer for analysis
    std::array<float, kMaxPitchBuffer> linearBuffer;
    const int pitchSamples = std::min(m_samplesAccumulated, m_pitchBufferSize);
    
    int readPos = (m_writePos - pitchSamples + m_pitchBufferSize) % m_pitchBufferSize;
    for (int i = 0; i < pitchSamples; ++i) {
        linearBuffer[i] = m_ringBuffer[(readPos + i) % m_pitchBufferSize];
    }
    
    // Compute RMS on onset window
    const int onsetStart = std::max(0, pitchSamples - m_onsetBufferSize);
    const float currentRMS = computeRMS(linearBuffer.data() + onsetStart, m_onsetBufferSize);
    result.currentRMS = currentRMS;
    
    // Run pitch detection
    YINResult pitchResult;
    if (pitchSamples >= m_pitchBufferSize) {
        pitchResult = m_pitchYIN.detect(linearBuffer.data(), pitchSamples);
    } else if (pitchSamples >= m_onsetBufferSize) {
        pitchResult = m_onsetYIN.detect(linearBuffer.data() + onsetStart, m_onsetBufferSize);
    } else {
        pitchResult = {0.0f, 0.0f, false, 0.0f};
    }
    
    result.pitchResult = pitchResult;
    m_lastPitch = pitchResult;
    
    // Calculate fret from frequency
    int detectedFret = -1;
    if (pitchResult.isPitched && pitchResult.frequency > 0.0f) {
        detectedFret = frequencyToFret(pitchResult.frequency, m_stringIdx);
    }
    result.detectedFret = detectedFret;
    
    // =========================================================================
    // STATE MACHINE v3 - Onset on RMS, pitch confirms later
    // =========================================================================
    updateStateMachine(currentRMS, pitchResult, params);
    
    result.state = m_state;
    result.peakRMS = m_peakRMS;
    result.onsetThreshold = m_lastOnsetThreshold;
    
    // Set output flags
    result.isOnset = (m_state == NoteState::ATTACK);
    result.isSustaining = (m_state == NoteState::SUSTAIN || m_state == NoteState::ATTACK);
    
    // Fret stability
    result.stableFret = updateFretStability(detectedFret, params);
    
    m_lastRMS = currentRMS;
    
    return result;
}

void FastYINDetector::updateStateMachine(float rms, const YINResult& pitch,
                                          const FastYINParams& params) {
    /**
     * State Machine v3:
     * 
     * Key insight: During attack transients, YIN often fails because the signal
     * is non-periodic (noise burst). We detect onset based on RMS, then wait
     * for pitch to stabilize.
     * 
     * IDLE → ATTACK:
     *   - Strong RMS rise (don't require pitch!)
     *   - OR: Signal above threshold with valid pitch
     * 
     * ATTACK → SUSTAIN:
     *   - Valid pitch detected, OR
     *   - Stayed in ATTACK for enough frames (pitch may never come for percussive sounds)
     * 
     * SUSTAIN → RELEASE:
     *   - RMS drops below release threshold
     * 
     * RELEASE → IDLE:
     *   - RMS drops below noise gate
     */
    
    const float noiseGate = params.noiseGateRMS;
    const float onsetMultiplier = params.onsetSensitivity;
    const float releaseRatio = params.releaseRatio;
    const float yinThreshold = params.yinThreshold;
    
    // Max frames to wait in ATTACK for pitch (at ~86fps, 10 frames ≈ 116ms)
    constexpr int kMaxAttackFrames = 10;
    // Min frames in SUSTAIN before release
    constexpr int kMinSustainFrames = 4;
    
    switch (m_state) {
        case NoteState::IDLE: {
            // =========================================================
            // IDLE → ATTACK: Trigger on RMS rise (pitch optional)
            // =========================================================
            
            bool shouldAttack = false;
            
            // Method 1: Strong RMS rise from low level
            if (m_lastRMS < noiseGate && rms > noiseGate * 2.0f) {
                shouldAttack = true;
#if FAST_YIN_STATE_DEBUG
                SessionLogger::instance().logf("yin-fsm",
                    "S%d: IDLE→ATTACK (from silence) rms=%.4f > gate*2=%.4f",
                    m_stringIdx, rms, noiseGate * 2.0f);
#endif
            }
            // Method 2: Significant RMS jump
            else if (m_lastRMS > 0.001f && rms > m_lastRMS * onsetMultiplier && rms > noiseGate) {
                shouldAttack = true;
#if FAST_YIN_STATE_DEBUG
                SessionLogger::instance().logf("yin-fsm",
                    "S%d: IDLE→ATTACK (rise) rms=%.4f > lastRms*%.1f=%.4f",
                    m_stringIdx, rms, onsetMultiplier, m_lastRMS * onsetMultiplier);
#endif
            }
            // Method 3: Signal with valid pitch (even without big rise)
            else if (rms > noiseGate && pitch.isPitched && pitch.confidence >= yinThreshold) {
                shouldAttack = true;
#if FAST_YIN_STATE_DEBUG
                SessionLogger::instance().logf("yin-fsm",
                    "S%d: IDLE→ATTACK (with pitch) rms=%.4f pitch=%.1fHz conf=%.2f",
                    m_stringIdx, rms, pitch.frequency, pitch.confidence);
#endif
            }
            
            if (shouldAttack) {
                m_state = NoteState::ATTACK;
                m_peakRMS = rms;
                m_lastOnsetThreshold = std::max(m_lastRMS * onsetMultiplier, noiseGate);
                m_attackFrameCount = 0;
            }
#if FAST_YIN_STATE_DEBUG
            else if (rms > noiseGate * 0.5f) {
                // Log why we didn't attack
                SessionLogger::instance().logf("yin-fsm",
                    "S%d: IDLE (no onset) rms=%.4f lastRms=%.4f gate=%.4f pitch=%d(%.1fHz)",
                    m_stringIdx, rms, m_lastRMS, noiseGate, 
                    pitch.isPitched ? 1 : 0, pitch.frequency);
            }
#endif
            break;
        }
            
        case NoteState::ATTACK: {
            // =========================================================
            // ATTACK: Wait for pitch or timeout, then → SUSTAIN
            // =========================================================
            
            m_attackFrameCount++;
            
            if (rms > m_peakRMS) {
                m_peakRMS = rms;
            }
            
            // Transition to SUSTAIN if:
            // 1. We got valid pitch, OR
            // 2. We've waited long enough (pitch may not come for percussive attack)
            bool gotPitch = pitch.isPitched && pitch.confidence >= yinThreshold;
            bool timeout = m_attackFrameCount >= kMaxAttackFrames;
            
            if (gotPitch || timeout) {
#if FAST_YIN_STATE_DEBUG
                SessionLogger::instance().logf("yin-fsm",
                    "S%d: ATTACK→SUSTAIN frames=%d pitch=%d(%.1fHz) peak=%.4f",
                    m_stringIdx, m_attackFrameCount, gotPitch ? 1 : 0, 
                    pitch.frequency, m_peakRMS);
#endif
                m_state = NoteState::SUSTAIN;
                m_sustainFrameCount = 0;
            }
            
            // But if RMS drops below noise gate, abort to IDLE
            if (rms < noiseGate) {
#if FAST_YIN_STATE_DEBUG
                SessionLogger::instance().logf("yin-fsm",
                    "S%d: ATTACK→IDLE (signal lost) rms=%.4f < gate=%.4f",
                    m_stringIdx, rms, noiseGate);
#endif
                m_state = NoteState::IDLE;
                m_peakRMS = 0.0f;
                m_currentFret = -1;
            }
            break;
        }
            
        case NoteState::SUSTAIN: {
            // =========================================================
            // SUSTAIN: Track peak, check for release or re-attack
            // =========================================================
            
            m_sustainFrameCount++;
            
            if (rms > m_peakRMS) {
                m_peakRMS = rms;
            }
            
            const float releaseThreshold = m_peakRMS * releaseRatio;
            const float reattackThreshold = m_peakRMS * onsetMultiplier;
            
            // Check for re-attack
            if (rms > reattackThreshold) {
#if FAST_YIN_STATE_DEBUG
                SessionLogger::instance().logf("yin-fsm",
                    "S%d: SUSTAIN→ATTACK (re-attack) rms=%.4f > thresh=%.4f",
                    m_stringIdx, rms, reattackThreshold);
#endif
                m_state = NoteState::ATTACK;
                m_lastOnsetThreshold = reattackThreshold;
                m_peakRMS = rms;
                m_attackFrameCount = 0;
            }
            // Check for release (with minimum sustain time)
            else if (rms < releaseThreshold && m_sustainFrameCount >= kMinSustainFrames) {
#if FAST_YIN_STATE_DEBUG
                SessionLogger::instance().logf("yin-fsm",
                    "S%d: SUSTAIN→RELEASE rms=%.4f < thresh=%.4f (peak=%.4f)",
                    m_stringIdx, rms, releaseThreshold, m_peakRMS);
#endif
                m_state = NoteState::RELEASE;
            }
            // Also release if signal drops to noise floor
            else if (rms < noiseGate) {
#if FAST_YIN_STATE_DEBUG
                SessionLogger::instance().logf("yin-fsm",
                    "S%d: SUSTAIN→RELEASE (below gate) rms=%.4f < gate=%.4f",
                    m_stringIdx, rms, noiseGate);
#endif
                m_state = NoteState::RELEASE;
            }
#if FAST_YIN_STATE_DEBUG
            // Periodic logging
            else if (m_sustainFrameCount % 20 == 1) {
                SessionLogger::instance().logf("yin-fsm",
                    "S%d: SUSTAIN rms=%.4f peak=%.4f relThresh=%.4f frames=%d fret=%d",
                    m_stringIdx, rms, m_peakRMS, releaseThreshold, m_sustainFrameCount,
                    frequencyToFret(pitch.frequency, m_stringIdx));
            }
#endif
            break;
        }
            
        case NoteState::RELEASE: {
            // =========================================================
            // RELEASE: Wait for silence or re-attack
            // =========================================================
            
            if (rms < noiseGate) {
#if FAST_YIN_STATE_DEBUG
                SessionLogger::instance().logf("yin-fsm",
                    "S%d: RELEASE→IDLE rms=%.4f < gate=%.4f",
                    m_stringIdx, rms, noiseGate);
#endif
                m_state = NoteState::IDLE;
                m_peakRMS = 0.0f;
                m_currentFret = -1;
                m_candidateFret = -1;
                m_fretStabilityCount = 0;
            }
            // Re-attack from decay
            else if (rms > m_peakRMS * onsetMultiplier) {
#if FAST_YIN_STATE_DEBUG
                SessionLogger::instance().logf("yin-fsm",
                    "S%d: RELEASE→ATTACK (re-attack) rms=%.4f",
                    m_stringIdx, rms);
#endif
                m_state = NoteState::ATTACK;
                m_lastOnsetThreshold = m_peakRMS * onsetMultiplier;
                m_peakRMS = rms;
                m_attackFrameCount = 0;
            }
            break;
        }
    }
}

int FastYINDetector::updateFretStability(int detectedFret, const FastYINParams& params) {
    if (detectedFret < 0) {
        if (m_state == NoteState::IDLE) {
            m_currentFret = -1;
            m_candidateFret = -1;
            m_fretStabilityCount = 0;
        }
        return m_currentFret;
    }
    
    if (detectedFret == m_currentFret) {
        m_candidateFret = -1;
        m_fretStabilityCount = 0;
        return m_currentFret;
    }
    
    if (detectedFret == m_candidateFret) {
        m_fretStabilityCount++;
        if (m_fretStabilityCount >= params.fretStabilityFrames) {
#if FAST_YIN_STATE_DEBUG
            SessionLogger::instance().logf("yin-fret",
                "S%d: Fret change %d → %d (stable for %d frames)",
                m_stringIdx, m_currentFret, m_candidateFret, m_fretStabilityCount);
#endif
            m_currentFret = m_candidateFret;
            m_candidateFret = -1;
            m_fretStabilityCount = 0;
        }
    } else {
        m_candidateFret = detectedFret;
        m_fretStabilityCount = 1;
        
        if (m_currentFret < 0) {
#if FAST_YIN_STATE_DEBUG
            SessionLogger::instance().logf("yin-fret",
                "S%d: First note fret=%d (immediate)",
                m_stringIdx, detectedFret);
#endif
            m_currentFret = detectedFret;
            m_candidateFret = -1;
            m_fretStabilityCount = 0;
        }
    }
    
    return m_currentFret;
}

} // namespace audio
