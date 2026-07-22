#include "State.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>

namespace chops::state
{

// AudioBuffer frame counts are int, and a performance sampler has no business
// holding hours of audio anyway.
static constexpr juce::int64 kMaxFrames = std::numeric_limits<int>::max();

static bool isCompactFormat (const juce::String& extension)
{
    static const juce::StringArray compact { "flac", "ogg", "oga", "mp3", "m4a", "aac", "opus" };
    return compact.contains (extension);
}

std::shared_ptr<SampleData> loadSampleFile (const juce::File& file, juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Unsupported or unreadable audio file: " + file.getFileName();
        return nullptr;
    }

    if (reader->lengthInSamples <= 0 || reader->lengthInSamples > kMaxFrames)
    {
        error = "Sample is empty or too long: " + file.getFileName();
        return nullptr;
    }

    auto sd = std::make_shared<SampleData>();
    const auto numFrames = (int) reader->lengthInSamples;
    const auto numChannels = (int) reader->numChannels;

    sd->buffer.setSize (numChannels, numFrames);
    reader->read (&sd->buffer, 0, numFrames, 0, true, true);
    sd->sourceSampleRate = reader->sampleRate;
    sd->sourceBitDepth = reader->usesFloatingPointData ? 32 : (int) reader->bitsPerSample;
    sd->originalPath = file.getFullPathName();

    const auto ext = file.getFileExtension().toLowerCase().trimCharactersAtStart (".");

    if (isCompactFormat (ext))
    {
        // Already compressed: embed the original bytes verbatim, zero loss.
        if (file.loadFileAsData (sd->embeddedBlob))
            sd->embeddedFormat = ext;
    }

    if (sd->embeddedBlob.isEmpty() && numChannels <= 8)
    {
        // PCM source: FLAC-encode once. 16-bit sources stay bit-exact; float
        // sources are stored at 24 bit (~-144 dB error, inaudible), and every
        // save/load cycle after this one is bit-exact.
        const int bits = sd->sourceBitDepth <= 16 ? 16 : 24;
        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::OutputStream> stream =
            std::make_unique<juce::MemoryOutputStream> (sd->embeddedBlob, false);

        const auto options = juce::AudioFormatWriterOptions{}
                                 .withSampleRate (sd->sourceSampleRate)
                                 .withNumChannels (numChannels)
                                 .withBitsPerSample (bits)
                                 .withQualityOptionIndex (5);

        bool flacOk = false;
        if (auto writer = flac.createWriterFor (stream, options))
            flacOk = writer->writeFromAudioSampleBuffer (sd->buffer, 0, numFrames);
        // The writer is destroyed (and has flushed) before the blob is inspected.

        if (flacOk)
            sd->embeddedFormat = "flac";
        else
            sd->embeddedBlob.reset();
    }

    if (sd->embeddedBlob.isEmpty())
    {
        // Last resort so persistence never silently fails: raw file bytes.
        if (! file.loadFileAsData (sd->embeddedBlob))
        {
            error = "Could not build embedded copy of " + file.getFileName();
            return nullptr;
        }
        sd->embeddedFormat = ext;
    }

    return sd;
}

static void writeSection (juce::ValueTree& parent, const Section& s)
{
    juce::ValueTree t ("Section");
    t.setProperty ("start", s.start, nullptr);
    t.setProperty ("end", s.end, nullptr);
    t.setProperty ("loopStart", s.loopStart, nullptr);
    t.setProperty ("loopEnd", s.loopEnd, nullptr);
    t.setProperty ("mode", (int) s.mode, nullptr);
    t.setProperty ("reverse", s.reverse, nullptr);
    t.setProperty ("pitchSemis", s.pitchSemis, nullptr);
    t.setProperty ("fineCents", s.fineCents, nullptr);
    t.setProperty ("gain", s.gain, nullptr);
    t.setProperty ("srOverride", s.srOverride, nullptr);
    t.setProperty ("driveOverride", s.driveOverride, nullptr);
    t.setProperty ("xfadeFrames", s.xfadeFrames, nullptr);
    t.setProperty ("midiNote", s.midiNote, nullptr);
    parent.appendChild (t, nullptr);
}

static Section readSection (const juce::ValueTree& t)
{
    Section s;
    s.start = t.getProperty ("start", 0);
    s.end = t.getProperty ("end", 0);
    s.loopStart = t.getProperty ("loopStart", -1);
    s.loopEnd = t.getProperty ("loopEnd", -1);
    s.mode = (PlayMode) (int) t.getProperty ("mode", (int) PlayMode::Gate);
    s.reverse = t.getProperty ("reverse", false);
    s.pitchSemis = t.getProperty ("pitchSemis", 0);
    s.fineCents = t.getProperty ("fineCents", 0.0f);
    s.gain = t.getProperty ("gain", 1.0f);
    s.srOverride = t.getProperty ("srOverride", 0.0);
    s.driveOverride = t.getProperty ("driveOverride", -1.0f);
    s.xfadeFrames = t.getProperty ("xfadeFrames", 0);
    s.midiNote = t.getProperty ("midiNote", 36);
    return s;
}

void toMemory (const Document& doc, juce::MemoryBlock& dest)
{
    juce::ValueTree root ("Chops");
    root.setProperty ("version", 1, nullptr);

    const auto& g = doc.global;
    root.setProperty ("srReduce", g.srReduce, nullptr);
    root.setProperty ("drive", g.drive, nullptr);
    root.setProperty ("pitchSemis", g.pitchSemis, nullptr);
    root.setProperty ("fineCents", g.fineCents, nullptr);
    root.setProperty ("gain", g.gain, nullptr);
    root.setProperty ("rootNote", g.rootNote, nullptr);

    if (doc.sample != nullptr)
    {
        const auto& smp = *doc.sample;
        juce::ValueTree s ("Sample");
        s.setProperty ("sourceSampleRate", smp.sourceSampleRate, nullptr);
        s.setProperty ("sourceBitDepth", smp.sourceBitDepth, nullptr);
        s.setProperty ("embeddedFormat", smp.embeddedFormat, nullptr);
        s.setProperty ("originalPath", smp.originalPath, nullptr);
        s.setProperty ("blob", smp.embeddedBlob, nullptr);
        root.appendChild (s, nullptr);
    }

    juce::ValueTree sections ("Sections");
    for (const auto& sec : doc.sections)
        writeSection (sections, sec);
    root.appendChild (sections, nullptr);

    dest.reset();
    juce::MemoryOutputStream out (dest, false);
    root.writeToStream (out);
}

std::unique_ptr<Document> fromMemory (const void* data, size_t size, juce::String& error)
{
    auto root = juce::ValueTree::readFromData (data, size);
    if (! root.isValid() || ! root.hasType ("Chops"))
    {
        error = "Unrecognised state chunk";
        return nullptr;
    }

    auto doc = std::make_unique<Document>();
    auto& g = doc->global;
    g.srReduce = root.getProperty ("srReduce", 0.0);
    g.drive = root.getProperty ("drive", 0.0f);
    g.pitchSemis = root.getProperty ("pitchSemis", 0);
    g.fineCents = root.getProperty ("fineCents", 0.0f);
    g.gain = root.getProperty ("gain", 1.0f);
    g.rootNote = root.getProperty ("rootNote", 36);

    if (auto s = root.getChildWithName ("Sample"); s.isValid())
    {
        auto sd = std::make_shared<SampleData>();
        sd->sourceBitDepth = s.getProperty ("sourceBitDepth", 24);
        sd->embeddedFormat = s.getProperty ("embeddedFormat", juce::String()).toString();
        sd->originalPath = s.getProperty ("originalPath", juce::String()).toString();

        if (const auto* blob = s.getProperty ("blob").getBinaryData())
            sd->embeddedBlob = *blob;

        if (sd->embeddedBlob.isEmpty())
        {
            error = "State chunk has no embedded sample data";
            return nullptr;
        }

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (
            std::make_unique<juce::MemoryInputStream> (sd->embeddedBlob, false)));

        if (reader == nullptr || reader->lengthInSamples <= 0
            || reader->lengthInSamples > kMaxFrames)
        {
            error = "Could not decode embedded sample";
            return nullptr;
        }

        const auto numFrames = (int) reader->lengthInSamples;
        sd->buffer.setSize ((int) reader->numChannels, numFrames);
        reader->read (&sd->buffer, 0, numFrames, 0, true, true);
        sd->sourceSampleRate = reader->sampleRate;

        doc->sample = std::move (sd);
    }

    for (auto sec : root.getChildWithName ("Sections"))
        if (sec.hasType ("Section"))
            doc->sections.push_back (readSection (sec));

    if (doc->sample != nullptr && doc->sections.empty())
        doc->sections.push_back (makeWholeFileSection (*doc->sample, g.rootNote));

    return doc;
}

} // namespace chops::state
