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
    held = true;
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

        case PlayMode::LoopRun:
            // Stop wrapping: the pass in flight becomes the final loop pass,
            // then playback runs on through the rest of the section.
            held = false;
            break;

        case PlayMode::Gate:
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

float Voice::readLooped (const float* channel, double pos, bool looping) const noexcept
{
    float value = readInterpolated (channel, pos);

    // Loop crossfade: approaching the wrap point, blend in the material from
    // one loop-length away so the wrap itself is seamless.
    if (looping && params_.xfadeFrames > 0)
    {
        const auto loopLength = (double) (params_.loopEnd - params_.loopStart);
        const auto xfade = (double) params_.xfadeFrames;

        if (! params_.reverse)
        {
            const double toWrap = (double) params_.loopEnd - pos;
            if (toWrap < xfade && pos >= (double) params_.loopStart)
            {
                const float w = (float) (toWrap / xfade);
                value = value * w + readInterpolated (channel, pos - loopLength) * (1.0f - w);
            }
        }
        else
        {
            const double toWrap = pos - (double) params_.loopStart;
            if (toWrap < xfade && pos <= (double) params_.loopEnd)
            {
                const float w = (float) (toWrap / xfade);
                value = value * w + readInterpolated (channel, pos + loopLength) * (1.0f - w);
            }
        }
    }

    return value;
}

void Voice::render (float* outL, float* outR, int numFrames) noexcept
{
    if (state == State::Idle || numFrames <= 0)
        return;

    const float* srcL = sample_.channels[0];
    const float* srcR = sample_.numChannels > 1 ? sample_.channels[1] : srcL;
    const bool stereoOut = outR != nullptr;
    const bool stereoSrc = srcR != srcL;

    const bool canLoop = params_.mode == PlayMode::LoopRun && params_.hasLoop();
    const auto loopLength = canLoop ? (double) (params_.loopEnd - params_.loopStart) : 0.0;

    for (int i = 0; i < numFrames; ++i)
    {
        const bool looping = canLoop && held;

        if (looping)
        {
            if (! params_.reverse)
                while (phase >= (double) params_.loopEnd)
                    phase -= loopLength;
            else
                while (phase < (double) params_.loopStart)
                    phase += loopLength;
        }

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

        const float l = readLooped (srcL, phase, looping);

        if (stereoOut)
        {
            outL[i] += l * gain;
            outR[i] += (stereoSrc ? readLooped (srcR, phase, looping) : l) * gain;
        }
        else
        {
            outL[i] += (stereoSrc ? 0.5f * (l + readLooped (srcR, phase, looping)) : l) * gain;
        }

        phase += params_.reverse ? -increment : increment;
    }
}

} // namespace chops
