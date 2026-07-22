#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace chops::ui
{

inline void configureMiniKnob (juce::Slider& s, double min, double max, double defaultValue,
                               double step = 0.0, double skewMid = 0.0, int decimals = -1)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setRange (min, max, step);
    if (skewMid > 0.0)
        s.setSkewFactorFromMidPoint (skewMid);
    s.setDoubleClickReturnValue (true, defaultValue);
    s.setValue (defaultValue, juce::dontSendNotification);
    s.setPopupDisplayEnabled (true, true, nullptr);
    // Short readouts: stepped knobs show integers, continuous ones 2 decimals
    // (pass an explicit count where that guess is wrong, e.g. Hz knobs).
    s.setNumDecimalPlacesToDisplay (decimals >= 0 ? decimals : (step >= 1.0 ? 0 : 2));
}

// Knob-at-max means "follow the global setting" for per-slice sample rate.
constexpr double kSrFollowThreshold = 47999.0;

// Type scale — keep to these sizes. The rev toggle sits below kFontLabel via
// its 15 px button height (JUCE derives toggle fonts from height).
constexpr float kFontHint = 22.0f;    // empty-state hint
constexpr float kFontTitle = 16.0f;   // pad note names (bold)
constexpr float kFontLabel = 13.0f;   // knob labels, info line, pads

} // namespace chops::ui
