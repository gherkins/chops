#include "SliceLane.h"

#include "WaveRender.h"
#include "../model/Edits.h"

namespace chops
{

static const juce::Colour kPanel { 0xff22242a };
static const juce::Colour kHeaderBg { 0xff1c1e24 };
static const juce::Colour kWaveDim { 0xff3a5c50 };
static const juce::Colour kWave { 0xff5ec8a8 };
static const juce::Colour kLoop { 0xff7ab8ff };
static const juce::Colour kEdge { 0xffd9a05a };

SliceLane::SliceLane()
{
    for (auto* b : { &loopButton, &oneShotButton, &gateButton })
    {
        addAndMakeVisible (*b);
        b->setClickingTogglesState (false);
    }
    addAndMakeVisible (reverseButton);

    loopButton.onClick = [this] { if (onSetMode) onSetMode (index, PlayMode::LoopRun); };
    oneShotButton.onClick = [this] { if (onSetMode) onSetMode (index, PlayMode::OneShot); };
    gateButton.onClick = [this] { if (onSetMode) onSetMode (index, PlayMode::Gate); };
    reverseButton.onClick = [this]
    {
        if (onSetReverse)
            onSetReverse (index, reverseButton.getToggleState());
    };
}

const Section* SliceLane::section() const
{
    if (doc == nullptr || index < 0 || index >= (int) doc->sections.size())
        return nullptr;
    return &doc->sections[(size_t) index];
}

void SliceLane::bind (int sectionIndex, std::shared_ptr<const Document> newDoc, const PeakCache* newPeaks)
{
    index = sectionIndex;
    doc = std::move (newDoc);
    peaks = newPeaks;
    updateView();

    if (const auto* sec = section())
    {
        loopButton.setToggleState (sec->mode == PlayMode::LoopRun, juce::dontSendNotification);
        oneShotButton.setToggleState (sec->mode == PlayMode::OneShot, juce::dontSendNotification);
        gateButton.setToggleState (sec->mode == PlayMode::Gate, juce::dontSendNotification);
        reverseButton.setToggleState (sec->reverse, juce::dontSendNotification);
    }

    repaint();
}

void SliceLane::updateView()
{
    const auto* sec = section();
    if (sec == nullptr || doc->sample == nullptr)
        return;

    // Keep the current view if the section still fits comfortably inside it;
    // otherwise re-frame with ~8% context on both sides. Stable view while
    // dragging edges = no visual jumps mid-gesture.
    const auto total = (double) doc->sample->numFrames();
    const auto secStart = (double) sec->start;
    const auto secEnd = (double) sec->end;
    const auto pad = std::max (64.0, (secEnd - secStart) * 0.08);

    const bool fits = drag != Drag::None
                      && viewLength > 0.0
                      && secStart >= viewStart
                      && secEnd <= viewStart + viewLength;
    if (fits)
        return;

    viewStart = std::max (0.0, secStart - pad);
    viewLength = std::min (total, secEnd + pad) - viewStart;
}

void SliceLane::setPlayhead (bool activeNow, double frame)
{
    if (active == activeNow && (! activeNow || juce::approximatelyEqual (frame, playFrame)))
        return;

    active = activeNow;
    playFrame = activeNow ? frame : -1.0;
    repaint (waveBounds());
    if (active != activeNow)
        repaint();
}

juce::Rectangle<int> SliceLane::waveBounds() const
{
    return getLocalBounds().withTrimmedLeft (kHeaderWidth).reduced (0, 2);
}

double SliceLane::frameToX (double frame) const
{
    const auto area = waveBounds().toFloat();
    return area.getX() + (frame - viewStart) / viewLength * (double) area.getWidth();
}

juce::int64 SliceLane::xToFrame (double x) const
{
    const auto area = waveBounds().toFloat();
    return (juce::int64) (viewStart + (x - area.getX()) / (double) area.getWidth() * viewLength);
}

void SliceLane::resized()
{
    auto header = getLocalBounds().removeFromLeft (kHeaderWidth).reduced (6);
    header.removeFromTop (20);   // note name row, painted

    auto modeRow = header.removeFromTop (22);
    const int buttonWidth = modeRow.getWidth() / 3;
    loopButton.setBounds (modeRow.removeFromLeft (buttonWidth).reduced (1));
    oneShotButton.setBounds (modeRow.removeFromLeft (buttonWidth).reduced (1));
    gateButton.setBounds (modeRow.reduced (1));

    header.removeFromTop (4);
    reverseButton.setBounds (header.removeFromTop (20));
}

void SliceLane::paint (juce::Graphics& g)
{
    const auto* sec = section();

    g.fillAll (kPanel);
    g.setColour (kHeaderBg);
    g.fillRect (getLocalBounds().removeFromLeft (kHeaderWidth));

    if (sec == nullptr || doc->sample == nullptr || peaks == nullptr)
        return;

    // Header: pad note name (click-and-hold on it auditions).
    g.setColour (active || padPressed ? kWave : juce::Colours::whitesmoke.withAlpha (0.85f));
    g.setFont (juce::Font (juce::FontOptions { 16.0f, juce::Font::bold }));
    g.drawText (juce::MidiMessage::getMidiNoteName (sec->midiNote, true, true, 3),
                juce::Rectangle<int> (6, 2, kHeaderWidth - 12, 18),
                juce::Justification::centredLeft);

    const auto area = waveBounds().toFloat();
    const auto& buffer = doc->sample->buffer;

    // Context wave (dim), then the section range on top (bright).
    render::drawWave (g, area, buffer, *peaks, viewStart, viewLength, kWaveDim);

    const auto startX = (float) frameToX ((double) sec->start);
    const auto endX = (float) frameToX ((double) sec->end);

    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (juce::Rectangle<float> (startX, area.getY(),
                                                    endX - startX, area.getHeight())
                                .toNearestInt());
        render::drawWave (g, area, buffer, *peaks, viewStart, viewLength, kWave);
    }

    // Loop region overlay.
    if (sec->hasLoop())
    {
        const auto loopX1 = (float) frameToX ((double) sec->loopStart);
        const auto loopX2 = (float) frameToX ((double) sec->loopEnd);
        g.setColour (kLoop.withAlpha (0.18f));
        g.fillRect (loopX1, area.getY(), loopX2 - loopX1, area.getHeight());
        g.setColour (kLoop);
        g.fillRect (loopX1 - 0.75f, area.getY(), 1.5f, area.getHeight());
        g.fillRect (loopX2 - 0.75f, area.getY(), 1.5f, area.getHeight());
    }

    // Section start/end handles.
    g.setColour (kEdge);
    g.fillRect (startX - 1.0f, area.getY(), 2.0f, area.getHeight());
    g.fillRect (endX - 1.0f, area.getY(), 2.0f, area.getHeight());

    if (active && playFrame >= 0.0)
    {
        const auto x = (float) frameToX (playFrame);
        if (x >= area.getX() && x <= area.getRight())
        {
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.fillRect (x, area.getY(), 1.5f, area.getHeight());
        }
    }

    if (active)
    {
        g.setColour (kWave.withAlpha (0.6f));
        g.drawRect (getLocalBounds(), 1);
    }
}

SliceLane::Drag SliceLane::hitAt (juce::Point<int> pos) const
{
    const auto* sec = section();
    if (sec == nullptr || ! waveBounds().contains (pos))
        return Drag::None;

    const auto near = [&] (juce::int64 frame)
    {
        return std::abs (frameToX ((double) frame) - (double) pos.x) <= kEdgeHitPx;
    };

    // Loop edges win over section edges; selection is the fallback.
    if (sec->hasLoop())
    {
        if (near (sec->loopStart)) return Drag::LoopStart;
        if (near (sec->loopEnd)) return Drag::LoopEnd;
    }
    if (near (sec->start)) return Drag::SecStart;
    if (near (sec->end)) return Drag::SecEnd;
    return Drag::SelectLoop;
}

void SliceLane::mouseDown (const juce::MouseEvent& e)
{
    const auto* sec = section();
    if (sec == nullptr)
        return;

    if (e.getPosition().x < kHeaderWidth)
    {
        if (onPad)
        {
            padPressed = true;
            onPad (sec->midiNote, true);
            repaint();
        }
        return;
    }

    drag = hitAt (e.getPosition());
    selectAnchor = xToFrame ((double) e.getPosition().x);
}

void SliceLane::mouseDrag (const juce::MouseEvent& e)
{
    const auto* sec = section();
    if (sec == nullptr || drag == Drag::None)
        return;

    const auto frame = xToFrame ((double) e.position.x);

    switch (drag)
    {
        case Drag::SelectLoop:
            if (std::abs (frame - selectAnchor) >= edits::kMinLoopFrames && onSetLoop)
                onSetLoop (index, selectAnchor, frame);
            break;

        case Drag::LoopStart:
            if (onSetLoop)
                onSetLoop (index, frame, sec->loopEnd);
            break;

        case Drag::LoopEnd:
            if (onSetLoop)
                onSetLoop (index, sec->loopStart, frame);
            break;

        case Drag::SecStart:
            if (onSetRange)
                onSetRange (index, frame, sec->end);
            break;

        case Drag::SecEnd:
            if (onSetRange)
                onSetRange (index, sec->start, frame);
            break;

        case Drag::None:
            break;
    }
}

void SliceLane::mouseUp (const juce::MouseEvent&)
{
    if (padPressed)
    {
        padPressed = false;
        if (const auto* sec = section(); sec != nullptr && onPad)
            onPad (sec->midiNote, false);
        repaint();
    }

    drag = Drag::None;
    updateView();
    repaint();
}

void SliceLane::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto* sec = section();
    if (sec == nullptr || ! waveBounds().contains (e.getPosition()))
        return;

    const auto frame = xToFrame ((double) e.getPosition().x);
    if (sec->hasLoop() && frame >= sec->loopStart && frame <= sec->loopEnd && onClearLoop)
        onClearLoop (index);
}

void SliceLane::mouseMove (const juce::MouseEvent& e)
{
    switch (hitAt (e.getPosition()))
    {
        case Drag::LoopStart:
        case Drag::LoopEnd:
        case Drag::SecStart:
        case Drag::SecEnd:
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
            break;
        case Drag::SelectLoop:
        case Drag::None:
            setMouseCursor (juce::MouseCursor::NormalCursor);
            break;
    }
}

// --- SliceLaneList ---

void SliceLaneList::setDocument (std::shared_ptr<const Document> doc, const PeakCache* peaks, int width)
{
    const auto count = doc != nullptr && doc->sample != nullptr ? doc->sections.size() : 0;

    if (lanes.size() != count)
    {
        lanes.clear();
        for (size_t i = 0; i < count; ++i)
        {
            auto lane = std::make_unique<SliceLane>();
            lane->onSetRange = [this] (int idx, juce::int64 s, juce::int64 e) { if (onSetRange) onSetRange (idx, s, e); };
            lane->onSetLoop = [this] (int idx, juce::int64 s, juce::int64 e) { if (onSetLoop) onSetLoop (idx, s, e); };
            lane->onClearLoop = [this] (int idx) { if (onClearLoop) onClearLoop (idx); };
            lane->onSetMode = [this] (int idx, PlayMode m) { if (onSetMode) onSetMode (idx, m); };
            lane->onSetReverse = [this] (int idx, bool r) { if (onSetReverse) onSetReverse (idx, r); };
            lane->onPad = [this] (int note, bool on) { if (onPad) onPad (note, on); };
            addAndMakeVisible (*lane);
            lanes.push_back (std::move (lane));
        }
    }

    for (size_t i = 0; i < count; ++i)
        lanes[i]->bind ((int) i, doc, peaks);

    setSize (width, (int) count * (SliceLane::kHeight + 2));
}

void SliceLaneList::setPlayhead (int activeSection, double frame)
{
    for (size_t i = 0; i < lanes.size(); ++i)
        lanes[i]->setPlayhead ((int) i == activeSection, frame);
}

void SliceLaneList::resized()
{
    int y = 0;
    for (auto& lane : lanes)
    {
        lane->setBounds (0, y, getWidth(), SliceLane::kHeight);
        y += SliceLane::kHeight + 2;
    }
}

} // namespace chops
