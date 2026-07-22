#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../model/Document.h"
#include "PeakCache.h"

namespace chops
{

// The main waveform: always on top, shows the whole sample, slice markers and
// the live playhead. Interactions (Renoise-style):
//   - mouse wheel            zoom around the cursor (trackpad pinch too)
//   - horizontal wheel/drag  pan
//   - click on empty wave    add a slice marker (split section)
//   - drag a marker handle   move it (linked partition boundaries follow)
//   - double-click a handle  remove that slice
class WaveDisplay : public juce::Component
{
public:
    std::function<void (juce::int64 frame)> onSplit;
    std::function<void (int index, juce::int64 newStart)> onMoveStart;
    std::function<void (int index)> onRemove;

    void setDocument (std::shared_ptr<const Document> newDoc, const PeakCache* newPeaks);
    void setPlayhead (int sectionIndex, double frame);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

private:
    double frameToX (double frame) const;
    juce::int64 xToFrame (double x) const;
    int hitMarker (juce::Point<int> pos) const;   // section index >= 1, or -1
    void zoomAround (double frame, double factor);
    void clampView();

    std::shared_ptr<const Document> doc;
    const PeakCache* peaks = nullptr;

    double viewStart = 0.0;    // frames
    double viewLength = 0.0;
    int dragIndex = -1;
    bool didDrag = false;
    int playSection = -1;
    double playFrame = -1.0;

    static constexpr int kHandleHitPx = 6;
    static constexpr double kMinViewFrames = 32.0;
};

} // namespace chops
