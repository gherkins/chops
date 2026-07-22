#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include "engine/Engine.h"
#include "model/Document.h"

class ChopsProcessor : public juce::AudioProcessor,
                       public juce::ChangeBroadcaster
{
public:
    ChopsProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                              { return true; }

    const juce::String getName() const override                  { return JucePlugin_Name; }
    bool acceptsMidi() const override                            { return true; }
    bool producesMidi() const override                           { return false; }
    bool isMidiEffect() const override                           { return false; }
    double getTailLengthSeconds() const override                 { return 0.0; }

    int getNumPrograms() override                                { return 1; }
    int getCurrentProgram() override                             { return 0; }
    void setCurrentProgram (int) override                        {}
    const juce::String getProgramName (int) override             { return {}; }
    void changeProgramName (int, const juce::String&) override   {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- message-thread API for the editor ---
    bool loadSampleFile (const juce::File& file);
    std::shared_ptr<const chops::Document> document() const;
    juce::String lastError() const;
    void triggerPad (int midiNote, bool noteOn);   // UI click audition
    chops::Engine& engine() noexcept               { return chopsEngine; }

private:
    void setModel (chops::Document&& newModel);

    chops::Engine chopsEngine;
    juce::MidiMessageCollector midiCollector;

    mutable juce::CriticalSection modelLock;
    std::shared_ptr<const chops::Document> model;
    juce::String lastErrorMessage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChopsProcessor)
};
