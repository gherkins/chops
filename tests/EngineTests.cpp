// Offline tests for the chops engine and state layer. No plugin wrapper, no
// GUI: pure Document -> Engine -> buffer processing, run by ctest.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>

#include "../src/engine/Engine.h"
#include "../src/model/Document.h"
#include "../src/model/Edits.h"
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

// Dev tool: synthesize a drum-loop-ish sample, slice it, give slice 0 a loop,
// and write the standalone app's settings file so the full UI can be driven
// and screenshotted without manual drag & drop.
//   chops_tests --make-standalone-state <settings-file-path>
static int makeStandaloneState (const juce::File& settingsFile)
{
    constexpr int frames = 2 * 44100;
    juce::AudioBuffer<float> buffer (2, frames);
    buffer.clear();

    juce::Random random (42);
    for (int hit = 0; hit < 8; ++hit)
    {
        const int start = hit * frames / 8;
        const float base = hit % 4 == 0 ? 0.9f : 0.55f;
        for (int i = 0; i < 6000 && start + i < frames; ++i)
        {
            const float env = base * std::exp (-6.0f * (float) i / 6000.0f);
            const float tone = std::sin (0.03f * (float) i) * 0.5f;
            for (int ch = 0; ch < 2; ++ch)
                buffer.setSample (ch, start + i,
                                  env * (tone + random.nextFloat() * 0.9f - 0.45f));
        }
    }

    const auto wav = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("chops_demo_loop.wav");
    wav.deleteFile();
    {
        juce::WavAudioFormat format;
        std::unique_ptr<juce::OutputStream> stream (wav.createOutputStream());
        auto writer = format.createWriterFor (stream,
                                              juce::AudioFormatWriterOptions{}
                                                  .withSampleRate (44100.0)
                                                  .withNumChannels (2)
                                                  .withBitsPerSample (16));
        if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (buffer, 0, frames))
            return 1;
    }

    juce::String error;
    chops::Document doc;
    doc.sample = chops::state::loadSampleFile (wav, error);
    if (doc.sample == nullptr)
        return 1;

    chops::edits::autoSliceEqual (doc, 4);
    chops::edits::setSectionMode (doc, 0, chops::PlayMode::LoopRun);
    chops::edits::setSectionLoop (doc, 0, 4000, 12000);
    chops::edits::setSectionReverse (doc, 2, true);
    chops::edits::setSectionDriveOverride (doc, 3, 0.6f);

    juce::MemoryBlock state;
    chops::state::toMemory (doc, state);

    juce::PropertiesFile::Options options;
    options.applicationName = "chops";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    juce::PropertiesFile props (settingsFile, options);
    props.setValue ("filterState", state.toBase64Encoding());
    return props.save() ? 0 : 1;
}

// Dev tool: run a settings file back through the exact standalone restore path.
static int verifyStandaloneState (const juce::File& settingsFile)
{
    juce::PropertiesFile::Options options;
    options.applicationName = "chops";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    juce::PropertiesFile props (settingsFile, options);

    const auto encoded = props.getValue ("filterState");
    std::printf ("filterState chars: %d\n", encoded.length());

    juce::MemoryBlock data;
    if (! data.fromBase64Encoding (encoded) || data.getSize() == 0)
    {
        std::printf ("base64 decode FAILED\n");
        return 1;
    }
    std::printf ("decoded bytes: %d\n", (int) data.getSize());

    juce::String error;
    auto doc = chops::state::fromMemory (data.getData(), data.getSize(), error);
    if (doc == nullptr)
    {
        std::printf ("fromMemory FAILED: %s\n", error.toRawUTF8());
        return 1;
    }

    std::printf ("sample: %s, frames: %lld, sections: %d\n",
                 doc->sample != nullptr ? "yes" : "NO",
                 doc->sample != nullptr ? (long long) doc->sample->numFrames() : 0,
                 (int) doc->sections.size());
    return 0;
}

int main (int argc, char* argv[])
{
    if (argc == 3 && juce::String (argv[1]) == "--make-standalone-state")
        return makeStandaloneState (juce::File (juce::String (argv[2])));
    if (argc == 3 && juce::String (argv[1]) == "--verify-standalone-state")
        return verifyStandaloneState (juce::File (juce::String (argv[2])));

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
    EXPECT (restored->global.mono);                  // default: one choke group

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
        for (const auto& slot : engine.uiVoices)
            EXPECT (slot.section.load() == -1);    // all voices retired
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

    // --- slicing edits: split / linked move / remove / auto-slice ---
    {
        chops::Document d;
        d.sample = sample;
        chops::edits::clearSlices (d);
        EXPECT (d.sections.size() == 1);

        EXPECT (chops::edits::splitAt (d, 20000));
        EXPECT (d.sections.size() == 2);
        EXPECT (d.sections[0].end == 20000 && d.sections[1].start == 20000);
        EXPECT (d.sections[0].midiNote == 36 && d.sections[1].midiNote == 37);

        // Too close to an existing boundary: rejected.
        EXPECT (! chops::edits::splitAt (d, 20000 + 10));

        // Linked partition move: previous end follows the dragged start.
        EXPECT (chops::edits::moveSectionStart (d, 1, 25000));
        EXPECT (d.sections[0].end == 25000 && d.sections[1].start == 25000);

        // Clamped move: cannot cross its own end or the previous start.
        EXPECT (chops::edits::moveSectionStart (d, 1, 999999));
        EXPECT (d.sections[1].start == kFrames - chops::edits::kMinSectionFrames);
        EXPECT (chops::edits::moveSectionStart (d, 1, 25000));

        // Remove merges back into the previous section.
        EXPECT (chops::edits::removeSection (d, 1));
        EXPECT (d.sections.size() == 1);
        EXPECT (d.sections[0].start == 0 && d.sections[0].end == kFrames);
        EXPECT (! chops::edits::removeSection (d, 0));   // never below one section

        chops::edits::autoSliceEqual (d, 8);
        EXPECT (d.sections.size() == 8);
        EXPECT (d.sections[7].end == kFrames);
        for (int i = 0; i < 8; ++i)
        {
            EXPECT (d.sections[(size_t) i].midiNote == 36 + i);
            if (i > 0)
                EXPECT (d.sections[(size_t) i].start == d.sections[(size_t) i - 1].end);
        }

        chops::edits::clearSlices (d);
        EXPECT (d.sections.size() == 1);
    }

    // --- transient auto-slice on an impulse train ---
    {
        auto impulse = std::make_shared<chops::SampleData>();
        impulse->buffer.setSize (1, kFrames);
        impulse->buffer.clear();
        // Decaying bursts at 0, 11025, 22050, 33075.
        for (int hit = 0; hit < 4; ++hit)
            for (int i = 0; i < 1000; ++i)
                impulse->buffer.setSample (0, hit * 11025 + i,
                                           (i % 7 < 4 ? 0.8f : -0.8f) * (1.0f - (float) i / 1000.0f));
        impulse->embeddedBlob.append ("x", 1);

        chops::Document d;
        d.sample = impulse;
        chops::edits::autoSliceTransients (d, 0.5f);

        EXPECT (d.sections.size() == 4);
        for (int hit = 1; hit < std::min (4, (int) d.sections.size()); ++hit)
            EXPECT (std::abs (d.sections[(size_t) hit].start - (juce::int64) hit * 11025) <= 512);
    }

    // --- LoopRun state machine on an identity ramp (sample[i] == i) ---
    // Catmull-Rom has linear precision and rate is 1, so every output frame
    // equals its expected source phase exactly.
    {
        auto ramp = std::make_shared<chops::SampleData>();
        ramp->buffer.setSize (1, kFrames);
        for (int i = 0; i < kFrames; ++i)
            ramp->buffer.setSample (0, i, (float) i);
        ramp->embeddedBlob.append ("x", 1);

        chops::Document d;
        d.sample = ramp;
        chops::edits::clearSlices (d);
        d.sections[0].mode = chops::PlayMode::LoopRun;
        EXPECT (chops::edits::setSectionLoop (d, 0, 1000, 2000));

        chops::Engine loopEngine;
        loopEngine.prepare (kRate, 512);
        loopEngine.publishDocument (std::make_unique<const chops::Document> (d));

        constexpr int kBlock = 512;
        constexpr int kNoteOffAt = 4096;
        std::vector<float> rendered;
        juce::AudioBuffer<float> block (1, kBlock);

        for (int blockStart = 0; blockStart < 8192; blockStart += kBlock)
        {
            block.clear();
            juce::MidiBuffer midi;
            if (blockStart == 0)
                addNoteOn (midi, 36, 0);
            if (blockStart == kNoteOffAt)
                addNoteOff (midi, 36, 0);
            loopEngine.process (block, midi);
            rendered.insert (rendered.end(), block.getReadPointer (0),
                             block.getReadPointer (0) + kBlock);
        }

        // Held: phase runs to the loop end, then wraps inside [1000, 2000).
        auto heldPhase = [] (int f) { return f < 2000 ? f : 1000 + (f - 1000) % 1000; };
        EXPECT (rendered[500] == (float) heldPhase (500));
        EXPECT (rendered[1999] == (float) heldPhase (1999));
        EXPECT (rendered[2000] == 1000.0f);   // first wrap
        EXPECT (rendered[2500] == 1500.0f);
        EXPECT (rendered[3999] == 1999.0f);

        // Released at 4096 with phase 1096: the pass in flight finishes and
        // playback runs straight through the loop end into the rest.
        const int phaseAtRelease = heldPhase (kNoteOffAt);
        EXPECT (phaseAtRelease == 1096);
        EXPECT (rendered[4500] == (float) (phaseAtRelease + (4500 - kNoteOffAt)));
        EXPECT (rendered[5100] == 2100.0f);   // past loopEnd: no wrap after release
        EXPECT (rendered[7000] == 4000.0f);   // deep into the tail
    }

    // --- loop crossfade blends toward the pre-loop material near the wrap ---
    {
        auto ramp = std::make_shared<chops::SampleData>();
        ramp->buffer.setSize (1, kFrames);
        for (int i = 0; i < kFrames; ++i)
            ramp->buffer.setSample (0, i, (float) i);
        ramp->embeddedBlob.append ("x", 1);

        chops::Document d;
        d.sample = ramp;
        chops::edits::clearSlices (d);
        d.sections[0].mode = chops::PlayMode::LoopRun;
        EXPECT (chops::edits::setSectionLoop (d, 0, 1000, 2000));
        d.sections[0].xfadeFrames = 64;

        chops::Engine xfEngine;
        xfEngine.prepare (kRate, 512);
        xfEngine.publishDocument (std::make_unique<const chops::Document> (d));

        std::vector<float> rendered;
        juce::AudioBuffer<float> block (1, 512);
        for (int blockStart = 0; blockStart < 2560; blockStart += 512)
        {
            block.clear();
            juce::MidiBuffer midi;
            if (blockStart == 0)
                addNoteOn (midi, 36, 0);
            xfEngine.process (block, midi);
            rendered.insert (rendered.end(), block.getReadPointer (0),
                             block.getReadPointer (0) + 512);
        }

        const int f = 1990;   // inside the 64-frame crossfade zone before 2000
        const float w = (float) (2000 - f) / 64.0f;
        const float expected = (float) f * w + (float) (f - 1000) * (1.0f - w);
        EXPECT (std::abs (rendered[(size_t) f] - expected) < 0.01f);
        EXPECT (std::abs (rendered[1900] - 1900.0f) < 1.0e-6f);   // outside the zone: untouched
    }

    // --- live loop tuning: loop edits land on the sounding note ---
    {
        auto ramp = std::make_shared<chops::SampleData>();
        ramp->buffer.setSize (1, kFrames);
        for (int i = 0; i < kFrames; ++i)
            ramp->buffer.setSample (0, i, (float) i);
        ramp->embeddedBlob.append ("x", 1);

        chops::Document d;
        d.sample = ramp;
        chops::edits::clearSlices (d);
        d.sections[0].mode = chops::PlayMode::LoopRun;
        EXPECT (chops::edits::setSectionLoop (d, 0, 1000, 2000));

        chops::Engine liveEngine;
        liveEngine.prepare (kRate, 512);
        liveEngine.publishDocument (std::make_unique<const chops::Document> (d));

        std::vector<float> rendered;
        juce::AudioBuffer<float> block (1, 512);
        const auto renderBlocks = [&] (int count, juce::MidiBuffer midi)
        {
            for (int b = 0; b < count; ++b)
            {
                block.clear();
                liveEngine.process (block, midi);
                midi.clear();
                rendered.insert (rendered.end(), block.getReadPointer (0),
                                 block.getReadPointer (0) + 512);
            }
        };

        juce::MidiBuffer noteOn;
        addNoteOn (noteOn, 36, 0);
        renderBlocks (1, noteOn);   // frames 0..511, still before the loop

        // Move the loop ahead of the phase while the note is held: playback
        // runs on and loops in the NEW region.
        chops::Document d2 (d);
        EXPECT (chops::edits::setSectionLoop (d2, 0, 3000, 3500));
        liveEngine.publishDocument (std::make_unique<const chops::Document> (d2));
        renderBlocks (9, {});       // through frame 5119

        EXPECT (rendered[4000] == 3000.0f);   // 3000 + (4000-3000) % 500
        EXPECT (rendered[4600] == 3100.0f);

        // Shrink the loop far below the current phase: O(1) fmod re-entry on
        // the next wrap check, then steady wrapping with the new period.
        chops::Document d3 (d2);
        EXPECT (chops::edits::setSectionLoop (d3, 0, 1000, 1200));
        liveEngine.publishDocument (std::make_unique<const chops::Document> (d3));
        renderBlocks (1, {});       // frames 5120..5631

        // Phase was 3120 entering the block: 1000 + (3120-1000) % 200 = 1120.
        EXPECT (rendered[5120] == 1120.0f);
        EXPECT (rendered[5150] == 1150.0f);
        EXPECT (rendered[5350] == 1150.0f);   // one 200-frame period later
    }

    // --- loop directions: Backward and PingPong on the identity ramp ---
    {
        const auto makeRampDoc = [&] (chops::LoopDirection dir)
        {
            auto ramp = std::make_shared<chops::SampleData>();
            ramp->buffer.setSize (1, kFrames);
            for (int i = 0; i < kFrames; ++i)
                ramp->buffer.setSample (0, i, (float) i);
            ramp->embeddedBlob.append ("x", 1);

            chops::Document d;
            d.sample = ramp;
            chops::edits::clearSlices (d);
            EXPECT (chops::edits::setSectionLoop (d, 0, 1000, 2000));
            EXPECT (chops::edits::setSectionLoopDirection (d, 0, dir));
            return d;
        };

        const auto renderHeld = [] (chops::Engine& engine, int frames, int noteOffAt)
        {
            std::vector<float> rendered;
            juce::AudioBuffer<float> block (1, 512);
            for (int blockStart = 0; blockStart < frames; blockStart += 512)
            {
                block.clear();
                juce::MidiBuffer midi;
                if (blockStart == 0)
                    addNoteOn (midi, 36, 0);
                if (blockStart == noteOffAt)
                    addNoteOff (midi, 36, 0);
                engine.process (block, midi);
                rendered.insert (rendered.end(), block.getReadPointer (0),
                                 block.getReadPointer (0) + 512);
            }
            return rendered;
        };

        {
            // Backward: forward into the loop, reflect at the end, then the
            // settled loop runs 2000 -> 1000 with a jump back to 1999.
            auto d = makeRampDoc (chops::LoopDirection::Backward);
            chops::Engine engine;
            engine.prepare (kRate, 512);
            engine.publishDocument (std::make_unique<const chops::Document> (d));

            const auto out = renderHeld (engine, 6656, 3072);
            EXPECT (out[1500] == 1500.0f);   // pre-loop, forward
            EXPECT (out[2000] == 2000.0f);   // reflection point
            EXPECT (out[2001] == 1999.0f);   // now travelling backward
            EXPECT (out[3000] == 1000.0f);   // reached loop start...
            EXPECT (out[3001] == 1999.0f);   // ...jump-cut back near the end

            // Note-off at 3072 (phase 1928, still moving backward): the pass
            // finishes to the loop start, bounces, and runs out forward.
            EXPECT (out[4000] == 1000.0f);
            EXPECT (out[4001] == 1001.0f);   // release bounce at the boundary
            EXPECT (out[5001] == 2001.0f);   // escaped the loop
            EXPECT (out[6000] == 3000.0f);   // deep into the tail
        }

        {
            // PingPong: bounce at both boundaries, no jump-cuts.
            auto d = makeRampDoc (chops::LoopDirection::PingPong);
            chops::Engine engine;
            engine.prepare (kRate, 512);
            engine.publishDocument (std::make_unique<const chops::Document> (d));

            const auto out = renderHeld (engine, 4608, -1);
            EXPECT (out[2000] == 2000.0f);   // first bounce
            EXPECT (out[2500] == 1500.0f);   // travelling backward
            EXPECT (out[3001] == 1001.0f);   // bounced forward at the start
            EXPECT (out[4000] == 2000.0f);   // full 2000-frame period
        }
    }

    // --- reverse playback ---
    {
        auto ramp = std::make_shared<chops::SampleData>();
        ramp->buffer.setSize (1, kFrames);
        for (int i = 0; i < kFrames; ++i)
            ramp->buffer.setSample (0, i, (float) i);
        ramp->embeddedBlob.append ("x", 1);

        chops::Document d;
        d.sample = ramp;
        chops::edits::clearSlices (d);
        d.sections[0].mode = chops::PlayMode::OneShot;
        EXPECT (chops::edits::setSectionReverse (d, 0, true));

        chops::Engine revEngine;
        revEngine.prepare (kRate, 512);
        revEngine.publishDocument (std::make_unique<const chops::Document> (d));

        juce::AudioBuffer<float> block (1, 512);
        block.clear();
        juce::MidiBuffer midi;
        addNoteOn (midi, 36, 0);
        revEngine.process (block, midi);
        EXPECT (block.getSample (0, 100) == (float) (kFrames - 1 - 100));
    }

    // --- lane edits: free ranges (overlap allowed), loop set/clear ---
    {
        chops::Document d;
        d.sample = sample;
        chops::edits::clearSlices (d);
        EXPECT (chops::edits::splitAt (d, 20000));

        // Free range move: section 0 now reaches deep into section 1 (overlap),
        // and no notes are remapped by range edits.
        EXPECT (chops::edits::setSectionRange (d, 0, 100, 30000));
        EXPECT (d.sections[0].start == 100 && d.sections[0].end == 30000);
        EXPECT (d.sections[1].start == 20000);   // neighbour untouched
        EXPECT (d.sections[0].midiNote == 36 && d.sections[1].midiNote == 37);

        EXPECT (! chops::edits::setSectionRange (d, 0, 100, 150));   // below minimum
        EXPECT (chops::edits::setSectionLoop (d, 0, 5000, 9000));
        EXPECT (d.sections[0].loopStart == 5000 && d.sections[0].loopEnd == 9000);
        EXPECT (d.sections[0].mode == chops::PlayMode::LoopRun);     // loop implies LoopRun
        EXPECT (chops::edits::setSectionLoop (d, 0, 50, 9000));      // clamps into section
        EXPECT (d.sections[0].loopStart == 100);
        EXPECT (! chops::edits::setSectionLoop (d, 0, 5000, 5010));  // too short
        EXPECT (chops::edits::clearSectionLoop (d, 0));
        EXPECT (! d.sections[0].hasLoop());
        EXPECT (d.sections[0].mode == chops::PlayMode::Gate);        // no loop -> Gate

        // Shrinking the range drops a loop that no longer fits — and the mode.
        EXPECT (chops::edits::setSectionLoop (d, 0, 25000, 29000));
        EXPECT (chops::edits::setSectionRange (d, 0, 100, 20000));
        EXPECT (! d.sections[0].hasLoop());
        EXPECT (d.sections[0].mode == chops::PlayMode::Gate);

        // LoopRun cannot be selected without a loop region.
        EXPECT (! chops::edits::setSectionMode (d, 0, chops::PlayMode::LoopRun));
        EXPECT (d.sections[0].mode == chops::PlayMode::Gate);
        EXPECT (chops::edits::setSectionLoop (d, 0, 5000, 9000));
        EXPECT (d.sections[0].mode == chops::PlayMode::LoopRun);
        EXPECT (chops::edits::setSectionMode (d, 0, chops::PlayMode::OneShot));
        EXPECT (chops::edits::setSectionMode (d, 0, chops::PlayMode::LoopRun));   // loop exists
    }

    // --- head trim: the single whole-file slice has an adjustable start ---
    {
        chops::Document d;
        d.sample = sample;
        chops::edits::clearSlices (d);

        EXPECT (chops::edits::moveSectionStart (d, 0, 5000));
        EXPECT (d.sections[0].start == 5000 && d.sections[0].end == kFrames);
        EXPECT (d.sections[0].midiNote == 36);

        EXPECT (chops::edits::moveSectionStart (d, 0, -100));   // clamps to 0
        EXPECT (d.sections[0].start == 0);
        EXPECT (chops::edits::moveSectionStart (d, 0, kFrames));   // clamps below end
        EXPECT (d.sections[0].start == kFrames - chops::edits::kMinSectionFrames);

        // With slices, marker 0 trims the head without touching neighbours.
        chops::edits::clearSlices (d);
        EXPECT (chops::edits::splitAt (d, 20000));
        EXPECT (chops::edits::moveSectionStart (d, 0, 3000));
        EXPECT (d.sections[0].start == 3000 && d.sections[0].end == 20000);
        EXPECT (d.sections[1].start == 20000);
    }

    // --- tail trim + crop: discard audio outside the first/last markers ---
    {
        chops::Document d;
        d.sample = sample;
        chops::edits::clearSlices (d);

        EXPECT (chops::edits::moveSectionEnd (d, 0, 30000));
        EXPECT (d.sections[0].end == 30000);
        EXPECT (chops::edits::moveSectionEnd (d, 0, kFrames + 999));   // clamps to file end
        EXPECT (d.sections[0].end == kFrames);
        EXPECT (chops::edits::moveSectionEnd (d, 0, 10));              // clamps above start
        EXPECT (d.sections[0].end == chops::edits::kMinSectionFrames);

        // Only the LAST section has a free end.
        chops::edits::clearSlices (d);
        EXPECT (chops::edits::splitAt (d, 20000));
        EXPECT (! chops::edits::moveSectionEnd (d, 0, 25000));
        EXPECT (chops::edits::moveSectionEnd (d, 1, 40000));

        // Crop: trim head and tail, keep a loop, then discard the rest.
        chops::edits::clearSlices (d);
        EXPECT (chops::edits::moveSectionStart (d, 0, 5000));
        EXPECT (chops::edits::moveSectionEnd (d, 0, 30000));
        EXPECT (chops::edits::setSectionLoop (d, 0, 10000, 20000));
        const auto oldBlobSize = d.sample->embeddedBlob.getSize();

        juce::String error;
        EXPECT (chops::state::cropToSections (d, error));
        EXPECT (d.sample->numFrames() == 25000);
        EXPECT (d.sample->buffer.getSample (0, 0) == kLeftValue);
        EXPECT (d.sections[0].start == 0 && d.sections[0].end == 25000);
        EXPECT (d.sections[0].loopStart == 5000 && d.sections[0].loopEnd == 15000);
        EXPECT (d.sample->embeddedBlob.getSize() < oldBlobSize);
        EXPECT (d.sample->embeddedFormat == "flac");

        // Nothing left to crop; state round-trips bit-exact after cropping.
        EXPECT (! chops::state::cropToSections (d, error));
        juce::MemoryBlock cropState;
        chops::state::toMemory (d, cropState);
        auto restoredCrop = chops::state::fromMemory (cropState.getData(), cropState.getSize(), error);
        EXPECT (restoredCrop != nullptr);
        if (restoredCrop != nullptr)
        {
            EXPECT (buffersIdentical (restoredCrop->sample->buffer, d.sample->buffer));
            EXPECT (restoredCrop->sections[0].loopStart == 5000);
        }
    }

    // --- polyphony UI snapshots: every playing voice is published ---
    {
        chops::Document d;
        d.sample = sample;
        chops::edits::clearSlices (d);
        EXPECT (chops::edits::splitAt (d, 20000));
        d.global.mono = false;   // this test needs slices sounding together

        chops::Engine polyEngine;
        polyEngine.prepare (kRate, 512);
        polyEngine.publishDocument (std::make_unique<const chops::Document> (d));

        juce::AudioBuffer<float> block (2, 512);
        block.clear();
        juce::MidiBuffer midi;
        addNoteOn (midi, 36, 0);
        addNoteOn (midi, 37, 100);
        polyEngine.process (block, midi);

        std::vector<int> playing;
        for (const auto& slot : polyEngine.uiVoices)
            if (const auto section = slot.section.load(); section >= 0)
                playing.push_back (section);

        std::sort (playing.begin(), playing.end());
        EXPECT (playing == (std::vector<int> { 0, 1 }));
        EXPECT (polyEngine.uiSectionIndex.load() == 1);       // last triggered
        EXPECT (polyEngine.uiTriggerSerial.load() == 2);

        // Sticky selection: retrigger the shorter section 0 (20000 frames vs
        // section 1's 24100), then run past its end while section 1 still
        // sounds. The selection must stay on the last-triggered slice — a
        // voice ending is not a trigger.
        juce::MidiBuffer retrig;
        addNoteOn (retrig, 36, 0);
        block.clear();
        polyEngine.process (block, retrig);
        EXPECT (polyEngine.uiSectionIndex.load() == 0);
        EXPECT (polyEngine.uiTriggerSerial.load() == 3);

        juce::MidiBuffer empty;
        for (int i = 0; i < 40; ++i)
        {
            block.clear();
            polyEngine.process (block, empty);
        }

        std::vector<int> remaining;
        for (const auto& slot : polyEngine.uiVoices)
            if (const auto section = slot.section.load(); section >= 0)
                remaining.push_back (section);

        EXPECT (remaining == (std::vector<int> { 1 }));       // section 0 ended
        EXPECT (polyEngine.uiSectionIndex.load() == 0);       // no jump back
        EXPECT (polyEngine.uiTriggerSerial.load() == 3);
    }

    // --- global mono (the default): one choke group across all slices ---
    {
        chops::Document d;
        d.sample = sample;
        chops::edits::clearSlices (d);
        EXPECT (chops::edits::splitAt (d, 20000));
        EXPECT (d.global.mono);

        chops::Engine monoEngine;
        monoEngine.prepare (kRate, 512);
        monoEngine.publishDocument (std::make_unique<const chops::Document> (d));

        juce::AudioBuffer<float> block (2, 512);
        block.clear();
        juce::MidiBuffer midi;
        addNoteOn (midi, 36, 0);
        addNoteOn (midi, 37, 100);
        monoEngine.process (block, midi);

        // Note 37 chokes note 36 even though it is a different slice. Past the
        // 2 ms fade and the attack ramp exactly one voice sounds at full level
        // (both slices play the same constant sample, so doubling would read
        // 2x).
        EXPECT (block.getSample (0, 400) == kLeftValue);
        EXPECT (block.getSample (1, 400) == kRightValue);

        std::vector<int> playing;
        for (const auto& slot : monoEngine.uiVoices)
            if (const auto section = slot.section.load(); section >= 0)
                playing.push_back (section);
        EXPECT (playing == (std::vector<int> { 1 }));         // section 0 choked

        // Poly setting survives the state round trip.
        d.global.mono = false;
        juce::MemoryBlock polyState;
        chops::state::toMemory (d, polyState);
        auto restoredPoly = chops::state::fromMemory (polyState.getData(), polyState.getSize(), error);
        EXPECT (restoredPoly != nullptr);
        if (restoredPoly != nullptr)
            EXPECT (! restoredPoly->global.mono);
    }

    // --- gap healing: states saved before boundaries became main-wave-only
    // can hold sections with gaps (e.g. a shortened slice); every partition
    // edit must heal them instead of getting stuck ---
    {
        // Reproduce the reported bug: slice "thinks" it is shorter than it
        // is, leaving an ignored region before the next slice.
        chops::Document d;
        d.sample = sample;
        chops::edits::clearSlices (d);
        EXPECT (chops::edits::splitAt (d, 20000));
        EXPECT (chops::edits::setSectionRange (d, 0, 0, 10000));   // gap: [10000, 20000)

        // Clicking in the ignored region adds a marker there: the shortened
        // slice extends to the click and a new slice covers the rest of the gap.
        EXPECT (chops::edits::splitAt (d, 15000));
        EXPECT (d.sections.size() == 3);
        EXPECT (d.sections[0].start == 0 && d.sections[0].end == 15000);
        EXPECT (d.sections[1].start == 15000 && d.sections[1].end == 20000);
        EXPECT (d.sections[2].start == 20000 && d.sections[2].end == kFrames);
        for (int i = 0; i < 3; ++i)
            EXPECT (d.sections[(size_t) i].midiNote == 36 + i);
    }

    {
        // Dragging the next slice's marker heals the boundary unconditionally.
        chops::Document d;
        d.sample = sample;
        chops::edits::clearSlices (d);
        EXPECT (chops::edits::splitAt (d, 20000));
        EXPECT (chops::edits::setSectionRange (d, 0, 0, 10000));   // gap: [10000, 20000)

        EXPECT (chops::edits::moveSectionStart (d, 1, 12000));
        EXPECT (d.sections[0].end == 12000 && d.sections[1].start == 12000);

        // Removing a slice absorbs the removed range plus any gap before it.
        EXPECT (chops::edits::setSectionRange (d, 0, 0, 5000));    // new gap: [5000, 12000)
        EXPECT (chops::edits::removeSection (d, 1));
        EXPECT (d.sections.size() == 1);
        EXPECT (d.sections[0].start == 0 && d.sections[0].end == kFrames);
    }

    {
        // Gap at the file head: clicking before the first slice creates one.
        chops::Document d;
        d.sample = sample;
        chops::edits::clearSlices (d);
        EXPECT (chops::edits::setSectionRange (d, 0, 30000, kFrames));
        EXPECT (chops::edits::splitAt (d, 2000));
        EXPECT (d.sections.size() == 2);
        EXPECT (d.sections[0].start == 2000 && d.sections[0].end == 30000);
        EXPECT (d.sections[0].midiNote == 36 && d.sections[1].midiNote == 37);

        // Gap at the file tail: the last slice extends to the click and a new
        // slice runs to the end of the file.
        EXPECT (chops::edits::setSectionRange (d, 1, 30000, 40000));
        EXPECT (chops::edits::splitAt (d, 42000));
        EXPECT (d.sections.size() == 3);
        EXPECT (d.sections[1].end == 42000);
        EXPECT (d.sections[2].start == 42000 && d.sections[2].end == kFrames);
    }

    // --- lo-fi DSP: sample-and-hold decimation, drive shaper, live update ---
    {
        auto ramp = std::make_shared<chops::SampleData>();
        ramp->buffer.setSize (1, kFrames);
        for (int i = 0; i < kFrames; ++i)
            ramp->buffer.setSample (0, i, (float) i);
        ramp->embeddedBlob.append ("x", 1);

        // Section 0 overrides SR to a quarter of the host rate; global is clean.
        chops::Document d;
        d.sample = ramp;
        chops::edits::clearSlices (d);
        d.sections[0].mode = chops::PlayMode::OneShot;
        EXPECT (chops::edits::setSectionSrOverride (d, 0, kRate / 4.0));

        chops::Engine fxEngine;
        fxEngine.prepare (kRate, 512);
        fxEngine.publishDocument (std::make_unique<const chops::Document> (d));

        juce::AudioBuffer<float> block (1, 512);
        block.clear();
        juce::MidiBuffer midi;
        addNoteOn (midi, 36, 0);
        fxEngine.process (block, midi);

        // Hold ticks every 4 frames: 40..43 hold ramp(40), 44 jumps to ramp(44).
        EXPECT (block.getSample (0, 40) == 40.0f);
        EXPECT (block.getSample (0, 41) == 40.0f);
        EXPECT (block.getSample (0, 43) == 40.0f);
        EXPECT (block.getSample (0, 44) == 44.0f);

        // Live update: switch the override off mid-note -> clean playback resumes.
        chops::Document d2 (d);
        EXPECT (chops::edits::setSectionSrOverride (d2, 0, 0.0));
        fxEngine.publishDocument (std::make_unique<const chops::Document> (d2));

        block.clear();
        juce::MidiBuffer noMidi;
        fxEngine.process (block, noMidi);
        EXPECT (block.getSample (0, 100) == 612.0f);   // 512 + 100, no holds
    }

    {
        // Drive: DC 0.5 through tanh(g*x)/tanh(g) at drive 1 (g = 8).
        auto dc = std::make_shared<chops::SampleData>();
        dc->buffer.setSize (1, kFrames);
        for (int i = 0; i < kFrames; ++i)
            dc->buffer.setSample (0, i, 0.5f);
        dc->embeddedBlob.append ("x", 1);

        chops::Document d;
        d.sample = dc;
        d.global.drive = 1.0f;
        chops::edits::clearSlices (d);

        chops::Engine driveEngine;
        driveEngine.prepare (kRate, 512);
        driveEngine.publishDocument (std::make_unique<const chops::Document> (d));

        juce::AudioBuffer<float> block (1, 512);
        block.clear();
        juce::MidiBuffer midi;
        addNoteOn (midi, 36, 0);
        driveEngine.process (block, midi);

        const float expected = std::tanh (8.0f * 0.5f) / std::tanh (8.0f);
        EXPECT (std::abs (block.getSample (0, 100) - expected) < 1.0e-5f);
    }

    wavFile.deleteFile();

    if (failures == 0)
        std::printf ("all tests passed\n");
    else
        std::printf ("%d failure(s)\n", failures);

    return failures == 0 ? 0 : 1;
}
