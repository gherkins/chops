#pragma once

#include "Playback.h"

namespace chops
{

// One playing section instance. Pure C++, no JUCE, no allocation: everything
// it needs is copied in at start(). Renders additively into the output.
class Voice
{
public:
    void reset() noexcept;

    void start (const SampleView& sample, const VoiceParams& params,
                int midiNote, float velocity, double outputRate,
                int sectionIndex, std::uint64_t serial) noexcept;

    void noteOff() noexcept;    // Gate: release ramp. OneShot: ignored.
    void fastFade() noexcept;   // steal / document-change kill, ~2 ms

    void render (float* outL, float* outR, int numFrames) noexcept;

    bool isActive() const noexcept          { return state != State::Idle; }
    int note() const noexcept               { return midiNote_; }
    int sectionIndex() const noexcept       { return sectionIndex_; }
    std::uint64_t serial() const noexcept   { return serial_; }
    double position() const noexcept        { return phase; }

    // Identity of the sample data this voice reads from, used to fade voices
    // out when a new document no longer contains their sample.
    const void* sampleId() const noexcept   { return sample_.channels; }

private:
    enum class State { Idle, Playing, Releasing, Fading };

    float readInterpolated (const float* channel, double pos) const noexcept;
    float readLooped (const float* channel, double pos, bool looping) const noexcept;

    static constexpr int kAttackFrames = 32;       // sub-ms declick on start
    static constexpr double kReleaseSeconds = 0.003;
    static constexpr double kFastFadeSeconds = 0.002;

    SampleView sample_;
    VoiceParams params_;
    State state = State::Idle;
    double phase = 0.0;          // absolute position in source frames
    double increment = 0.0;
    double outputRate_ = 44100.0;
    float velocityGain = 1.0f;
    float rampGain = 1.0f;       // release / fast-fade envelope
    float rampStep = 0.0f;
    int attackRemaining = 0;
    bool held = false;           // LoopRun: wraps only while the note is held
    int midiNote_ = -1;
    int sectionIndex_ = -1;
    std::uint64_t serial_ = 0;
};

} // namespace chops
