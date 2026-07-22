#include "WaveDisplay.h"

#include "Knobs.h"
#include "WaveRender.h"

namespace chops
{

static const juce::Colour kPanel { 0xff22242a };
static const juce::Colour kWave { 0xff5ec8a8 };
static const juce::Colour kMarker { 0xffd9a05a };
static const juce::Colour kEndMarker { 0xff7ab8ff };
static const juce::Colour kPlayhead { 0xffffffff };
static const juce::Colour kSelectedBg { 0x14ffffff };   // lift over panel + tints

void WaveDisplay::setDocument (std::shared_ptr<const Document> newDoc, const PeakCache* newPeaks)
{
    const bool sampleChanged = (doc == nullptr || newDoc == nullptr
                                || doc->sample != newDoc->sample);
    doc = std::move (newDoc);
    peaks = newPeaks;

    if (sampleChanged)
    {
        viewStart = 0.0;
        viewLength = doc != nullptr && doc->sample != nullptr
                         ? (double) doc->sample->numFrames()
                         : 0.0;
    }

    repaint();
}

void WaveDisplay::setPlayheads (const std::vector<std::pair<int, double>>& voices)
{
    if (voices == playheads)
        return;

    playheads = voices;
    repaint();
}

void WaveDisplay::setSelectedSection (int index)
{
    if (index == selectedSection)
        return;

    selectedSection = index;
    repaint();
}

double WaveDisplay::frameToX (double frame) const
{
    return (frame - viewStart) / viewLength * (double) getWidth();
}

juce::int64 WaveDisplay::xToFrame (double x) const
{
    return (juce::int64) (viewStart + x / (double) getWidth() * viewLength);
}

void WaveDisplay::clampView()
{
    if (doc == nullptr || doc->sample == nullptr)
        return;

    const auto total = (double) doc->sample->numFrames();
    viewLength = std::clamp (viewLength, kMinViewFrames, total);
    viewStart = std::clamp (viewStart, 0.0, total - viewLength);
}

void WaveDisplay::zoomAround (double frame, double factor)
{
    if (doc == nullptr || doc->sample == nullptr || factor <= 0.0)
        return;

    const double anchor = (frame - viewStart) / viewLength;
    viewLength /= factor;
    clampView();
    viewStart = frame - anchor * viewLength;
    clampView();
    repaint();
}

int WaveDisplay::endMarkerIndex() const
{
    return doc != nullptr ? (int) doc->sections.size() : -1;
}

int WaveDisplay::hitMarker (juce::Point<int> pos) const
{
    if (doc == nullptr || doc->sections.empty())
        return -1;

    int best = -1;
    double bestDistance = kHandleHitPx + 1.0;

    for (int i = 0; i < (int) doc->sections.size(); ++i)
    {
        const auto x = frameToX ((double) doc->sections[(size_t) i].start);
        const auto distance = std::abs (x - (double) pos.x);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }

    const auto endX = frameToX ((double) doc->sections.back().end);
    if (std::abs (endX - (double) pos.x) < bestDistance)
        best = endMarkerIndex();

    return best;
}

void WaveDisplay::paint (juce::Graphics& g)
{
    g.fillAll (kPanel);

    if (doc == nullptr || doc->sample == nullptr || peaks == nullptr)
    {
        g.setColour (juce::Colours::whitesmoke.withAlpha (0.6f));
        g.setFont (ui::kFontHint);
        g.drawText ("drop an audio file", getLocalBounds(), juce::Justification::centred);
        return;
    }

    const auto& buffer = doc->sample->buffer;
    const auto bounds = getLocalBounds().toFloat();
    const int width = getWidth();

    // Playing section highlights: every currently sounding slice, so
    // polyphonic playback is fully visible.
    g.setColour (kWave.withAlpha (0.14f));
    for (const auto& [section, frame] : playheads)
    {
        juce::ignoreUnused (frame);
        if (section < 0 || section >= (int) doc->sections.size())
            continue;

        const auto& sec = doc->sections[(size_t) section];
        const auto x1 = (float) std::max (0.0, frameToX ((double) sec.start));
        const auto x2 = (float) std::min ((double) width, frameToX ((double) sec.end));
        if (x2 > x1)
            g.fillRect (x1, 0.0f, x2 - x1, bounds.getHeight());
    }

    // Selected slice: neutral lift drawn over the playing tint, so idle it
    // reads as a lighter background and while sounding it stays visually
    // distinct from the other playing slices.
    if (selectedSection >= 0 && selectedSection < (int) doc->sections.size())
    {
        const auto& sec = doc->sections[(size_t) selectedSection];
        const auto x1 = (float) std::max (0.0, frameToX ((double) sec.start));
        const auto x2 = (float) std::min ((double) width, frameToX ((double) sec.end));
        if (x2 > x1)
        {
            g.setColour (kSelectedBg);
            g.fillRect (x1, 0.0f, x2 - x1, bounds.getHeight());
        }
    }

    render::drawWave (g, bounds, buffer, *peaks, viewStart, viewLength, kWave);

    // Slice markers with grab handles; marker 0 doubles as the head trim.
    for (int i = 0; i < (int) doc->sections.size(); ++i)
    {
        const auto x = (float) frameToX ((double) doc->sections[(size_t) i].start);
        if (x < -kHandleHitPx || x > (float) width + kHandleHitPx)
            continue;

        g.setColour (i == dragIndex ? kMarker.brighter (0.4f) : kMarker);
        g.fillRect (x - 0.75f, 0.0f, 1.5f, bounds.getHeight());

        juce::Path handle;
        handle.addTriangle (x - 6.0f, 0.0f, x + 6.0f, 0.0f, x, 10.0f);
        g.fillPath (handle);
    }

    // Tail-trim end marker (the last slice's end), reset by double-click.
    {
        const auto x = (float) frameToX ((double) doc->sections.back().end);
        if (x >= -kHandleHitPx && x <= (float) width + kHandleHitPx)
        {
            g.setColour (dragIndex == endMarkerIndex() ? kEndMarker.brighter (0.4f) : kEndMarker);
            g.fillRect (x - 0.75f, 0.0f, 1.5f, bounds.getHeight());
            juce::Path handle;
            handle.addTriangle (x - 6.0f, 0.0f, x + 6.0f, 0.0f, x, 10.0f);
            g.fillPath (handle);
        }
    }

    g.setColour (kPlayhead.withAlpha (0.85f));
    for (const auto& [section, frame] : playheads)
    {
        juce::ignoreUnused (section);
        if (frame < 0.0)
            continue;

        const auto x = (float) frameToX (frame);
        if (x >= 0.0f && x <= (float) width)
            g.fillRect (x, 0.0f, 1.5f, bounds.getHeight());
    }
}

void WaveDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragIndex = hitMarker (e.getPosition());
    didDrag = false;
    repaint();
}

void WaveDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragIndex >= 0)
    {
        didDrag = true;
        if (dragIndex == endMarkerIndex())
        {
            if (onMoveEnd != nullptr)
                onMoveEnd (xToFrame ((double) e.position.x));
        }
        else if (onMoveStart != nullptr)
        {
            onMoveStart (dragIndex, xToFrame ((double) e.position.x));
        }
    }
}

void WaveDisplay::mouseUp (const juce::MouseEvent& e)
{
    // A plain click on empty wave adds a slice; clicks on handles don't.
    if (dragIndex < 0 && ! didDrag && ! e.mouseWasDraggedSinceMouseDown()
        && e.getNumberOfClicks() == 1 && onSplit != nullptr
        && getLocalBounds().contains (e.getPosition()))
    {
        onSplit (xToFrame ((double) e.position.x));
    }

    dragIndex = -1;
    repaint();
}

void WaveDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto index = hitMarker (e.getPosition());
    if (index < 0)
        return;

    if (index == endMarkerIndex())
    {
        // Reset the tail trim to the end of the file.
        if (onMoveEnd != nullptr && doc != nullptr && doc->sample != nullptr)
            onMoveEnd (doc->sample->numFrames());
    }
    else if (onRemove != nullptr)
    {
        onRemove (index);
    }
}

void WaveDisplay::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (hitMarker (e.getPosition()) >= 0
                        ? juce::MouseCursor::LeftRightResizeCursor
                        : juce::MouseCursor::NormalCursor);
}

void WaveDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (doc == nullptr || doc->sample == nullptr)
        return;

    if (std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
    {
        viewStart -= (double) wheel.deltaX * viewLength * 0.8;
        clampView();
        repaint();
    }
    else if (! juce::approximatelyEqual (wheel.deltaY, 0.0f))
    {
        zoomAround ((double) xToFrame ((double) e.position.x),
                    std::exp ((double) wheel.deltaY * 2.5));
    }
}

void WaveDisplay::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    zoomAround ((double) xToFrame ((double) e.position.x), (double) scaleFactor);
}

} // namespace chops
