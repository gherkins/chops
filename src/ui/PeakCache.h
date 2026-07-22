#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace chops
{

// Mip-mapped min/max peaks over all channels combined. Built once per sample
// on the message thread; per-pixel queries stay cheap at any zoom, which is
// what keeps waveform repaints inside a single frame.
class PeakCache
{
public:
    void clear()
    {
        levels.clear();
        totalFrames = 0;
    }

    void build (const juce::AudioBuffer<float>& buffer)
    {
        clear();
        totalFrames = buffer.getNumSamples();
        if (totalFrames <= 0)
            return;

        // Base level: 256 frames per bin, then x16 per level until tiny.
        for (juce::int64 framesPerBin = 256;; framesPerBin *= 16)
        {
            Level level;
            level.framesPerBin = framesPerBin;
            const auto bins = (totalFrames + framesPerBin - 1) / framesPerBin;
            level.mins.resize ((size_t) bins);
            level.maxs.resize ((size_t) bins);

            if (levels.empty())
            {
                for (juce::int64 b = 0; b < bins; ++b)
                {
                    const auto begin = b * framesPerBin;
                    const auto end = std::min ((juce::int64) totalFrames, begin + framesPerBin);
                    float lo = 0.0f, hi = 0.0f;

                    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    {
                        const float* data = buffer.getReadPointer (ch);
                        for (auto i = begin; i < end; ++i)
                        {
                            lo = std::min (lo, data[i]);
                            hi = std::max (hi, data[i]);
                        }
                    }

                    level.mins[(size_t) b] = lo;
                    level.maxs[(size_t) b] = hi;
                }
            }
            else
            {
                const auto& prev = levels.back();
                for (juce::int64 b = 0; b < bins; ++b)
                {
                    const auto begin = (size_t) (b * 16);
                    const auto end = std::min (prev.mins.size(), begin + 16);
                    float lo = 0.0f, hi = 0.0f;
                    for (auto i = begin; i < end; ++i)
                    {
                        lo = std::min (lo, prev.mins[i]);
                        hi = std::max (hi, prev.maxs[i]);
                    }
                    level.mins[(size_t) b] = lo;
                    level.maxs[(size_t) b] = hi;
                }
            }

            const auto binCount = (juce::int64) level.mins.size();
            levels.push_back (std::move (level));
            if (binCount <= 2048)
                break;
        }
    }

    // Min/max over [begin, end), expanded outward to bin bounds of the level
    // chosen for the range (edge overshoot is at most one bin — invisible).
    void query (const juce::AudioBuffer<float>& raw,
                juce::int64 begin, juce::int64 end, float& lo, float& hi) const
    {
        lo = 0.0f;
        hi = 0.0f;
        begin = std::max ((juce::int64) 0, begin);
        end = std::min ((juce::int64) totalFrames, end);
        if (begin >= end)
            return;

        if (end - begin < 512 || levels.empty())
        {
            for (int ch = 0; ch < raw.getNumChannels(); ++ch)
            {
                const float* data = raw.getReadPointer (ch);
                for (auto i = begin; i < end; ++i)
                {
                    lo = std::min (lo, data[i]);
                    hi = std::max (hi, data[i]);
                }
            }
            return;
        }

        size_t levelIndex = 0;
        for (size_t l = 0; l < levels.size(); ++l)
            if (levels[l].framesPerBin * 2 <= end - begin)
                levelIndex = l;

        const auto& level = levels[levelIndex];
        const auto firstBin = begin / level.framesPerBin;
        const auto lastBin = (end - 1) / level.framesPerBin;

        for (auto b = firstBin; b <= lastBin && b < (juce::int64) level.mins.size(); ++b)
        {
            lo = std::min (lo, level.mins[(size_t) b]);
            hi = std::max (hi, level.maxs[(size_t) b]);
        }
    }

    juce::int64 frames() const noexcept { return totalFrames; }

private:
    struct Level
    {
        juce::int64 framesPerBin = 0;
        std::vector<float> mins, maxs;
    };

    std::vector<Level> levels;
    juce::int64 totalFrames = 0;
};

} // namespace chops
