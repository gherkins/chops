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

    ChopsProcessor& chopsProcessor;
    std::shared_ptr<const chops::Document> doc;
    chops::PeakCache peaks;
    std::shared_ptr<const chops::SampleData> peaksBuiltFor;

    chops::WaveDisplay waveDisplay;
    chops::SliceLaneList laneList;
    juce::Viewport laneViewport;
    chops::PadStrip padStrip;
    juce::TextButton sliceEqualButton { "slice =" };
    juce::ComboBox sliceCountBox;
    juce::TextButton transientButton { "transients" };
    juce::TextButton clearButton { "clear" };

    bool dragOver = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChopsEditor)
};
