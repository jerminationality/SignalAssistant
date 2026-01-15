/**
 * FretboardProvider.cpp - TIER 3 UI Bridge Implementation
 * 
 * Implements 60Hz polling of AtomicNoteState with delta-based signal emission.
 * All magnitude math uses float (32-bit) for ARM NEON SIMD compatibility.
 */

#include "FretboardProvider.h"
#include "audio/AtomicNoteState.h"

#include <cmath>
#include <algorithm>

FretboardProvider::FretboardProvider(audio::AtomicNoteState& noteState, QObject* parent)
    : QObject(parent)
    , m_noteState(noteState)
{
    // Initialize arrays to zero/default
    for (auto& row : m_prevMagnitudes) row.fill(0.0f);
    for (auto& row : m_currMagnitudes) row.fill(0.0f);
    m_prevActiveFret.fill(-1);
    m_currActiveFret.fill(-1);
    m_isAttack.fill(false);
    m_isSustaining.fill(false);
    
    // Create polling timer (will be started by start())
    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);  // Request high-precision timing
    connect(m_timer, &QTimer::timeout, this, &FretboardProvider::pollAtomicState);
}

FretboardProvider::~FretboardProvider() {
    stop();
}

void FretboardProvider::start() {
    if (m_timer && !m_timer->isActive()) {
        m_timer->start(POLL_INTERVAL_MS);  // 16ms ≈ 60Hz
    }
}

void FretboardProvider::stop() {
    if (m_timer && m_timer->isActive()) {
        m_timer->stop();
    }
}

QVariantList FretboardProvider::magnitudes() const {
    QVariantList result;
    result.reserve(TOTAL_BINS);
    
    // Row-major order: string 0 frets 0-24, string 1 frets 0-24, ...
    for (int s = 0; s < NUM_STRINGS; ++s) {
        for (int f = 0; f < NUM_FRETS; ++f) {
            result.append(static_cast<qreal>(m_currMagnitudes[s][f]));
        }
    }
    
    return result;
}

qreal FretboardProvider::getMagnitude(int stringIndex, int fretIndex) const {
    // O(1) lookup - bounds check for safety
    if (stringIndex < 0 || stringIndex >= NUM_STRINGS ||
        fretIndex < 0 || fretIndex >= NUM_FRETS) {
        return 0.0;
    }
    return static_cast<qreal>(m_currMagnitudes[stringIndex][fretIndex]);
}

int FretboardProvider::getActiveFret(int stringIndex) const {
    if (stringIndex < 0 || stringIndex >= NUM_STRINGS) {
        return -1;
    }
    return m_currActiveFret[stringIndex];
}

bool FretboardProvider::isAttack(int stringIndex) const {
    if (stringIndex < 0 || stringIndex >= NUM_STRINGS) {
        return false;
    }
    return m_isAttack[stringIndex];
}

bool FretboardProvider::isSustaining(int stringIndex) const {
    if (stringIndex < 0 || stringIndex >= NUM_STRINGS) {
        return false;
    }
    return m_isSustaining[stringIndex];
}

void FretboardProvider::pollAtomicState() {
    // Check if CQT worker has produced new frames
    const std::uint64_t frameCounter = m_noteState.frameCounter();
    if (frameCounter == m_lastFrameCounter) {
        // No new data - skip this poll cycle
        return;
    }
    m_lastFrameCounter = frameCounter;
    
    bool anyChanged = false;
    
    // Read all 6 strings × 25 frets from atomic state
    for (int s = 0; s < NUM_STRINGS; ++s) {
        // Read per-string note state
        int fret = -1;
        float energy = 0.0f;
        bool attack = false;
        bool sustaining = false;
        float pitchHz = 0.0f;
        
        m_noteState.readString(s, fret, energy, attack, sustaining, pitchHz);
        
        // Store active fret and state
        m_currActiveFret[s] = fret;
        m_isAttack[s] = attack;
        m_isSustaining[s] = sustaining;
        
        // Check for note attack/release transitions
        if (fret >= 0 && m_prevActiveFret[s] < 0) {
            // New note attack
            emit noteAttack(s, fret, static_cast<qreal>(energy));
        } else if (fret < 0 && m_prevActiveFret[s] >= 0) {
            // Note released
            emit noteRelease(s, m_prevActiveFret[s]);
        }
        m_prevActiveFret[s] = fret;
        
        // Read all 25 fret bin magnitudes for this string
        for (int f = 0; f < NUM_FRETS; ++f) {
            const float mag = m_noteState.getBinMagnitude(s, f);
            m_currMagnitudes[s][f] = mag;
            
            // Delta detection: only emit if change exceeds threshold
            const float delta = std::fabsf(mag - m_prevMagnitudes[s][f]);
            if (delta > DELTA_THRESHOLD) {
                anyChanged = true;
                emit binChanged(s, f, static_cast<qreal>(mag));
            }
        }
        
        // Update previous state for next frame's delta detection
        m_prevMagnitudes[s] = m_currMagnitudes[s];
    }
    
    // Emit bulk change signal if anything changed (for QML revision binding)
    if (anyChanged) {
        ++m_revision;
        emit magnitudesChanged();
    }
}
