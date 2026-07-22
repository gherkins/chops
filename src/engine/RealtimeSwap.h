#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace chops
{

// Latest-value exchange between one producer (message thread) and one consumer
// (audio thread). acquire()/endBlock() are wait-free; publish()/reclaim() are
// producer-side only and may allocate.
//
// Retired values are deleted only after the consumer has advanced
// kSafetyBlocks past the swap. Voices reading from a retired document are
// always faded out within a couple of blocks, so the window also guarantees
// no voice ever touches freed sample data.
template <typename T>
class RealtimeSwap
{
public:
    static constexpr std::uint64_t kSafetyBlocks = 128;

    RealtimeSwap() = default;

    RealtimeSwap (const RealtimeSwap&) = delete;
    RealtimeSwap& operator= (const RealtimeSwap&) = delete;

    ~RealtimeSwap()
    {
        delete current.exchange (nullptr);
        for (auto& r : retired)
            delete r.ptr;
    }

    // --- producer (message thread) ---
    void publish (std::unique_ptr<const T> next)
    {
        const T* old = current.exchange (next.release(), std::memory_order_acq_rel);
        if (old != nullptr)
            retired.push_back ({ old, epoch.load (std::memory_order_acquire) });
        reclaim();
    }

    void reclaim()
    {
        const auto now = epoch.load (std::memory_order_acquire);
        std::erase_if (retired, [now] (const Retired& r)
        {
            if (now >= r.epoch + kSafetyBlocks)
            {
                delete r.ptr;
                return true;
            }
            return false;
        });
    }

    // --- consumer (audio thread) ---
    const T* acquire() const noexcept { return current.load (std::memory_order_acquire); }
    void endBlock() noexcept          { epoch.fetch_add (1, std::memory_order_acq_rel); }

private:
    struct Retired
    {
        const T* ptr;
        std::uint64_t epoch;
    };

    std::atomic<const T*> current { nullptr };
    std::atomic<std::uint64_t> epoch { 0 };
    std::vector<Retired> retired;   // producer-side only
};

} // namespace chops
