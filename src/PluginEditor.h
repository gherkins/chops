#pragma once

#include "PluginProcessor.h"

class ChopsEditor : public juce::AudioProcessorEditor,
                    public juce::FileDragAndDropTarget,
                    private juce::ChangeListener,
                    private juce::Timer
{
public:
    explicit ChopsEditor (ChopsProcessor&);
    ~ChopsEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override {}

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    juce::Rectangle<int> waveArea() const;
    void rebuildPeaks();

    static constexpr int kNumBins = 2048;

    ChopsProcessor& chopsProcessor;
    std::shared_ptr<const chops::Document> doc;
    std::vector<float> peakMin, peakMax;
    float lastPlayheadX = -1.0f;
    bool dragOver = false;
    bool auditioning = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChopsEditor)
};
