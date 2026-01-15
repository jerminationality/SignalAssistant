#pragma once
/**
 * FretboardProvider.h - TIER 3 UI Bridge for Fretboard Heatmap
 * 
 * Bridges Tier 2 (CQT Worker) to Tier 3 (QML) using a 60Hz heartbeat poll.
 * Prevents UI thread saturation by:
 *   1. Pulling bin magnitudes from AtomicNoteState (lock-free atomics)
 *   2. Only emitting signals when magnitude changes exceed delta threshold
 *   3. Using revision counter for efficient QML binding updates
 * 
 * The CQT engine runs at ~86fps; this provider polls at 60fps to match
 * typical display refresh rates. The "pull" model ensures the audio thread
 * never blocks on UI operations.
 * 
 * Data Layout:
 *   - 6 strings × 25 frets = 150 total bins
 *   - All magnitudes are float (32-bit) for ARM NEON SIMD compatibility
 */

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <array>
#include <atomic>

namespace audio {
    class AtomicNoteState;  // Forward declaration
}

class FretboardProvider : public QObject {
    Q_OBJECT
    
    // Revision counter for efficient QML binding (bumps on any change)
    Q_PROPERTY(int revision READ revision NOTIFY magnitudesChanged)
    
    // Full 6×25 grid as flat list for bulk QML access (row-major: string 0 frets 0-24, string 1 frets 0-24, ...)
    Q_PROPERTY(QVariantList magnitudes READ magnitudes NOTIFY magnitudesChanged)
    
public:
    static constexpr int NUM_STRINGS = 6;
    static constexpr int NUM_FRETS = 25;
    static constexpr int TOTAL_BINS = NUM_STRINGS * NUM_FRETS;  // 150
    
    // Delta threshold for signal emission (prevents signal spam)
    static constexpr float DELTA_THRESHOLD = 0.005f;
    
    // Poll interval in milliseconds (~60Hz)
    static constexpr int POLL_INTERVAL_MS = 16;
    
    /**
     * Construct with reference to shared atomic note state
     * @param noteState Shared atomic state written by CQT Worker (TIER 2)
     * @param parent    QObject parent for ownership
     */
    explicit FretboardProvider(audio::AtomicNoteState& noteState, QObject* parent = nullptr);
    ~FretboardProvider() override;
    
    // Non-copyable
    FretboardProvider(const FretboardProvider&) = delete;
    FretboardProvider& operator=(const FretboardProvider&) = delete;
    
    /**
     * Start the 60Hz polling timer
     */
    void start();
    
    /**
     * Stop the polling timer
     */
    void stop();
    
    /**
     * Check if polling is active
     */
    bool isRunning() const { return m_timer && m_timer->isActive(); }
    
    /**
     * Get current revision counter
     */
    int revision() const { return m_revision; }
    
    /**
     * Get all 150 magnitudes as a flat QVariantList (row-major order)
     * For QML GridView/Repeater binding
     */
    QVariantList magnitudes() const;
    
    /**
     * O(1) single-bin lookup for QML colorProvider callbacks
     * @param stringIndex 0-5 (Low E to High E)
     * @param fretIndex   0-24
     * @return Magnitude value (0.0 - 1.0 normalized)
     */
    Q_INVOKABLE qreal getMagnitude(int stringIndex, int fretIndex) const;
    
    /**
     * Get the detected fret for a string (-1 if no note)
     * @param stringIndex 0-5
     */
    Q_INVOKABLE int getActiveFret(int stringIndex) const;
    
    /**
     * Check if a string has an active attack (new note onset)
     * @param stringIndex 0-5
     */
    Q_INVOKABLE bool isAttack(int stringIndex) const;
    
    /**
     * Check if a string has a sustaining note
     * @param stringIndex 0-5
     */
    Q_INVOKABLE bool isSustaining(int stringIndex) const;

signals:
    /**
     * Emitted when any magnitude changes by more than DELTA_THRESHOLD
     * QML bindings to 'revision' or 'magnitudes' will auto-update
     */
    void magnitudesChanged();
    
    /**
     * Emitted when a specific bin changes significantly
     * @param stringIndex 0-5
     * @param fretIndex   0-24
     * @param magnitude   New magnitude value
     */
    void binChanged(int stringIndex, int fretIndex, qreal magnitude);
    
    /**
     * Emitted on note attack (new note detected)
     */
    void noteAttack(int stringIndex, int fretIndex, qreal energy);
    
    /**
     * Emitted when note ends
     */
    void noteRelease(int stringIndex, int fretIndex);

private slots:
    /**
     * Called by QTimer at 60Hz to poll atomic state
     */
    void pollAtomicState();

private:
    // Reference to shared atomic state (written by TIER 2, read by TIER 3)
    audio::AtomicNoteState& m_noteState;
    
    // 60Hz polling timer
    QTimer* m_timer{nullptr};
    
    // Cached magnitudes for delta detection (previous frame)
    std::array<std::array<float, NUM_FRETS>, NUM_STRINGS> m_prevMagnitudes{};
    
    // Current magnitudes (updated each poll)
    std::array<std::array<float, NUM_FRETS>, NUM_STRINGS> m_currMagnitudes{};
    
    // Cached active fret per string (for attack/release detection)
    std::array<int, NUM_STRINGS> m_prevActiveFret{};
    std::array<int, NUM_STRINGS> m_currActiveFret{};
    
    // Attack/sustain state per string
    std::array<bool, NUM_STRINGS> m_isAttack{};
    std::array<bool, NUM_STRINGS> m_isSustaining{};
    
    // Revision counter (incremented on any change)
    int m_revision{0};
    
    // Frame counter from CQT worker (to detect new frames)
    std::uint64_t m_lastFrameCounter{0};
};
