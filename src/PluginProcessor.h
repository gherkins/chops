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
    void setDocument (chops::Document&& doc)       { setModel (std::move (doc)); }
    std::shared_ptr<const chops::Document> document() const;
    juce::String lastError() const;
    void triggerPad (int midiNote, bool noteOn);   // UI click audition
    chops::Engine& engine() noexcept               { return chopsEngine; }

    // Host transport as last seen in processBlock (90 / 4/4 without a host
    // playhead, e.g. the standalone). The tuner scales its hold time to bars.
    double hostBpm() const noexcept                { return bpm.load (std::memory_order_relaxed); }
    int hostTimeSigNumerator() const noexcept      { return tsNum.load (std::memory_order_relaxed); }
    int hostTimeSigDenominator() const noexcept    { return tsDen.load (std::memory_order_relaxed); }

private:
    void setModel (chops::Document&& newModel);

    chops::Engine chopsEngine;
    juce::MidiMessageCollector midiCollector;
    std::atomic<double> bpm { 90.0 };
    std::atomic<int> tsNum { 4 }, tsDen { 4 };

    mutable juce::CriticalSection modelLock;
    std::shared_ptr<const chops::Document> model;
    juce::String lastErrorMessage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChopsProcessor)
};
