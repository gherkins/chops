#include "PluginEditor.h"

#include "model/Edits.h"
#include "ui/Knobs.h"

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
    addAndMakeVisible (sliceLane);
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
    // Pads trigger AND select which slice the lane editor shows.
    padStrip.onPad = [this] (int sectionIndex, bool on)
    {
        if (doc != nullptr && sectionIndex >= 0 && sectionIndex < (int) doc->sections.size())
        {
            if (on)
                selectSection (sectionIndex);
            chopsProcessor.triggerPad (doc->sections[(size_t) sectionIndex].midiNote, on);
        }
    };

    sliceLane.onSetLoop = [this] (int index, juce::int64 s, juce::int64 e)
    {
        applyEdit ([index, s, e] (chops::Document& d)
                   { return chops::edits::setSectionLoop (d, index, s, e); });
    };
    sliceLane.onClearLoop = [this] (int index)
    {
        applyEdit ([index] (chops::Document& d)
                   { return chops::edits::clearSectionLoop (d, index); });
    };
    sliceLane.onSetMode = [this] (int index, chops::PlayMode mode)
    {
        applyEdit ([index, mode] (chops::Document& d)
                   { return chops::edits::setSectionMode (d, index, mode); });
    };
    sliceLane.onSetReverse = [this] (int index, bool reverse)
    {
        applyEdit ([index, reverse] (chops::Document& d)
                   { return chops::edits::setSectionReverse (d, index, reverse); });
    };
    sliceLane.onSetPitch = [this] (int index, int semis, float cents)
    {
        applyEdit ([index, semis, cents] (chops::Document& d)
                   { return chops::edits::setSectionPitch (d, index, semis, cents); });
    };
    sliceLane.onSetSr = [this] (int index, double hz)
    {
        applyEdit ([index, hz] (chops::Document& d)
                   { return chops::edits::setSectionSrOverride (d, index, hz); });
    };
    sliceLane.onSetDrive = [this] (int index, float drive)
    {
        applyEdit ([index, drive] (chops::Document& d)
                   { return chops::edits::setSectionDriveOverride (d, index, drive); });
    };
    sliceLane.onPad = [this] (int note, bool on) { chopsProcessor.triggerPad (note, on); };

    // Global FX strip.
    chops::ui::configureMiniKnob (globalSr, 300.0, 48000.0, 48000.0, 0.0, 4000.0);
    chops::ui::configureMiniKnob (globalDrive, 0.0, 1.0, 0.0);
    chops::ui::configureMiniKnob (globalPitch, -24.0, 24.0, 0.0, 1.0);
    chops::ui::configureMiniKnob (globalFine, -100.0, 100.0, 0.0, 1.0);
    chops::ui::configureMiniKnob (globalGain, 0.0, 2.0, 1.0);
    for (auto* k : { &globalSr, &globalDrive, &globalPitch, &globalFine, &globalGain })
        addAndMakeVisible (*k);

    const auto sendGlobals = [this]
    {
        applyEdit ([sr = globalSr.getValue(), drive = globalDrive.getValue(),
                    pitch = globalPitch.getValue(), fine = globalFine.getValue(),
                    gain = globalGain.getValue()] (chops::Document& d)
        {
            d.global.srReduce = sr >= chops::ui::kSrFollowThreshold ? 0.0 : sr;
            d.global.drive = (float) drive;
            d.global.pitchSemis = (int) pitch;
            d.global.fineCents = (float) fine;
            d.global.gain = (float) gain;
            return true;
        });
    };
    for (auto* k : { &globalSr, &globalDrive, &globalPitch, &globalFine, &globalGain })
        k->onValueChange = sendGlobals;

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

    const auto& g = doc->global;
    globalSr.setValue (g.srReduce > 0.0 ? g.srReduce : 48000.0, juce::dontSendNotification);
    globalDrive.setValue (g.drive, juce::dontSendNotification);
    globalPitch.setValue (g.pitchSemis, juce::dontSendNotification);
    globalFine.setValue (g.fineCents, juce::dontSendNotification);
    globalGain.setValue (g.gain, juce::dontSendNotification);

    if (doc->sample != peaksBuiltFor)
    {
        peaksBuiltFor = doc->sample;
        if (doc->sample != nullptr)
            peaks.build (doc->sample->buffer);
        else
            peaks.clear();
    }

    waveDisplay.setDocument (doc, &peaks);
    padStrip.setDocument (doc);

    // Clamp the selection to the current slice count; first slice by default.
    if (doc->sample == nullptr)
        selectedSection = -1;
    else if (selectedSection < 0)
        selectedSection = 0;
    else
        selectedSection = std::min (selectedSection, (int) doc->sections.size() - 1);

    sliceLane.bind (selectedSection, doc, &peaks);
    padStrip.setSelectedSection (selectedSection);
    repaint();
}

void ChopsEditor::selectSection (int sectionIndex)
{
    if (sectionIndex == selectedSection || doc == nullptr
        || sectionIndex < 0 || sectionIndex >= (int) doc->sections.size()
        || sliceLane.isGestureActive())
        return;

    selectedSection = sectionIndex;
    sliceLane.bind (selectedSection, doc, &peaks);
    padStrip.setSelectedSection (selectedSection);
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
    padStrip.setActiveSection (section);

    // Last-triggered slice becomes the edited one (also covers MIDI input).
    if (section >= 0)
        selectSection (section);

    sliceLane.setPlayhead (section == sliceLane.boundIndex(), frame);
}

void ChopsEditor::resized()
{
    auto bounds = getLocalBounds().reduced (12);

    auto topBar = bounds.removeFromTop (52);
    auto buttonRow = topBar.withHeight (28).withY (topBar.getY() + 6);
    sliceCountBox.setBounds (buttonRow.removeFromLeft (64));
    buttonRow.removeFromLeft (4);
    sliceEqualButton.setBounds (buttonRow.removeFromLeft (72));
    buttonRow.removeFromLeft (4);
    transientButton.setBounds (buttonRow.removeFromLeft (92));
    buttonRow.removeFromLeft (4);
    clearButton.setBounds (buttonRow.removeFromLeft (64));

    auto knobArea = topBar.removeFromRight (5 * 48).withTrimmedBottom (11);
    for (auto* k : { &globalSr, &globalDrive, &globalPitch, &globalFine, &globalGain })
        k->setBounds (knobArea.removeFromLeft (48));

    bounds.removeFromTop (8);
    auto info = bounds.removeFromBottom (22);
    juce::ignoreUnused (info);

    padStrip.setBounds (bounds.removeFromBottom (56));
    bounds.removeFromBottom (8);

    // Always exactly two waveforms: the full sample and the selected slice.
    waveDisplay.setBounds (bounds.removeFromTop (juce::jmax (120, bounds.getHeight() / 2)));
    bounds.removeFromTop (8);
    sliceLane.setBounds (bounds);
}

void ChopsEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff17181c));

    // Global knob labels.
    g.setColour (juce::Colours::whitesmoke.withAlpha (0.45f));
    g.setFont (9.0f);
    {
        const std::pair<const juce::Slider*, const char*> labels[] =
            { { &globalSr, "sr" }, { &globalDrive, "drive" }, { &globalPitch, "pitch" },
              { &globalFine, "fine" }, { &globalGain, "gain" } };
        for (const auto& [knob, text] : labels)
            g.drawText (text, knob->getBounds().withY (knob->getBottom()).withHeight (10),
                        juce::Justification::centred);
    }

    const auto info = getLocalBounds().reduced (12).removeFromBottom (22);
    g.setFont (13.0f);

    if (doc != nullptr && doc->sample != nullptr)
    {
        const auto& smp = *doc->sample;
        g.setColour (juce::Colours::whitesmoke.withAlpha (0.7f));
        g.drawText (juce::File (smp.originalPath).getFileName()
                        + "   " + juce::String (smp.sourceSampleRate / 1000.0, 1) + " kHz"
                        + "   " + juce::String (doc->sections.size()) + " slice(s)"
                        + "   wave: click adds slice, drag handles, wheel zooms · lane: drag"
                        + " selects loop, loop edges adjust, double-click clears",
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
