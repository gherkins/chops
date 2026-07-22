#include "PluginProcessor.h"
#include "PluginEditor.h"

ChopsProcessor::ChopsProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void ChopsProcessor::prepareToPlay (double, int)
{
    setLatencySamples (0);
}

bool ChopsProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

void ChopsProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
}

juce::AudioProcessorEditor* ChopsProcessor::createEditor()
{
    return new ChopsEditor (*this);
}

void ChopsProcessor::getStateInformation (juce::MemoryBlock&)
{
}

void ChopsProcessor::setStateInformation (const void*, int)
{
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChopsProcessor();
}
