// Offline tests for the chops engine and state layer. No plugin wrapper, no
// GUI: pure Document -> Engine -> buffer processing, run by ctest.

#include <juce_audio_formats/juce_audio_formats.h>

#include "../src/engine/Engine.h"
#include "../src/model/Document.h"
#include "../src/state/State.h"

#include <cstdio>
#include <cstring>

static int failures = 0;

#define EXPECT(cond)                                                          \
    do                                                                        \
    {                                                                         \
        if (! (cond))                                                         \
        {                                                                     \
            ++failures;                                                       \
            std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                     \
    } while (false)

// The test sample: 1 s stereo int16 WAV at 44.1 kHz. DC values are chosen so
// int16 quantisation is exact and every playback sample is predictable.
static constexpr float kLeftValue = 0.5f;
static constexpr float kRightValue = -0.25f;
static constexpr int kFrames = 44100;
static constexpr double kRate = 44100.0;

static juce::File writeTestWav()
{
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getNonexistentChildFile ("chops_test", ".wav");

    juce::AudioBuffer<float> buffer (2, kFrames);
    for (int i = 0; i < kFrames; ++i)
    {
        buffer.setSample (0, i, kLeftValue);
        buffer.setSample (1, i, kRightValue);
    }

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    auto writer = wav.createWriterFor (stream,
                                       juce::AudioFormatWriterOptions{}
                                           .withSampleRate (kRate)
                                           .withNumChannels (2)
                                           .withBitsPerSample (16));
    if (writer == nullptr)
        return {};

    writer->writeFromAudioSampleBuffer (buffer, 0, kFrames);
    return file;
}

static bool buffersIdentical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
        return false;

    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        if (std::memcmp (a.getReadPointer (ch), b.getReadPointer (ch),
                         sizeof (float) * (size_t) a.getNumSamples()) != 0)
            return false;

    return true;
}

static void addNoteOn (juce::MidiBuffer& midi, int note, int samplePosition, juce::uint8 velocity = 127)
{
    midi.addEvent (juce::MidiMessage::noteOn (1, note, velocity), samplePosition);
}

static void addNoteOff (juce::MidiBuffer& midi, int note, int samplePosition)
{
    midi.addEvent (juce::MidiMessage::noteOff (1, note), samplePosition);
}

int main()
{
    const auto wavFile = writeTestWav();
    EXPECT (wavFile.existsAsFile());

    // --- load: decode + embed blob built once ---
    juce::String error;
    auto sample = chops::state::loadSampleFile (wavFile, error);
    EXPECT (sample != nullptr);
    if (sample == nullptr)
    {
        std::printf ("load error: %s\n", error.toRawUTF8());
        return 1;
    }

    EXPECT (sample->numFrames() == kFrames);
    EXPECT (sample->buffer.getNumChannels() == 2);
    EXPECT (sample->sourceSampleRate == kRate);
    EXPECT (sample->sourceBitDepth == 16);
    EXPECT (sample->embeddedFormat == "flac");
    EXPECT (! sample->embeddedBlob.isEmpty());
    EXPECT (sample->buffer.getSample (0, 0) == kLeftValue);
    EXPECT (sample->buffer.getSample (1, 0) == kRightValue);
    EXPECT ((juce::int64) sample->embeddedBlob.getSize() < (juce::int64) kFrames * 2 * 2);

    chops::Document doc;
    doc.sample = sample;
    doc.sections = { chops::makeWholeFileSection (*sample, doc.global.rootNote) };
    EXPECT (doc.sections[0].end == kFrames);
    EXPECT (doc.sections[0].midiNote == 36);

    // --- state round trip: decoded audio and re-serialised state bit-exact ---
    juce::MemoryBlock stateA;
    chops::state::toMemory (doc, stateA);
    EXPECT (stateA.getSize() > sample->embeddedBlob.getSize());

    auto restored = chops::state::fromMemory (stateA.getData(), stateA.getSize(), error);
    EXPECT (restored != nullptr);
    if (restored == nullptr)
    {
        std::printf ("restore error: %s\n", error.toRawUTF8());
        return 1;
    }

    EXPECT (restored->sample != nullptr);
    EXPECT (buffersIdentical (restored->sample->buffer, sample->buffer));
    EXPECT (restored->sample->sourceSampleRate == kRate);
    EXPECT (restored->sections.size() == 1);
    EXPECT (restored->sections[0].start == 0);
    EXPECT (restored->sections[0].end == kFrames);
    EXPECT (restored->sections[0].mode == chops::PlayMode::Gate);
    EXPECT (restored->sections[0].midiNote == 36);

    // Saving the restored document must reuse the embedded blob verbatim.
    juce::MemoryBlock stateB;
    chops::state::toMemory (*restored, stateB);
    EXPECT (stateA == stateB);

    // --- engine: sample-accurate start, zero latency ---
    chops::Engine engine;
    engine.prepare (kRate, 512);
    engine.publishDocument (std::make_unique<const chops::Document> (doc));

    juce::AudioBuffer<float> out (2, 512);
    {
        out.clear();
        juce::MidiBuffer midi;
        addNoteOn (midi, 36, 137);
        engine.process (out, midi);

        EXPECT (out.getSample (0, 136) == 0.0f);
        // First frame: attack ramp step 1/32 on the raw value, exact in float.
        EXPECT (out.getSample (0, 137) == kLeftValue / 32.0f);
        EXPECT (out.getSample (1, 137) == kRightValue / 32.0f);
        // Attack finished: verbatim playback.
        EXPECT (out.getSample (0, 137 + 40) == kLeftValue);
        EXPECT (out.getSample (1, 137 + 40) == kRightValue);
    }

    // --- gate release: ~3 ms ramp to silence after note-off ---
    {
        out.clear();
        juce::MidiBuffer midi;
        addNoteOff (midi, 36, 0);
        engine.process (out, midi);

        EXPECT (std::abs (out.getSample (0, 0)) < std::abs (kLeftValue));
        EXPECT (out.getSample (0, 200) == 0.0f);   // 3 ms @ 44.1k = 133 frames
        EXPECT (engine.uiPositionFrames.load() == -1.0);
    }

    // --- mono per section: retrigger cuts predecessor, no doubling ---
    {
        out.clear();
        juce::MidiBuffer midi;
        addNoteOn (midi, 36, 0);
        addNoteOn (midi, 36, 100);
        engine.process (out, midi);

        // Old voice fades over 2 ms (88 frames), new voice ramps in over 32.
        // Well past both: exactly one voice at full level.
        EXPECT (out.getSample (0, 400) == kLeftValue);
        EXPECT (out.getSample (1, 400) == kRightValue);

        float peak = 0.0f;
        for (int i = 0; i < 512; ++i)
            peak = std::max (peak, std::abs (out.getSample (0, i)));
        EXPECT (peak <= kLeftValue * 2.0f + 1.0e-6f);
    }

    wavFile.deleteFile();

    if (failures == 0)
        std::printf ("all tests passed\n");
    else
        std::printf ("%d failure(s)\n", failures);

    return failures == 0 ? 0 : 1;
}
