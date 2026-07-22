#include "Voice.h"

#include <algorithm>
#include <cmath>

namespace chops
{

void Voice::reset() noexcept
{
    state = State::Idle;
    midiNote_ = -1;
    sectionIndex_ = -1;
}

void Voice::start (const SampleView& sample, const VoiceParams& params, const VoiceFx& fx,
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
    fx_ = fx;
    outputRate_ = outputRate;
    midiNote_ = midiNote;
    sectionIndex_ = sectionIndex;
    serial_ = serial;
    velocityGain = velocity;
    rampGain = 1.0f;
    rampStep = 0.0f;
    gainNow = fx.gain;      // no ramp-in: the attack ramp handles the declick
    attackRemaining = kAttackFrames;
    decimPhase = 1.0;       // capture on the very first frame
    holdL = holdR = 0.0f;

    // Reverse starts on the last frame inside the section.
    phase = params_.reverse ? (double) (params_.end - 1) : (double) params_.start;
    increment = fx_.rate;
    dirSign = params_.reverse ? -1 : 1;
    held = true;
    state = State::Playing;
}

void Voice::updateFx (const VoiceFx& fx) noexcept
{
    fx_ = fx;
    increment = fx.rate;
}

void Voice::updateLoop (std::int64_t loopStart, std::int64_t loopEnd,
                        int xfadeFrames, LoopDirection loopDir) noexcept
{
    params_.loopStart = loopStart;
    params_.loopEnd = loopEnd;
    params_.xfadeFrames = xfadeFrames;
    params_.loopDir = loopDir;
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

float Voice::readLooped (const float* channel, double pos, bool jumpFade) const noexcept
{
    float value = readInterpolated (channel, pos);

    // Loop crossfade: approaching a jump-cut wrap, blend in the material from
    // one loop-length away so the wrap itself is seamless. Keyed on the
    // current travel direction (PingPong reflections have no jump to hide).
    if (jumpFade && params_.xfadeFrames > 0)
    {
        const auto loopLength = (double) (params_.loopEnd - params_.loopStart);
        const auto xfade = (double) params_.xfadeFrames;

        if (dirSign > 0)
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
    const auto loopStart = (double) params_.loopStart;
    const auto loopEnd = (double) params_.loopEnd;
    const auto loopLength = canLoop ? loopEnd - loopStart : 0.0;
    const int mainSign = params_.reverse ? -1 : 1;
    // The direction a settled loop travels in: with the sample for Forward,
    // against it for Backward. Jump-cut crossfades only apply on that leg.
    const int steadySign = params_.loopDir == LoopDirection::Backward ? -mainSign : mainSign;

    // Per-block DSP constants.
    const double decimIncrement = fx_.decimHz > 0.0 ? fx_.decimHz / outputRate_ : 0.0;
    const float driveG = fx_.drive * 8.0f;
    const bool shaping = driveG > 1.0e-3f;
    // tanh(g*x)/tanh(g): transparent as g->0, normalized hard clip as g grows.
    const float driveNorm = shaping ? 1.0f / std::tanh (driveG) : 1.0f;

    for (int i = 0; i < numFrames; ++i)
    {
        const bool looping = canLoop && held;

        // Boundary handling. fmod, not subtraction loops: live loop edits can
        // leave the phase arbitrarily far outside, re-entry must stay O(1).
        if (looping)
        {
            switch (params_.loopDir)
            {
                case LoopDirection::Forward:
                    dirSign = mainSign;   // restores direction after a live switch
                    if (mainSign > 0)
                    {
                        if (phase >= loopEnd)
                            phase = loopStart + std::fmod (phase - loopStart, loopLength);
                    }
                    else
                    {
                        if (phase < loopStart)
                            phase = loopEnd - std::fmod (loopEnd - phase, loopLength);
                    }
                    break;

                case LoopDirection::Backward:
                    // Entry reflects at the far boundary; the settled loop
                    // then wraps with a jump-cut at the near one.
                    if (mainSign > 0)
                    {
                        if (dirSign > 0 && phase >= loopEnd)
                        {
                            phase = loopEnd - std::fmod (phase - loopEnd, loopLength);
                            dirSign = -1;
                        }
                        else if (dirSign < 0 && phase < loopStart)
                        {
                            phase = loopEnd - std::fmod (loopStart - phase, loopLength);
                        }
                    }
                    else
                    {
                        if (dirSign < 0 && phase < loopStart)
                        {
                            phase = loopStart + std::fmod (loopStart - phase, loopLength);
                            dirSign = 1;
                        }
                        else if (dirSign > 0 && phase >= loopEnd)
                        {
                            phase = loopStart + std::fmod (phase - loopEnd, loopLength);
                        }
                    }
                    break;

                case LoopDirection::PingPong:
                    if (dirSign > 0 && phase >= loopEnd)
                    {
                        phase = loopEnd - std::fmod (phase - loopEnd, loopLength);
                        dirSign = -1;
                    }
                    else if (dirSign < 0 && phase < loopStart)
                    {
                        phase = loopStart + std::fmod (loopStart - phase, loopLength);
                        dirSign = 1;
                    }
                    break;
            }
        }
        else if (canLoop && dirSign != mainSign)
        {
            // Released while travelling against the playback direction:
            // bounce once at the near boundary, then run out to the end —
            // the counter pass becomes the final loop pass.
            if (mainSign > 0 && phase < loopStart)
            {
                phase = loopStart + (loopStart - phase);
                dirSign = 1;
            }
            else if (mainSign < 0 && phase >= loopEnd)
            {
                phase = loopEnd - (phase - loopEnd);
                dirSign = -1;
            }
        }
        else if (! canLoop)
        {
            dirSign = mainSign;   // loop removed live: restore direction
        }

        if (mainSign > 0 ? (phase >= (double) params_.end)
                         : (phase < (double) params_.start))
        {
            state = State::Idle;
            return;
        }

        gainNow += (fx_.gain - gainNow) * 0.002f;
        float gain = velocityGain * gainNow;

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

        const bool jumpFade = looping && params_.loopDir != LoopDirection::PingPong
                              && dirSign == steadySign;
        float l = readLooped (srcL, phase, jumpFade);
        float r = stereoSrc ? readLooped (srcR, phase, jumpFade) : l;

        if (decimIncrement > 0.0)
        {
            if (decimPhase >= 1.0)
            {
                decimPhase -= std::floor (decimPhase);
                holdL = l;
                holdR = r;
            }
            l = holdL;
            r = holdR;
            decimPhase += decimIncrement;
        }

        if (shaping)
        {
            l = std::tanh (driveG * l) * driveNorm;
            r = stereoSrc || decimIncrement > 0.0 ? std::tanh (driveG * r) * driveNorm : l;
        }

        if (stereoOut)
        {
            outL[i] += l * gain;
            outR[i] += r * gain;
        }
        else
        {
            outL[i] += 0.5f * (l + r) * gain;
        }

        phase += dirSign * increment;
    }
}

} // namespace chops
