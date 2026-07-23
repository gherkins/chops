#pragma once

#include "PluginProcessor.h"
#include "ui/PadStrip.h"
#include "ui/PeakCache.h"
#include "ui/SliceLane.h"
#include "ui/WaveDisplay.h"

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

    bool dragOver = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChopsEditor)
};
