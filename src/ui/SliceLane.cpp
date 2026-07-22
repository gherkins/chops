#include "SliceLane.h"

#include "Knobs.h"
#include "WaveRender.h"
#include "../model/Edits.h"

namespace chops
{

static const juce::Colour kPanel { 0xff22242a };
static const juce::Colour kHeaderBg { 0xff1c1e24 };
static const juce::Colour kWave { 0xff5ec8a8 };
static const juce::Colour kLoop { 0xff7ab8ff };

SliceLane::SliceLane()
{
    for (auto* b : { &loopButton, &oneShotButton, &gateButton,
                     &loopFwdButton, &loopBackButton, &loopPingPongButton, &reverseButton })
    {
        addAndMakeVisible (*b);
        b->setColour (juce::TextButton::buttonOnColourId, kWave);
        b->setColour (juce::TextButton::textColourOnId, kHeaderBg);
    }
    reverseButton.setClickingTogglesState (true);

    // Two segmented three-way toggles (mode, loop direction): a radio group
    // per row (exactly one always on) with connected edges so each row
    // renders as a single element. Distinct group ids — same parent.
    const auto makeSegmented = [] (juce::TextButton& left, juce::TextButton& mid,
                                   juce::TextButton& right, int groupId)
    {
        for (auto* b : { &left, &mid, &right })
        {
            b->setClickingTogglesState (true);
            b->setRadioGroupId (groupId);
        }
        left.setConnectedEdges (juce::Button::ConnectedOnRight);
        mid.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
        right.setConnectedEdges (juce::Button::ConnectedOnLeft);
    };
    makeSegmented (loopButton, oneShotButton, gateButton, 1);
    makeSegmented (loopFwdButton, loopBackButton, loopPingPongButton, 2);

    loopButton.onClick = [this] { if (onSetMode) onSetMode (index, PlayMode::LoopRun); };
    oneShotButton.onClick = [this] { if (onSetMode) onSetMode (index, PlayMode::OneShot); };
    gateButton.onClick = [this] { if (onSetMode) onSetMode (index, PlayMode::Gate); };
    loopFwdButton.onClick = [this] { if (onSetLoopDir) onSetLoopDir (index, LoopDirection::Forward); };
    loopBackButton.onClick = [this] { if (onSetLoopDir) onSetLoopDir (index, LoopDirection::Backward); };
    loopPingPongButton.onClick = [this] { if (onSetLoopDir) onSetLoopDir (index, LoopDirection::PingPong); };
    reverseButton.onClick = [this]
    {
        if (onSetReverse)
            onSetReverse (index, reverseButton.getToggleState());
    };

    ui::configureMiniKnob (pitchKnob, -24.0, 24.0, 0.0, 1.0);
    ui::configureMiniKnob (fineKnob, -100.0, 100.0, 0.0, 1.0);
    ui::configureMiniKnob (srKnob, 300.0, 48000.0, 48000.0, 0.0, 4000.0, 0);
    ui::configureMiniKnob (driveKnob, -0.05, 1.0, -0.05);
    for (auto* k : { &pitchKnob, &fineKnob, &srKnob, &driveKnob })
        addAndMakeVisible (*k);

    const auto sendPitch = [this]
    {
        if (onSetPitch)
            onSetPitch (index, (int) pitchKnob.getValue(), (float) fineKnob.getValue());
    };
    pitchKnob.onValueChange = sendPitch;
    fineKnob.onValueChange = sendPitch;
    srKnob.onValueChange = [this]
    {
        const auto v = srKnob.getValue();
        if (onSetSr)
            onSetSr (index, v >= ui::kSrFollowThreshold ? 0.0 : v);
    };
    driveKnob.onValueChange = [this]
    {
        const auto v = (float) driveKnob.getValue();
        if (onSetDrive)
            onSetDrive (index, v < 0.0f ? -1.0f : v);
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
        loopFwdButton.setToggleState (sec->loopDir == LoopDirection::Forward, juce::dontSendNotification);
        loopBackButton.setToggleState (sec->loopDir == LoopDirection::Backward, juce::dontSendNotification);
        loopPingPongButton.setToggleState (sec->loopDir == LoopDirection::PingPong, juce::dontSendNotification);
        reverseButton.setToggleState (sec->reverse, juce::dontSendNotification);

        // Loop mode needs a loop region; loop direction needs loop mode.
        const bool hasLoop = sec->hasLoop();
        loopButton.setEnabled (hasLoop);
        for (auto* b : { &loopFwdButton, &loopBackButton, &loopPingPongButton })
            b->setEnabled (hasLoop && sec->mode == PlayMode::LoopRun);
        pitchKnob.setValue (sec->pitchSemis, juce::dontSendNotification);
        fineKnob.setValue (sec->fineCents, juce::dontSendNotification);
        srKnob.setValue (sec->srOverride > 0.0 ? sec->srOverride : 48000.0,
                         juce::dontSendNotification);
        driveKnob.setValue (sec->driveOverride >= 0.0f ? sec->driveOverride : -0.05,
                            juce::dontSendNotification);
    }

    repaint();
}

void SliceLane::updateView()
{
    const auto* sec = section();
    if (sec == nullptr || doc->sample == nullptr)
        return;

    // The slice waveform is exactly the section: no context padding, the
    // slice fills the lane edge to edge.
    viewStart = (double) sec->start;
    viewLength = (double) (sec->end - sec->start);
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
    auto header = getLocalBounds().removeFromLeft (kHeaderWidth).reduced (6, 2);

    // All seven buttons share one size and one vertical rhythm.
    const int buttonWidth = header.getWidth() / 3;

    auto nameRow = header.removeFromTop (22);
    reverseButton.setBounds (nameRow.removeFromRight (buttonWidth).reduced (0, 1));

    header.removeFromTop (6);
    auto modeRow = header.removeFromTop (22).reduced (0, 1);
    loopButton.setBounds (modeRow.removeFromLeft (buttonWidth));
    oneShotButton.setBounds (modeRow.removeFromLeft (buttonWidth));
    gateButton.setBounds (modeRow);

    // Aligned with the mode row; paint() draws a connector tree from the loop
    // segment down into all three direction segments through this gap.
    header.removeFromTop (6);
    auto loopDirRow = header.removeFromTop (22).reduced (0, 1);
    loopFwdButton.setBounds (loopDirRow.removeFromLeft (buttonWidth));
    loopBackButton.setBounds (loopDirRow.removeFromLeft (buttonWidth));
    loopPingPongButton.setBounds (loopDirRow);

    header.removeFromTop (2);
    // Cap the knob strip so a tall lane grows its waveform, not its knobs.
    auto knobRow = header.withTrimmedBottom (15);
    if (knobRow.getHeight() > 54)
        knobRow = knobRow.removeFromTop (54);
    const int knobWidth = knobRow.getWidth() / 4;
    pitchKnob.setBounds (knobRow.removeFromLeft (knobWidth));
    fineKnob.setBounds (knobRow.removeFromLeft (knobWidth));
    srKnob.setBounds (knobRow.removeFromLeft (knobWidth));
    driveKnob.setBounds (knobRow);
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
    g.setFont (juce::Font (juce::FontOptions { ui::kFontTitle, juce::Font::bold }));
    g.drawText (juce::MidiMessage::getMidiNoteName (sec->midiNote, true, true, 3),
                juce::Rectangle<int> (6, 2, kHeaderWidth - 12, 22),
                juce::Justification::centredLeft);

    // Connector tree: a stem from the loop mode segment splits into all three
    // direction segments, making the ownership obvious.
    {
        g.setColour (loopFwdButton.isEnabled() ? kWave.withAlpha (0.9f)
                                               : juce::Colours::whitesmoke.withAlpha (0.25f));

        const float stemX = (float) loopButton.getBounds().getCentreX();
        const float top = (float) loopButton.getBottom();
        const float rowTop = (float) loopFwdButton.getY();
        const float busY = (top + rowTop) * 0.5f;
        const float firstX = (float) loopFwdButton.getBounds().getCentreX();
        const float lastX = (float) loopPingPongButton.getBounds().getCentreX();

        g.fillRect (stemX - 0.75f, top, 1.5f, busY - top);
        g.fillRect (firstX, busY - 0.75f, lastX - firstX, 1.5f);
        for (const auto* b : { &loopFwdButton, &loopBackButton, &loopPingPongButton })
        {
            const float x = (float) b->getBounds().getCentreX();
            g.fillRect (x - 0.75f, busY, 1.5f, rowTop - busY);
        }
    }

    // Knob labels.
    g.setColour (juce::Colours::whitesmoke.withAlpha (0.55f));
    g.setFont (ui::kFontLabel);
    static const char* const knobLabels[] = { "pitch", "fine", "sr", "drive" };
    for (int k = 0; k < 4; ++k)
    {
        const auto* knob = k == 0 ? &pitchKnob : k == 1 ? &fineKnob : k == 2 ? &srKnob : &driveKnob;
        g.drawText (knobLabels[k],
                    knob->getBounds().withY (knob->getBottom()).withHeight (14),
                    juce::Justification::centred);
    }

    const auto area = waveBounds().toFloat();
    const auto& buffer = doc->sample->buffer;

    render::drawWave (g, area, buffer, *peaks, viewStart, viewLength, kWave);

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

    if (sec->hasLoop())
    {
        if (near (sec->loopStart)) return Drag::LoopStart;
        if (near (sec->loopEnd)) return Drag::LoopEnd;

        const auto frame = xToFrame ((double) pos.x);
        if (frame > sec->loopStart && frame < sec->loopEnd)
            return Drag::MoveLoop;   // grab the loop body, slide it whole
    }
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

    if (drag == Drag::MoveLoop)
    {
        loopGrabOffset = selectAnchor - sec->loopStart;
        loopGrabLength = sec->loopEnd - sec->loopStart;
    }
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

        case Drag::MoveLoop:
            if (onSetLoop && loopGrabLength > 0)
            {
                // Slide the whole loop, length preserved, stopping at the
                // slice walls (setSectionLoop's independent clamping would
                // otherwise compress it against them).
                const auto newStart = std::clamp (frame - loopGrabOffset,
                                                  sec->start, sec->end - loopGrabLength);
                onSetLoop (index, newStart, newStart + loopGrabLength);
            }
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
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
            break;
        case Drag::MoveLoop:
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            break;
        case Drag::SelectLoop:
        case Drag::None:
            setMouseCursor (juce::MouseCursor::NormalCursor);
            break;
    }
}

} // namespace chops
