#pragma once

#include "PluginProcessor.h"

class ChopsEditor : public juce::AudioProcessorEditor
{
public:
    explicit ChopsEditor (ChopsProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override {}

private:
    ChopsProcessor& chopsProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChopsEditor)
};
