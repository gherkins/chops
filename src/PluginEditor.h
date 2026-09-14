#pragma once

#include "PluginProcessor.h"
#include "ui/PadStrip.h"
#include "ui/PeakCache.h"
#include "ui/SliceLane.h"
#include "ui/WaveDisplay.h"
#include "engine/GridTune.h"

class ChopsEditor : public juce::AudioProcessorEditor,
                    public juce::FileDragAndDropTarget,
                    private juce::ChangeListener,
                    private juce::Timer
{
public:
    explicit ChopsEditor (ChopsProcessor&);
    ~ChopsEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void applyEdit (const std::function<bool (chops::Document&)>& edit);
    void refreshFromModel();
    void selectSection (int sectionIndex);
    void analyseOutput (const std::vector<int>& playingSections);
    void resetTuner();
    void clearTunerReading();
    void updateTunerText();

    ChopsProcessor& chopsProcessor;
    std::shared_ptr<const chops::Document> doc;
    chops::PeakCache peaks;
    std::shared_ptr<const chops::SampleData> peaksBuiltFor;

    chops::WaveDisplay waveDisplay;
    chops::SliceLane sliceLane;      // single lane: shows the selected slice
    chops::PadStrip padStrip;
    int selectedSection = -1;        // first slice by default, then last triggered
    std::uint64_t lastTriggerSerial = 0;   // engine triggers already applied
    juce::TextButton sliceEqualButton { "slice =" };
    juce::ComboBox sliceCountBox;
    juce::TextButton transientButton { "transients" };
    juce::TextButton clearButton { "clear" };
    juce::TextButton cropButton { "crop" };
    juce::TextButton polyButton { "poly" };
    juce::TextButton monoButton { "mono" };
    juce::TextButton velButton { "vel" };
    juce::Slider globalSr, globalDrive, globalPitch, globalFine, globalGain;

    // Output tuner: grid offset of the most-played slice, latched until the
    // sample or the reference slice changes.
    juce::TextButton snapButton { "snap" };
    std::vector<float> tunerWindow;
    std::unique_ptr<chops::tune::Scratch> tunerScratch;
    int tunerRefSection = -1;
    // Rolling window of per-tick resultants on the 100-cent circle (~3 s at
    // 30 Hz, so chord changes in a loop average out); the reading clears
    // after ~4 s without the reference sounding.
    static constexpr int kTunerRollTicks = 90;
    static constexpr int kTunerClearTicks = 120;
    std::array<std::pair<float, float>, kTunerRollTicks> tuneRing {};
    int tuneRingPos = 0, tuneRingCount = 0;
    int tunerSilentTicks = 0;
    struct { int pitchClass = -1; float cents = 0.0f; bool valid = false; } tunerReading;
    juce::String tunerText;
    juce::Rectangle<int> tunerRect, infoRect;

    bool dragOver = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChopsEditor)
};
