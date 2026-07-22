#include "PluginEditor.h"

ChopsEditor::ChopsEditor (ChopsProcessor& p)
    : AudioProcessorEditor (p), chopsProcessor (p)
{
    setSize (900, 600);
    setResizable (true, true);
    setResizeLimits (600, 400, 4096, 4096);
}

void ChopsEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff17181c));
    g.setColour (juce::Colours::whitesmoke);
    g.setFont (28.0f);
    g.drawText ("chops", getLocalBounds(), juce::Justification::centred);
}
