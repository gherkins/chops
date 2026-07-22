#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace chops::ui
{

inline void configureMiniKnob (juce::Slider& s, double min, double max, double defaultValue,
                               double step = 0.0, double skewMid = 0.0)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setRange (min, max, step);
    if (skewMid > 0.0)
        s.setSkewFactorFromMidPoint (skewMid);
    s.setDoubleClickReturnValue (true, defaultValue);
    s.setValue (defaultValue, juce::dontSendNotification);
    s.setPopupDisplayEnabled (true, true, nullptr);
}

// Knob-at-max means "follow the global setting" for per-slice sample rate.
constexpr double kSrFollowThreshold = 47999.0;

} // namespace chops::ui
