#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "state/State.h"

ChopsProcessor::ChopsProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      model (std::make_shared<const chops::Document>())
{
}

void ChopsProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    chopsEngine.prepare (sampleRate, samplesPerBlock);
    midiCollector.reset (sampleRate);
    setLatencySamples (0);
}

bool ChopsProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

void ChopsProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    midiCollector.removeNextBlockOfMessages (midiMessages, buffer.getNumSamples());
    chopsEngine.process (buffer, midiMessages);
}

juce::AudioProcessorEditor* ChopsProcessor::createEditor()
{
    return new ChopsEditor (*this);
}

void ChopsProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    std::shared_ptr<const chops::Document> snapshot;
    {
        const juce::ScopedLock lock (modelLock);
        snapshot = model;
    }
    chops::state::toMemory (*snapshot, destData);
}

void ChopsProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::String error;
    if (auto doc = chops::state::fromMemory (data, (size_t) sizeInBytes, error))
    {
        setModel (std::move (*doc));
    }
    else
    {
        const juce::ScopedLock lock (modelLock);
        lastErrorMessage = error;
    }
}

bool ChopsProcessor::loadSampleFile (const juce::File& file)
{
    juce::String error;
    auto sampleData = chops::state::loadSampleFile (file, error);

    if (sampleData == nullptr)
    {
        {
            const juce::ScopedLock lock (modelLock);
            lastErrorMessage = error;
        }
        sendChangeMessage();
        return false;
    }

    chops::Document doc;
    doc.global = document()->global;
    doc.sample = std::move (sampleData);
    doc.sections = { chops::makeWholeFileSection (*doc.sample, doc.global.rootNote) };
    setModel (std::move (doc));
    return true;
}

void ChopsProcessor::setModel (chops::Document&& newModel)
{
    auto shared = std::make_shared<const chops::Document> (std::move (newModel));
    {
        const juce::ScopedLock lock (modelLock);
        model = shared;
        lastErrorMessage.clear();
        // The engine gets its own copy: cheap, the sample buffer is shared.
        chopsEngine.publishDocument (std::make_unique<const chops::Document> (*shared));
    }
    sendChangeMessage();
}

std::shared_ptr<const chops::Document> ChopsProcessor::document() const
{
    const juce::ScopedLock lock (modelLock);
    return model;
}

juce::String ChopsProcessor::lastError() const
{
    const juce::ScopedLock lock (modelLock);
    return lastErrorMessage;
}

void ChopsProcessor::triggerPad (int midiNote, bool noteOn)
{
    auto message = noteOn ? juce::MidiMessage::noteOn (1, midiNote, (juce::uint8) 127)
                          : juce::MidiMessage::noteOff (1, midiNote);
    message.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
    midiCollector.addMessageToQueue (message);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChopsProcessor();
}
