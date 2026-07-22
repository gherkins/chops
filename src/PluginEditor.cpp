#include "PluginEditor.h"

#include "model/Edits.h"

static bool isAudioFile (const juce::String& path)
{
    static const juce::StringArray extensions
        { ".wav", ".aif", ".aiff", ".flac", ".ogg", ".oga", ".mp3", ".m4a", ".aac", ".opus" };

    for (const auto& ext : extensions)
        if (path.endsWithIgnoreCase (ext))
            return true;

    return false;
}

ChopsEditor::ChopsEditor (ChopsProcessor& p)
    : AudioProcessorEditor (p), chopsProcessor (p)
{
    setSize (900, 600);
    setResizable (true, true);
    setResizeLimits (600, 400, 4096, 4096);

    addAndMakeVisible (waveDisplay);
    addAndMakeVisible (laneViewport);
    laneViewport.setViewedComponent (&laneList, false);
    laneViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (padStrip);
    addAndMakeVisible (sliceEqualButton);
    addAndMakeVisible (sliceCountBox);
    addAndMakeVisible (transientButton);
    addAndMakeVisible (clearButton);

    for (const int count : { 2, 4, 8, 16, 32 })
        sliceCountBox.addItem (juce::String (count), count);
    sliceCountBox.setSelectedId (8, juce::dontSendNotification);

    waveDisplay.onSplit = [this] (juce::int64 frame)
    {
        applyEdit ([frame] (chops::Document& d) { return chops::edits::splitAt (d, frame); });
    };
    waveDisplay.onMoveStart = [this] (int index, juce::int64 newStart)
    {
        applyEdit ([index, newStart] (chops::Document& d)
                   { return chops::edits::moveSectionStart (d, index, newStart); });
    };
    waveDisplay.onRemove = [this] (int index)
    {
        applyEdit ([index] (chops::Document& d) { return chops::edits::removeSection (d, index); });
    };
    padStrip.onPad = [this] (int note, bool on) { chopsProcessor.triggerPad (note, on); };

    laneList.onSetRange = [this] (int index, juce::int64 s, juce::int64 e)
    {
        applyEdit ([index, s, e] (chops::Document& d)
                   { return chops::edits::setSectionRange (d, index, s, e); });
    };
    laneList.onSetLoop = [this] (int index, juce::int64 s, juce::int64 e)
    {
        applyEdit ([index, s, e] (chops::Document& d)
                   { return chops::edits::setSectionLoop (d, index, s, e); });
    };
    laneList.onClearLoop = [this] (int index)
    {
        applyEdit ([index] (chops::Document& d)
                   { return chops::edits::clearSectionLoop (d, index); });
    };
    laneList.onSetMode = [this] (int index, chops::PlayMode mode)
    {
        applyEdit ([index, mode] (chops::Document& d)
                   { return chops::edits::setSectionMode (d, index, mode); });
    };
    laneList.onSetReverse = [this] (int index, bool reverse)
    {
        applyEdit ([index, reverse] (chops::Document& d)
                   { return chops::edits::setSectionReverse (d, index, reverse); });
    };
    laneList.onPad = [this] (int note, bool on) { chopsProcessor.triggerPad (note, on); };

    sliceEqualButton.onClick = [this]
    {
        const int parts = sliceCountBox.getSelectedId();
        applyEdit ([parts] (chops::Document& d)
                   { chops::edits::autoSliceEqual (d, parts); return true; });
    };
    transientButton.onClick = [this]
    {
        applyEdit ([] (chops::Document& d)
                   { chops::edits::autoSliceTransients (d, 0.5f); return true; });
    };
    clearButton.onClick = [this]
    {
        applyEdit ([] (chops::Document& d) { chops::edits::clearSlices (d); return true; });
    };

    refreshFromModel();
    chopsProcessor.addChangeListener (this);
    startTimerHz (30);
}

ChopsEditor::~ChopsEditor()
{
    chopsProcessor.removeChangeListener (this);
}

void ChopsEditor::applyEdit (const std::function<bool (chops::Document&)>& edit)
{
    if (doc == nullptr || doc->sample == nullptr)
        return;

    chops::Document edited (*doc);
    if (edit (edited))
        chopsProcessor.setDocument (std::move (edited));
}

void ChopsEditor::refreshFromModel()
{
    doc = chopsProcessor.document();

    if (doc->sample != peaksBuiltFor)
    {
        peaksBuiltFor = doc->sample;
        if (doc->sample != nullptr)
            peaks.build (doc->sample->buffer);
        else
            peaks.clear();
    }

    waveDisplay.setDocument (doc, &peaks);
    laneList.setDocument (doc, &peaks, laneViewport.getMaximumVisibleWidth());
    padStrip.setDocument (doc);
    repaint();
}

void ChopsEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshFromModel();
}

void ChopsEditor::timerCallback()
{
    const auto section = chopsProcessor.engine().uiSectionIndex.load (std::memory_order_relaxed);
    const auto frame = chopsProcessor.engine().uiPositionFrames.load (std::memory_order_relaxed);
    waveDisplay.setPlayhead (section, frame);
    laneList.setPlayhead (section, frame);
    padStrip.setActiveSection (section);
}

void ChopsEditor::resized()
{
    auto bounds = getLocalBounds().reduced (12);

    auto topBar = bounds.removeFromTop (30);
    sliceCountBox.setBounds (topBar.removeFromLeft (64));
    topBar.removeFromLeft (4);
    sliceEqualButton.setBounds (topBar.removeFromLeft (72));
    topBar.removeFromLeft (4);
    transientButton.setBounds (topBar.removeFromLeft (92));
    topBar.removeFromLeft (4);
    clearButton.setBounds (topBar.removeFromLeft (64));

    bounds.removeFromTop (8);
    auto info = bounds.removeFromBottom (22);
    juce::ignoreUnused (info);

    padStrip.setBounds (bounds.removeFromBottom (56));
    bounds.removeFromBottom (8);

    waveDisplay.setBounds (bounds.removeFromTop (juce::jmax (120, bounds.getHeight() * 2 / 5)));
    bounds.removeFromTop (8);
    laneViewport.setBounds (bounds);
    laneList.setDocument (doc, &peaks, laneViewport.getMaximumVisibleWidth());
}

void ChopsEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff17181c));

    const auto info = getLocalBounds().reduced (12).removeFromBottom (22);
    g.setFont (13.0f);

    if (doc != nullptr && doc->sample != nullptr)
    {
        const auto& smp = *doc->sample;
        g.setColour (juce::Colours::whitesmoke.withAlpha (0.7f));
        g.drawText (juce::File (smp.originalPath).getFileName()
                        + "   " + juce::String (smp.sourceSampleRate / 1000.0, 1) + " kHz"
                        + "   " + juce::String (doc->sections.size()) + " slice(s)"
                        + "   wave: click adds slice, wheel zooms · lane: drag selects loop,"
                        + " edges trim, double-click clears loop",
                    info, juce::Justification::centredLeft);
    }
    else
    {
        const auto error = chopsProcessor.lastError();
        if (error.isNotEmpty())
        {
            g.setColour (juce::Colours::orangered);
            g.drawText (error, info, juce::Justification::centredLeft);
        }
    }

    if (dragOver)
    {
        g.setColour (juce::Colour (0xff5ec8a8));
        g.drawRect (getLocalBounds(), 3);
    }
}

bool ChopsEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (isAudioFile (f))
            return true;

    return false;
}

void ChopsEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    dragOver = true;
    repaint();
}

void ChopsEditor::fileDragExit (const juce::StringArray&)
{
    dragOver = false;
    repaint();
}

void ChopsEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;

    for (const auto& f : files)
    {
        if (isAudioFile (f))
        {
            chopsProcessor.loadSampleFile (juce::File (f));
            break;
        }
    }

    repaint();
}
