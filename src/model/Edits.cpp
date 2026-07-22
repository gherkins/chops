#include "Edits.h"

#include <algorithm>
#include <cmath>

namespace chops::edits
{

static void sanitizeLoop (Section& s)
{
    if (s.hasLoop() && (s.loopStart < s.start || s.loopEnd > s.end))
    {
        s.loopStart = -1;
        s.loopEnd = -1;
    }
}

void remapNotesChromatic (Document& doc)
{
    std::stable_sort (doc.sections.begin(), doc.sections.end(),
                      [] (const Section& a, const Section& b) { return a.start < b.start; });

    for (int i = 0; i < (int) doc.sections.size(); ++i)
        doc.sections[(size_t) i].midiNote = doc.global.rootNote + i;
}

bool splitAt (Document& doc, juce::int64 frame)
{
    if (doc.sample == nullptr)
        return false;

    for (size_t i = 0; i < doc.sections.size(); ++i)
    {
        auto& sec = doc.sections[i];
        if (frame >= sec.start + kMinSectionFrames && frame <= sec.end - kMinSectionFrames)
        {
            Section right = sec;   // inherits mode, pitch, fx, ...
            right.start = frame;
            sec.end = frame;
            sanitizeLoop (sec);
            sanitizeLoop (right);
            doc.sections.insert (doc.sections.begin() + (long) i + 1, right);
            remapNotesChromatic (doc);
            return true;
        }
    }

    return false;
}

bool moveSectionStart (Document& doc, int index, juce::int64 newStart)
{
    if (index < 1 || index >= (int) doc.sections.size())
        return false;

    auto& sec = doc.sections[(size_t) index];
    auto& prev = doc.sections[(size_t) index - 1];

    const auto lo = prev.start + kMinSectionFrames;
    const auto hi = sec.end - kMinSectionFrames;
    if (lo > hi)
        return false;

    newStart = std::clamp (newStart, lo, hi);

    if (prev.end == sec.start)   // linked partition boundary
    {
        prev.end = newStart;
        sanitizeLoop (prev);
    }

    sec.start = newStart;
    sanitizeLoop (sec);
    remapNotesChromatic (doc);
    return true;
}

bool removeSection (Document& doc, int index)
{
    if (index < 0 || index >= (int) doc.sections.size() || doc.sections.size() <= 1)
        return false;

    const auto removed = doc.sections[(size_t) index];

    if (index > 0)
    {
        auto& prev = doc.sections[(size_t) index - 1];
        if (prev.end == removed.start)
            prev.end = removed.end;
    }

    doc.sections.erase (doc.sections.begin() + index);
    remapNotesChromatic (doc);
    return true;
}

void clearSlices (Document& doc)
{
    if (doc.sample == nullptr)
        return;

    doc.sections = { makeWholeFileSection (*doc.sample, doc.global.rootNote) };
}

static std::vector<juce::int64> partitionBounds (juce::int64 numFrames,
                                                 const std::vector<juce::int64>& innerBounds)
{
    std::vector<juce::int64> bounds { 0 };
    for (auto b : innerBounds)
        if (b - bounds.back() >= kMinSectionFrames && numFrames - b >= kMinSectionFrames)
            bounds.push_back (b);
    bounds.push_back (numFrames);
    return bounds;
}

static void rebuildFromBounds (Document& doc, const std::vector<juce::int64>& bounds)
{
    doc.sections.clear();
    for (size_t i = 0; i + 1 < bounds.size(); ++i)
    {
        Section s;
        s.start = bounds[i];
        s.end = bounds[i + 1];
        doc.sections.push_back (s);
    }
    remapNotesChromatic (doc);
}

void autoSliceEqual (Document& doc, int parts)
{
    if (doc.sample == nullptr || parts < 1)
        return;

    const auto n = doc.sample->numFrames();
    std::vector<juce::int64> inner;
    for (int i = 1; i < parts; ++i)
        inner.push_back (n * i / parts);

    rebuildFromBounds (doc, partitionBounds (n, inner));
}

void autoSliceTransients (Document& doc, float sensitivity)
{
    if (doc.sample == nullptr)
        return;

    const auto& buffer = doc.sample->buffer;
    const auto n = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    constexpr int kHop = 256;

    // RMS energy per hop over a mono mix.
    std::vector<float> rms;
    rms.reserve ((size_t) (n / kHop + 1));

    for (int start = 0; start < n; start += kHop)
    {
        const int end = std::min (n, start + kHop);
        float sum = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* data = buffer.getReadPointer (ch);
            for (int i = start; i < end; ++i)
                sum += data[i] * data[i];
        }
        rms.push_back (std::sqrt (sum / (float) ((end - start) * numChannels)));
    }

    // Positive energy flux, thresholded against its mean. Sensitivity scales
    // the threshold: 0 -> conservative, 1 -> eager.
    std::vector<float> flux (rms.size(), 0.0f);
    float fluxSum = 0.0f;
    for (size_t k = 1; k < rms.size(); ++k)
    {
        flux[k] = std::max (0.0f, rms[k] - rms[k - 1]);
        fluxSum += flux[k];
    }

    if (rms.size() < 2 || fluxSum <= 0.0f)
        return;

    const float mean = fluxSum / (float) (flux.size() - 1);
    const float threshold = mean * (6.0f - 5.0f * std::clamp (sensitivity, 0.0f, 1.0f));
    const juce::int64 minGap = 2048;

    std::vector<juce::int64> onsets;
    for (size_t k = 1; k + 1 < flux.size(); ++k)
    {
        if (flux[k] > threshold && flux[k] >= flux[k - 1] && flux[k] >= flux[k + 1])
        {
            // Backtrack to the quietest hop just before the rise.
            size_t at = k;
            while (at > 1 && rms[at - 1] < rms[at])
                --at;

            const auto frame = (juce::int64) at * kHop;
            if (onsets.empty() || frame - onsets.back() >= minGap)
                onsets.push_back (frame);
        }
    }

    rebuildFromBounds (doc, partitionBounds (n, onsets));
}

} // namespace chops::edits
