#include "WaveDisplay.h"

#include "WaveRender.h"

namespace chops
{

static const juce::Colour kPanel { 0xff22242a };
static const juce::Colour kWave { 0xff5ec8a8 };
static const juce::Colour kMarker { 0xffd9a05a };
static const juce::Colour kPlayhead { 0xffffffff };

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

void WaveDisplay::setPlayhead (int sectionIndex, double frame)
{
    if (sectionIndex == playSection && juce::approximatelyEqual (frame, playFrame))
        return;

    playSection = sectionIndex;
    playFrame = frame;
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

int WaveDisplay::hitMarker (juce::Point<int> pos) const
{
    if (doc == nullptr)
        return -1;

    int best = -1;
    double bestDistance = kHandleHitPx + 1.0;

    for (int i = 1; i < (int) doc->sections.size(); ++i)
    {
        const auto x = frameToX ((double) doc->sections[(size_t) i].start);
        const auto distance = std::abs (x - (double) pos.x);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }

    return best;
}

void WaveDisplay::paint (juce::Graphics& g)
{
    g.fillAll (kPanel);

    if (doc == nullptr || doc->sample == nullptr || peaks == nullptr)
    {
        g.setColour (juce::Colours::whitesmoke.withAlpha (0.6f));
        g.setFont (22.0f);
        g.drawText ("drop an audio file", getLocalBounds(), juce::Justification::centred);
        return;
    }

    const auto& buffer = doc->sample->buffer;
    const auto bounds = getLocalBounds().toFloat();
    const int width = getWidth();

    // Playing section highlight: the main display always shows where the
    // currently played slice lives.
    if (playSection >= 0 && playSection < (int) doc->sections.size())
    {
        const auto& sec = doc->sections[(size_t) playSection];
        const auto x1 = (float) std::max (0.0, frameToX ((double) sec.start));
        const auto x2 = (float) std::min ((double) width, frameToX ((double) sec.end));
        if (x2 > x1)
        {
            g.setColour (kWave.withAlpha (0.14f));
            g.fillRect (x1, 0.0f, x2 - x1, bounds.getHeight());
        }
    }

    render::drawWave (g, bounds, buffer, *peaks, viewStart, viewLength, kWave);

    // Slice markers with grab handles.
    for (int i = 1; i < (int) doc->sections.size(); ++i)
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

    if (playFrame >= 0.0)
    {
        const auto x = (float) frameToX (playFrame);
        if (x >= 0.0f && x <= (float) width)
        {
            g.setColour (kPlayhead.withAlpha (0.85f));
            g.fillRect (x, 0.0f, 1.5f, bounds.getHeight());
        }
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
        if (onMoveStart != nullptr)
            onMoveStart (dragIndex, xToFrame ((double) e.position.x));
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
    if (index >= 0 && onRemove != nullptr)
        onRemove (index);
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
