#pragma once

#include <cstdint>

// Pure C++ playback types shared between the model and the engine.
// Deliberately JUCE-free so the DSP core can be unit-tested standalone.

namespace chops
{

enum class PlayMode : int
{
    LoopRun = 0,   // play into loop, hold while note held, finish pass + tail on release
    OneShot = 1,   // note-on plays start..end, note-off ignored
    Gate    = 2,   // plays while held, short release ramp on note-off
};

// Loop traversal, relative to the slice's playback direction: "Forward" runs
// with the sample (backwards when the slice is reversed).
enum class LoopDirection : int
{
    Forward  = 0,  // wrap far boundary -> near boundary
    Backward = 1,  // loop plays against the playback direction
    PingPong = 2,  // bounce between the boundaries
};

// Non-owning view into decoded sample data. The owner (Document/SampleData)
// must outlive any voice reading through it; the engine guarantees this by
// fading voices out well inside RealtimeSwap's reclamation window.
struct SampleView
{
    const float* const* channels = nullptr;
    int numChannels = 0;
    std::int64_t numFrames = 0;
    double sampleRate = 44100.0;
};

// Snapshot of everything a voice needs at note-on. Copied by value so later
// document edits never race with a running voice.
struct VoiceParams
{
    std::int64_t start = 0;
    std::int64_t end = 0;          // exclusive
    std::int64_t loopStart = -1;   // -1 == no loop region
    std::int64_t loopEnd = -1;
    PlayMode mode = PlayMode::OneShot;
    LoopDirection loopDir = LoopDirection::Forward;
    bool reverse = false;
    int xfadeFrames = 0;           // loop crossfade

    bool hasLoop() const noexcept { return loopStart >= 0 && loopEnd > loopStart; }
};

// Per-voice DSP settings, resolved from section overrides falling back to the
// globals. Refreshed every block for running voices, so tweaking FX live
// affects a sustaining loop — a performance feature, not just a preset one.
struct VoiceFx
{
    double rate = 1.0;             // playback rate (pitch * srcRate/hostRate)
    double decimHz = 0.0;          // sample-and-hold rate; <= 0 (or >= host rate) = clean
    float drive = 0.0f;            // 0..1 waveshaper amount
    float gain = 1.0f;             // section gain * global gain
};

} // namespace chops
