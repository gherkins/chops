#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../model/Document.h"
#include "PeakCache.h"

namespace chops
{

// One lane per slice: its own waveform view of the section (with a little
// context around it), a drag-selectable loop region, free start/end handles
// (sections may overlap — lane edits never touch neighbours), and a header
// with the pad note, playback mode and reverse.
class SliceLane : public juce::Component
{
public:
    static constexpr int kHeight = 104;
    static constexpr int kHeaderWidth = 196;

    // All callbacks receive the section index this lane is bound to.
    // Section boundaries are edited in the main waveform only; the lane owns
    // the loop region and per-slice settings.
    std::function<void (int, juce::int64, juce::int64)> onSetLoop;
    std::function<void (int)> onClearLoop;
    std::function<void (int, PlayMode)> onSetMode;
    std::function<void (int, bool)> onSetReverse;
    std::function<void (int, int, float)> onSetPitch;      // semis, cents
    std::function<void (int, double)> onSetSr;             // Hz, 0 = follow global
    std::function<void (int, float)> onSetDrive;           // 0..1, -1 = follow global
    std::function<void (int midiNote, bool on)> onPad;

    SliceLane();

    void bind (int sectionIndex, std::shared_ptr<const Document> doc, const PeakCache* peaks);
    void setPlayhead (bool activeNow, double frame);

    int boundIndex() const noexcept { return index; }

    // True while a gesture is in flight; the editor must not rebind the lane
    // to another slice mid-drag.
    bool isGestureActive() const noexcept { return drag != Drag::None || padPressed; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    enum class Drag { None, SelectLoop, LoopStart, LoopEnd };

    const Section* section() const;
    juce::Rectangle<int> waveBounds() const;
    double frameToX (double frame) const;
    juce::int64 xToFrame (double x) const;
    Drag hitAt (juce::Point<int> pos) const;
    void updateView();

    int index = -1;
    std::shared_ptr<const Document> doc;
    const PeakCache* peaks = nullptr;

    double viewStart = 0.0, viewLength = 0.0;
    Drag drag = Drag::None;
    juce::int64 selectAnchor = 0;
    bool active = false;
    double playFrame = -1.0;
    bool padPressed = false;

    juce::TextButton loopButton { "loop" }, oneShotButton { "1shot" }, gateButton { "gate" };
    juce::ToggleButton reverseButton { "rev" };
    juce::Slider pitchKnob, fineKnob, srKnob, driveKnob;

    static constexpr int kEdgeHitPx = 6;
};

} // namespace chops
