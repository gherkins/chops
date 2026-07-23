#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>
#include <vector>

#include "../engine/Playback.h"

namespace chops
{

// Decoded sample plus the compressed blob that gets embedded in plugin state.
// The blob is produced ONCE when the sample is loaded and reused verbatim on
// every save, so hosts that snapshot state per parameter tweak (Reaper undo)
// never pay for re-encoding.
struct SampleData
{
    juce::AudioBuffer<float> buffer;      // PCM at the original sample rate
    double sourceSampleRate = 44100.0;
    int sourceBitDepth = 24;
    juce::MemoryBlock embeddedBlob;
    juce::String embeddedFormat;          // decoder hint: "flac", "ogg", "mp3", ...
    juce::String originalPath;            // informational only, never required

    juce::int64 numFrames() const noexcept { return buffer.getNumSamples(); }
};

// A slice is a freely positioned section of the sample. Sections may overlap.
struct Section
{
    juce::int64 start = 0;
    juce::int64 end = 0;                  // exclusive
    juce::int64 loopStart = -1;           // -1 == no loop region
    juce::int64 loopEnd = -1;
    PlayMode mode = PlayMode::Gate;
    LoopDirection loopDir = LoopDirection::Forward;
    bool reverse = false;
    int pitchSemis = 0;
    float fineCents = 0.0f;
    float gain = 1.0f;
    double srOverride = 0.0;              // Hz; 0 == follow global
    float driveOverride = -1.0f;          // -1 == follow global
    int xfadeFrames = 0;
    int midiNote = 36;

    bool hasLoop() const noexcept { return loopStart >= 0 && loopEnd > loopStart; }
};

struct GlobalParams
{
    double srReduce = 0.0;                // Hz; 0 == off
    float drive = 0.0f;                   // 0..1
    int pitchSemis = 0;
    float fineCents = 0.0f;
    float gain = 1.0f;
    int rootNote = 36;                    // C1, pad-controller convention
    bool mono = true;                     // one choke group across all slices
};

// The full instrument model. Immutable once published to the audio thread:
// edits build a new Document (cheap — SampleData is shared) and swap it in.
struct Document
{
    std::shared_ptr<const SampleData> sample;   // nullptr == empty instrument
    std::vector<Section> sections;              // never empty while sample != nullptr
    GlobalParams global;
};

// Loading a sample always creates one whole-file section, so the workflow is
// identical with and without slices.
inline Section makeWholeFileSection (const SampleData& s, int rootNote)
{
    Section sec;
    sec.start = 0;
    sec.end = s.numFrames();
    sec.mode = PlayMode::Gate;
    sec.midiNote = rootNote;
    return sec;
}

inline SampleView makeSampleView (const SampleData& s)
{
    return { s.buffer.getArrayOfReadPointers(),
             s.buffer.getNumChannels(),
             (std::int64_t) s.buffer.getNumSamples(),
             s.sourceSampleRate };
}

} // namespace chops
