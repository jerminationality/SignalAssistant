#pragma once
/**
 * ThreadPriority.h - Thread priority and CPU affinity utilities for Pi 5
 * 
 * Implements the 3-tier priority model:
 * - TIER 1: Audio Thread - Core 0, SCHED_FIFO (real-time)
 * - TIER 2: CQT Worker   - Core 1/2, High priority  
 * - TIER 3: UI/Main      - Core 3, Normal priority
 */

#include <thread>
#include <cstdint>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#endif

namespace audio {

enum class ThreadTier {
    Audio,      // Tier 1: Real-time, Core 0
    CQTWorker,  // Tier 2: High priority, Core 1 or 2
    UIMain      // Tier 3: Normal priority, Core 3
};

/**
 * Pin the current thread to a specific CPU core
 * @param core CPU core number (0-3 on Pi 5)
 * @return true if successful
 */
inline bool pinToCore(int core) {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core, &cpuset);
    
    int result = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    return result == 0;
#else
    (void)core;
    return false; // Not supported on non-Linux
#endif
}

/**
 * Set thread to real-time SCHED_FIFO priority
 * @param priority FIFO priority (1-99, higher = more priority)
 * @return true if successful
 */
inline bool setRealtimePriority(int priority = 70) {
#ifdef __linux__
    struct sched_param param;
    param.sched_priority = priority;
    
    int result = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
    return result == 0;
#else
    (void)priority;
    return false;
#endif
}

/**
 * Set thread to high priority (but not real-time)
 * Uses SCHED_RR with moderate priority
 * @return true if successful
 */
inline bool setHighPriority(int priority = 50) {
#ifdef __linux__
    struct sched_param param;
    param.sched_priority = priority;
    
    // Try SCHED_RR first, fall back to SCHED_OTHER with nice
    int result = pthread_setschedparam(pthread_self(), SCHED_RR, &param);
    if (result != 0) {
        // Fallback: just set nice value
        nice(-10);
        return false;
    }
    return true;
#else
    (void)priority;
    return false;
#endif
}

/**
 * Configure thread for its designated tier
 * @param tier The thread tier to configure for
 * @return true if all settings applied successfully
 */
inline bool configureThreadForTier(ThreadTier tier) {
    bool success = true;
    
    switch (tier) {
        case ThreadTier::Audio:
            // TIER 1: Real-time priority, pinned to Core 0
            success &= pinToCore(0);
            success &= setRealtimePriority(70);
            break;
            
        case ThreadTier::CQTWorker:
            // TIER 2: High priority, pinned to Core 1
            success &= pinToCore(1);
            success &= setHighPriority(50);
            break;
            
        case ThreadTier::UIMain:
            // TIER 3: Normal priority, pinned to Core 3
            success &= pinToCore(3);
            // No special priority needed
            break;
    }
    
    return success;
}

/**
 * Get human-readable name for thread tier
 */
inline const char* tierName(ThreadTier tier) {
    switch (tier) {
        case ThreadTier::Audio: return "Audio (Tier 1)";
        case ThreadTier::CQTWorker: return "CQT Worker (Tier 2)";
        case ThreadTier::UIMain: return "UI/Main (Tier 3)";
        default: return "Unknown";
    }
}

} // namespace audio
