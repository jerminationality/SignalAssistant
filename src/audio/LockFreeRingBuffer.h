#pragma once
/**
 * LockFreeRingBuffer.h - SPSC (Single Producer, Single Consumer) lock-free ring buffer
 * 
 * TIER 1 (Audio Thread) -> TIER 2 (CQT Worker Thread) communication
 * 
 * Design:
 * - Zero allocation in push/pop paths
 * - Cache-line padding to prevent false sharing
 * - Atomic indices with acquire/release semantics
 * - Fixed capacity (must be power of 2)
 */

#include <atomic>
#include <array>
#include <cstddef>
#include <cstring>

namespace audio {

// Cache line size for padding (typical ARM64)
constexpr std::size_t kCacheLineSize = 64;

template<typename T, std::size_t Capacity>
class LockFreeRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    
public:
    LockFreeRingBuffer() : m_writeIndex(0), m_readIndex(0) {
        m_buffer.fill(T{});
    }
    
    /**
     * Push a single element (PRODUCER ONLY - Audio Thread)
     * @return true if pushed, false if buffer full
     */
    bool push(const T& item) noexcept {
        const std::size_t currentWrite = m_writeIndex.load(std::memory_order_relaxed);
        const std::size_t nextWrite = (currentWrite + 1) & kMask;
        
        // Check if buffer is full
        if (nextWrite == m_readIndex.load(std::memory_order_acquire)) {
            return false; // Buffer full
        }
        
        m_buffer[currentWrite] = item;
        m_writeIndex.store(nextWrite, std::memory_order_release);
        return true;
    }
    
    /**
     * Push multiple elements (PRODUCER ONLY - Audio Thread)
     * @return number of elements actually pushed
     */
    std::size_t pushBulk(const T* items, std::size_t count) noexcept {
        std::size_t pushed = 0;
        for (std::size_t i = 0; i < count; ++i) {
            if (!push(items[i])) break;
            ++pushed;
        }
        return pushed;
    }
    
    /**
     * Pop a single element (CONSUMER ONLY - CQT Worker Thread)
     * @return true if popped, false if buffer empty
     */
    bool pop(T& item) noexcept {
        const std::size_t currentRead = m_readIndex.load(std::memory_order_relaxed);
        
        // Check if buffer is empty
        if (currentRead == m_writeIndex.load(std::memory_order_acquire)) {
            return false; // Buffer empty
        }
        
        item = m_buffer[currentRead];
        m_readIndex.store((currentRead + 1) & kMask, std::memory_order_release);
        return true;
    }
    
    /**
     * Pop multiple elements (CONSUMER ONLY - CQT Worker Thread)
     * @return number of elements actually popped
     */
    std::size_t popBulk(T* items, std::size_t maxCount) noexcept {
        std::size_t popped = 0;
        for (std::size_t i = 0; i < maxCount; ++i) {
            if (!pop(items[i])) break;
            ++popped;
        }
        return popped;
    }
    
    /**
     * Check how many elements are available to read
     */
    std::size_t available() const noexcept {
        const std::size_t write = m_writeIndex.load(std::memory_order_acquire);
        const std::size_t read = m_readIndex.load(std::memory_order_relaxed);
        return (write - read) & kMask;
    }
    
    /**
     * Check how much space is available to write
     */
    std::size_t space() const noexcept {
        return Capacity - 1 - available();
    }
    
    bool empty() const noexcept { return available() == 0; }
    bool full() const noexcept { return space() == 0; }
    
    static constexpr std::size_t capacity() noexcept { return Capacity; }
    
private:
    static constexpr std::size_t kMask = Capacity - 1;
    
    // Separate cache lines for producer and consumer indices
    alignas(kCacheLineSize) std::atomic<std::size_t> m_writeIndex;
    alignas(kCacheLineSize) std::atomic<std::size_t> m_readIndex;
    alignas(kCacheLineSize) std::array<T, Capacity> m_buffer;
};

/**
 * AudioRingBuffer - Specialized ring buffer for interleaved multi-channel audio frames
 * 
 * Stores frames as arrays of 6 floats (one per guitar string)
 */
struct AudioFrame {
    std::array<float, 6> samples;  // One sample per string
};

// 8192 frames = ~186ms buffer at 44.1kHz (plenty of headroom)
// Must be power of 2
constexpr std::size_t kAudioRingBufferCapacity = 8192;

using AudioRingBuffer = LockFreeRingBuffer<AudioFrame, kAudioRingBufferCapacity>;

} // namespace audio
