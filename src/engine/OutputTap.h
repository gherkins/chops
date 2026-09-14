#pragma once

// Wait-free capture of the engine's final output for the UI tuner. The audio
// thread mono-sums each block into a fixed ring of relaxed atomics; the
// message thread copies the newest window out. The ring is twice the window,
// so a torn read would need the audio thread to write a full window during
// one memcpy — harmless if it ever happens (the reading is smoothed anyway).
// JUCE-free and allocation-free.

#include <array>
#include <atomic>
#include <cstdint>

#include "GridTune.h"

namespace chops
{

struct OutputTap
{
    static constexpr int kSize = 2 * tune::kWindow;

    std::array<std::atomic<float>, kSize> ring {};
    std::atomic<std::uint32_t> writePos { 0 };

    // Audio thread. r may be null for mono buses.
    void push (const float* l, const float* r, int n) noexcept
    {
        std::uint32_t pos = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            const float v = r != nullptr ? 0.5f * (l[i] + r[i]) : l[i];
            ring[pos % kSize].store (v, std::memory_order_relaxed);
            ++pos;
        }
        writePos.store (pos, std::memory_order_relaxed);
    }

    // Message thread: the newest n frames, oldest first. n <= kSize.
    void copyLatest (float* dest, int n) const noexcept
    {
        const std::uint32_t end = writePos.load (std::memory_order_relaxed);
        std::uint32_t pos = end - (std::uint32_t) n;
        for (int i = 0; i < n; ++i, ++pos)
            dest[i] = ring[pos % kSize].load (std::memory_order_relaxed);
    }

    void clear() noexcept
    {
        for (auto& v : ring)
            v.store (0.0f, std::memory_order_relaxed);
        writePos.store (0, std::memory_order_relaxed);
    }
};

} // namespace chops
