#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <memory>

#include "../model/Document.h"
#include "RealtimeSwap.h"
#include "Voice.h"

namespace chops
{

// The audio-thread core: a preallocated voice pool driven by sample-accurate
// MIDI, reading an immutable Document published from the message thread.
class Engine
{
public:
    static constexpr int kMaxVoices = 32;

    void prepare (double sampleRate, int maxBlockSize);

    // Message thread. May allocate; never blocks the audio thread.
    void publishDocument (std::unique_ptr<const Document> doc);
    void reclaim();

    // Audio thread. Expects a cleared buffer; renders additively.
    void process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi) noexcept;

    // UI feedback (read from message thread, torn values are harmless).
    std::atomic<int> uiSectionIndex { -1 };
    std::atomic<double> uiPositionFrames { -1.0 };

private:
    void handleMidi (const Document* doc, const juce::uint8* data, int numBytes) noexcept;
    void startSectionVoice (const Document& doc, int sectionIdx, int note, float velocity) noexcept;
    void renderSpan (juce::AudioBuffer<float>& buffer, int start, int numFrames) noexcept;
    void stopAllVoices() noexcept;

    RealtimeSwap<Document> swap;
    std::array<Voice, kMaxVoices> voices;
    double hostRate = 44100.0;
    const Document* lastDoc = nullptr;
    std::uint64_t nextSerial = 1;
};

} // namespace chops
