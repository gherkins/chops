#include "State.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>

#include "../model/Edits.h"

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

// FLAC-encode the buffer into the embedded blob. 16-bit sources stay
// bit-exact; float sources are stored at 24 bit (~-144 dB error, inaudible),
// and every save/load cycle afterwards is bit-exact.
static bool encodeFlacBlob (SampleData& sd)
{
    sd.embeddedBlob.reset();
    const int numChannels = sd.buffer.getNumChannels();
    if (numChannels < 1 || numChannels > 8)
        return false;

    const int bits = sd.sourceBitDepth <= 16 ? 16 : 24;
    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::OutputStream> stream =
        std::make_unique<juce::MemoryOutputStream> (sd.embeddedBlob, false);

    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate (sd.sourceSampleRate)
                             .withNumChannels (numChannels)
                             .withBitsPerSample (bits)
                             .withQualityOptionIndex (5);

    bool ok = false;
    if (auto writer = flac.createWriterFor (stream, options))
        ok = writer->writeFromAudioSampleBuffer (sd.buffer, 0, sd.buffer.getNumSamples());
    // The writer is destroyed (and has flushed) before the blob is inspected.

    if (ok)
        sd.embeddedFormat = "flac";
    else
        sd.embeddedBlob.reset();
    return ok;
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

    if (sd->embeddedBlob.isEmpty())
        encodeFlacBlob (*sd);

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
    t.setProperty ("loopDir", (int) s.loopDir, nullptr);
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
    s.loopDir = (LoopDirection) (int) t.getProperty ("loopDir", (int) LoopDirection::Forward);
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
    root.setProperty ("mono", g.mono, nullptr);

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
    g.mono = root.getProperty ("mono", true);

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

bool cropToSections (Document& doc, juce::String& error)
{
    if (doc.sample == nullptr || doc.sections.empty())
    {
        error = "Nothing to crop";
        return false;
    }

    const auto total = doc.sample->numFrames();
    juce::int64 lo = total, hi = 0;
    for (const auto& sec : doc.sections)
    {
        lo = std::min (lo, sec.start);
        hi = std::max (hi, sec.end);
    }
    lo = std::clamp<juce::int64> (lo, 0, total);
    hi = std::clamp<juce::int64> (hi, 0, total);

    if (lo <= 0 && hi >= total)
        return false;   // nothing outside the slices

    if (hi - lo < edits::kMinSectionFrames)
    {
        error = "Crop range is too short";
        return false;
    }

    const auto& source = *doc.sample;
    auto cropped = std::make_shared<SampleData>();
    const int numChannels = source.buffer.getNumChannels();
    const int newFrames = (int) (hi - lo);

    cropped->buffer.setSize (numChannels, newFrames);
    for (int ch = 0; ch < numChannels; ++ch)
        cropped->buffer.copyFrom (ch, 0, source.buffer, ch, (int) lo, newFrames);

    cropped->sourceSampleRate = source.sourceSampleRate;
    cropped->sourceBitDepth = source.sourceBitDepth;
    cropped->originalPath = source.originalPath;

    if (! encodeFlacBlob (*cropped))
    {
        error = "Could not re-encode the cropped sample";
        return false;
    }

    doc.sample = std::move (cropped);
    for (auto& sec : doc.sections)
    {
        sec.start = std::clamp<juce::int64> (sec.start - lo, 0, newFrames);
        sec.end = std::clamp<juce::int64> (sec.end - lo, 0, newFrames);
        if (sec.hasLoop())
        {
            sec.loopStart -= lo;
            sec.loopEnd -= lo;
        }
    }

    return true;
}

} // namespace chops::state
