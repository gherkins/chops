#include "Voice.h"

#include <algorithm>

namespace chops
{

void Voice::reset() noexcept
{
    state = State::Idle;
    midiNote_ = -1;
    sectionIndex_ = -1;
}

void Voice::start (const SampleView& sample, const VoiceParams& params,
                   int midiNote, float velocity, double outputRate,
                   int sectionIndex, std::uint64_t serial) noexcept
{
    if (sample.channels == nullptr || sample.numFrames <= 0
        || params.end <= params.start || outputRate <= 0.0)
    {
        reset();
        return;
    }

    sample_ = sample;
    params_ = params;
    outputRate_ = outputRate;
    midiNote_ = midiNote;
    sectionIndex_ = sectionIndex;
    serial_ = serial;
    velocityGain = velocity;
    rampGain = 1.0f;
    rampStep = 0.0f;
    attackRemaining = kAttackFrames;

    // Reverse starts on the last frame inside the section.
    phase = params_.reverse ? (double) (params_.end - 1) : (double) params_.start;
    increment = params_.rate;
    state = State::Playing;
}

void Voice::noteOff() noexcept
{
    if (state != State::Playing)
        return;

    switch (params_.mode)
    {
        case PlayMode::OneShot:
            break;

        case PlayMode::Gate:
        case PlayMode::LoopRun:   // LoopRun release behaviour lands in M3; gate for now
            state = State::Releasing;
            rampStep = (float) (1.0 / (kReleaseSeconds * outputRate_));
            break;
    }
}

void Voice::fastFade() noexcept
{
    if (state == State::Idle || state == State::Fading)
        return;

    state = State::Fading;
    rampStep = (float) (1.0 / (kFastFadeSeconds * outputRate_));
}

float Voice::readInterpolated (const float* channel, double pos) const noexcept
{
    const auto n = sample_.numFrames;
    const auto i = (std::int64_t) pos;
    const float t = (float) (pos - (double) i);

    auto at = [channel, n] (std::int64_t idx) noexcept
    {
        return channel[std::clamp<std::int64_t> (idx, 0, n - 1)];
    };

    const float p0 = at (i - 1);
    const float p1 = at (i);
    const float p2 = at (i + 1);
    const float p3 = at (i + 2);

    // Catmull-Rom
    return 0.5f * ((2.0f * p1)
                   + (p2 - p0) * t
                   + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t * t
                   + (3.0f * (p1 - p2) + p3 - p0) * t * t * t);
}

void Voice::render (float* outL, float* outR, int numFrames) noexcept
{
    if (state == State::Idle || numFrames <= 0)
        return;

    const float* srcL = sample_.channels[0];
    const float* srcR = sample_.numChannels > 1 ? sample_.channels[1] : srcL;
    const bool stereoOut = outR != nullptr;
    const bool stereoSrc = srcR != srcL;

    for (int i = 0; i < numFrames; ++i)
    {
        if (params_.reverse ? (phase < (double) params_.start)
                            : (phase >= (double) params_.end))
        {
            state = State::Idle;
            return;
        }

        float gain = velocityGain * params_.gain;

        if (attackRemaining > 0)
        {
            gain *= (float) (kAttackFrames - attackRemaining + 1) / (float) kAttackFrames;
            --attackRemaining;
        }

        if (state == State::Releasing || state == State::Fading)
        {
            rampGain -= rampStep;
            if (rampGain <= 0.0f)
            {
                state = State::Idle;
                return;
            }
            gain *= rampGain;
        }

        const float l = readInterpolated (srcL, phase);

        if (stereoOut)
        {
            outL[i] += l * gain;
            outR[i] += (stereoSrc ? readInterpolated (srcR, phase) : l) * gain;
        }
        else
        {
            outL[i] += (stereoSrc ? 0.5f * (l + readInterpolated (srcR, phase)) : l) * gain;
        }

        phase += params_.reverse ? -increment : increment;
    }
}

} // namespace chops
