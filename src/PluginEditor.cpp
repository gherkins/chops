#include "PluginEditor.h"

#include "model/Edits.h"
#include "state/State.h"
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
    setResizeLimits (600, 460, 4096, 4096);

    addAndMakeVisible (waveDisplay);
    addAndMakeVisible (sliceLane);
    addAndMakeVisible (padStrip);
    addAndMakeVisible (sliceEqualButton);
    addAndMakeVisible (sliceCountBox);
    addAndMakeVisible (transientButton);
    addAndMakeVisible (clearButton);
    addAndMakeVisible (cropButton);
    addAndMakeVisible (polyButton);
    addAndMakeVisible (monoButton);
    addAndMakeVisible (velButton);

    for (auto* b : { &polyButton, &monoButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1);
        // Same active styling as the lane's mode/loop toggles.
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff5ec8a8));
        b->setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1c1e24));
    }
    velButton.setClickingTogglesState (true);
    velButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff5ec8a8));
    velButton.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1c1e24));
    polyButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    monoButton.setConnectedEdges (juce::Button::ConnectedOnLeft);

    // One handler for both: turning a radio button on also click-notifies the
    // sibling being turned off, so each click can fire twice. Reading the
    // final toggle state and skipping no-op edits keeps it to one publish.
    const auto sendVoiceMode = [this]
    {
        applyEdit ([mono = monoButton.getToggleState()] (chops::Document& d)
        {
            if (d.global.mono == mono)
                return false;
            d.global.mono = mono;
            return true;
        });
    };
    polyButton.onClick = sendVoiceMode;
    monoButton.onClick = sendVoiceMode;

    velButton.onClick = [this]
    {
        applyEdit ([on = velButton.getToggleState()] (chops::Document& d)
        {
            if (d.global.velSensitive == on)
                return false;
            d.global.velSensitive = on;
            return true;
        });
    };

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
    waveDisplay.onMoveEnd = [this] (juce::int64 newEnd)
    {
        applyEdit ([newEnd] (chops::Document& d)
                   { return chops::edits::moveSectionEnd (d, (int) d.sections.size() - 1, newEnd); });
    };
    waveDisplay.onRemove = [this] (int index)
    {
        applyEdit ([index] (chops::Document& d)
        {
            // Marker 0 is the head trim: double-click resets it to the start.
            if (index == 0)
                return chops::edits::moveSectionStart (d, 0, 0);
            return chops::edits::removeSection (d, index);
        });
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
    sliceLane.onSetLoopDir = [this] (int index, chops::LoopDirection dir)
    {
        applyEdit ([index, dir] (chops::Document& d)
                   { return chops::edits::setSectionLoopDirection (d, index, dir); });
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
    sliceLane.onSetGain = [this] (int index, float gain)
    {
        applyEdit ([index, gain] (chops::Document& d)
                   { return chops::edits::setSectionGain (d, index, gain); });
    };
    sliceLane.onPad = [this] (int note, bool on) { chopsProcessor.triggerPad (note, on); };

    // Global FX strip.
    chops::ui::configureMiniKnob (globalSr, 300.0, 48000.0, 48000.0, 0.0, 4000.0, 0);
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
    cropButton.onClick = [this]
    {
        // Hard-discards audio outside the first/last markers and re-encodes
        // the embedded blob, so long files stop weighing down plugin state.
        applyEdit ([] (chops::Document& d)
        {
            juce::String error;
            return chops::state::cropToSections (d, error);
        });
    };

    // Output tuner: the readout latches the grid offset of the most-played
    // slice; snap moves the global fine by exactly that amount. The engine
    // re-resolves sustaining voices every block, so a held note re-pitches
    // live and the readout re-converges to +0%.
    addAndMakeVisible (snapButton);
    snapButton.setEnabled (false);
    tunerWindow.resize ((size_t) chops::tune::kWindow);
    tunerScratch = std::make_unique<chops::tune::Scratch>();
    snapButton.onClick = [this]
    {
        if (! tunerReading.valid)
            return;

        const int off = (int) std::lround (tunerReading.cents);
        applyEdit ([off] (chops::Document& d)
                   { return chops::edits::snapGlobalPitchToSemitone (d, off); });
        tunerReading.cents -= (float) off;
        tuneRingCount = tuneRingPos = 0;   // the window refills from the re-pitched output
        updateTunerText();
    };
    updateTunerText();

    refreshFromModel();
    chopsProcessor.addChangeListener (this);
    startTimerHz (kTunerTimerHz);
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
    monoButton.setToggleState (g.mono, juce::dontSendNotification);
    polyButton.setToggleState (! g.mono, juce::dontSendNotification);
    velButton.setToggleState (g.velSensitive, juce::dontSendNotification);

    if (doc->sample != peaksBuiltFor)
    {
        peaksBuiltFor = doc->sample;
        if (doc->sample != nullptr)
            peaks.build (doc->sample->buffer);
        else
            peaks.clear();
        resetTuner();
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
    waveDisplay.setSelectedSection (selectedSection);
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
    waveDisplay.setSelectedSection (selectedSection);
}

void ChopsEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshFromModel();
}

void ChopsEditor::timerCallback()
{
    auto& engine = chopsProcessor.engine();

    // Every playing voice: the main wave and the pads show full polyphony.
    std::vector<std::pair<int, double>> playing;
    std::vector<int> playingSections;
    for (const auto& slot : engine.uiVoices)
    {
        const auto section = slot.section.load (std::memory_order_relaxed);
        if (section >= 0)
        {
            playing.emplace_back (section, slot.frame.load (std::memory_order_relaxed));
            playingSections.push_back (section);
        }
    }
    waveDisplay.setPlayheads (playing);
    analyseOutput (playingSections);
    padStrip.setActiveSections (std::move (playingSections));

    // Last-triggered slice becomes the edited one (also covers MIDI input).
    // Edge-triggered on the serial: only a new note-on moves the selection, so
    // a short slice ending never hands it back to an older, still-sounding
    // one, and manual pad selection is not overridden between triggers. While
    // a gesture is active the serial is left unconsumed so the trigger still
    // lands once the gesture ends.
    const auto serial = engine.uiTriggerSerial.load (std::memory_order_relaxed);
    if (serial != lastTriggerSerial && ! sliceLane.isGestureActive())
    {
        lastTriggerSerial = serial;
        const auto newest = engine.uiSectionIndex.load (std::memory_order_relaxed);
        if (newest >= 0)
            selectSection (newest);
    }

    // The lane shows its own section's playhead whenever that slice sounds,
    // even if it is not the newest voice.
    double laneFrame = -1.0;
    for (const auto& [section, frame] : playing)
        if (section == sliceLane.boundIndex())
            laneFrame = frame;
    sliceLane.setPlayhead (laneFrame >= 0.0, laneFrame);
}

void ChopsEditor::analyseOutput (const std::vector<int>& playingSections)
{
    if (doc == nullptr || doc->sample == nullptr)
        return;

    auto& engine = chopsProcessor.engine();

    // Reference: the slice with the most playtime since the sample (or the
    // slice layout) changed. A change of reference restarts the reading.
    int ref = -1;
    std::uint64_t most = 0;
    const int count = std::min ((int) doc->sections.size(), chops::Engine::kMaxSections);
    for (int i = 0; i < count; ++i)
    {
        const auto frames = engine.uiPlayFrames[(size_t) i].load (std::memory_order_relaxed);
        if (frames > most)
        {
            most = frames;
            ref = i;
        }
    }
    if (ref != tunerRefSection)
    {
        resetTuner();
        tunerRefSection = ref;
        updateTunerText();
    }

    // Only windows where the reference slice is all that sounds count, so
    // other slices never bleed into its reading. Anything else is silence
    // for the tuner: the reading holds long enough to release a pad and
    // click snap, then clears.
    bool sounding = ref >= 0 && ! playingSections.empty() && chopsProcessor.getSampleRate() > 0.0;
    for (const int s : playingSections)
        sounding = sounding && s == ref;

    if (sounding)
    {
        engine.uiOutputTap.copyLatest (tunerWindow.data(), chops::tune::kWindow);
        double energy = 0.0;
        for (const float v : tunerWindow)
            energy += (double) v * v;
        sounding = std::sqrt (energy / (double) tunerWindow.size()) >= 1.0e-3;   // -60 dBFS
    }

    if (! sounding)
    {
        if (++tunerSilentTicks >= tunerHoldTicks() && (tunerReading.valid || tuneRingCount > 0))
            clearTunerReading();
        return;
    }
    tunerSilentTicks = 0;

    const auto r = chops::tune::analyse (tunerWindow.data(), chops::tune::kWindow,
                                         chopsProcessor.getSampleRate(), *tunerScratch);
    if (r.numPeaks < 3)
        return;

    // Rolling window of resultant vectors (a plain average of cents would
    // break at the +-50 wrap): the reading follows the newest rollTicks.
    tuneRing[(size_t) tuneRingPos] = { r.re, r.im };
    tuneRingPos = (tuneRingPos + 1) % kTunerRingMax;
    tuneRingCount = std::min (tuneRingCount + 1, kTunerRingMax);

    const int used = std::min (tuneRingCount, tunerRollTicks());
    float re = 0.0f, im = 0.0f;
    for (int i = 1; i <= used; ++i)
    {
        const auto& entry = tuneRing[(size_t) ((tuneRingPos - i + kTunerRingMax) % kTunerRingMax)];
        re += entry.first;
        im += entry.second;
    }
    re /= (float) used;
    im /= (float) used;

    const float confidence = std::sqrt (re * re + im * im);
    if (confidence < 0.6f)
        return;

    tunerReading.pitchClass = r.pitchClass;
    tunerReading.cents = (float) (100.0 * std::atan2 (im, re) / (2.0 * juce::MathConstants<double>::pi));
    tunerReading.valid = true;
    snapButton.setEnabled (true);
    updateTunerText();
}

int ChopsEditor::tunerBarTicks() const
{
    const double bpm = chopsProcessor.hostBpm();
    const double beats = chopsProcessor.hostTimeSigNumerator()
                       * (4.0 / chopsProcessor.hostTimeSigDenominator());
    const double seconds = bpm > 0.0 ? beats * 60.0 / bpm : 2.0;
    return (int) std::ceil (seconds * kTunerTimerHz);
}

int ChopsEditor::tunerRollTicks() const
{
    return std::clamp (std::max ((int) (kTunerRollSeconds * kTunerTimerHz), tunerBarTicks()),
                       1, kTunerRingMax);
}

int ChopsEditor::tunerHoldTicks() const
{
    return std::max ((int) (kTunerHoldSeconds * kTunerTimerHz), tunerBarTicks());
}

void ChopsEditor::clearTunerReading()
{
    tunerReading = {};
    tuneRingCount = tuneRingPos = 0;
    tunerSilentTicks = 0;
    snapButton.setEnabled (false);
    updateTunerText();
}

void ChopsEditor::resetTuner()
{
    clearTunerReading();
    tunerRefSection = -1;
}

void ChopsEditor::updateTunerText()
{
    juce::String text;
    if (tunerReading.valid && doc != nullptr && tunerRefSection >= 0
        && tunerRefSection < (int) doc->sections.size())
    {
        const int cents = (int) std::lround (tunerReading.cents);
        text = juce::MidiMessage::getMidiNoteName (doc->sections[(size_t) tunerRefSection].midiNote,
                                                   true, true, 3)
             + "  " + juce::MidiMessage::getMidiNoteName (tunerReading.pitchClass, true, false, 3)
             + " " + (cents >= 0 ? "+" : "") + juce::String (cents) + "%";
    }
    else
    {
        text = "--";
    }

    if (text != tunerText)
    {
        tunerText = text;
        repaint (tunerRect);
    }
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
    buttonRow.removeFromLeft (4);
    cropButton.setBounds (buttonRow.removeFromLeft (64));
    buttonRow.removeFromLeft (12);
    polyButton.setBounds (buttonRow.removeFromLeft (48));
    monoButton.setBounds (buttonRow.removeFromLeft (48));
    buttonRow.removeFromLeft (12);
    velButton.setBounds (buttonRow.removeFromLeft (48));

    // Same reading order as the slice lane's knob grid: pitch, fine, drive,
    // gain, sr.
    auto knobArea = topBar.removeFromRight (5 * chops::ui::kKnobW).withHeight (chops::ui::kKnobH);
    for (auto* k : { &globalPitch, &globalFine, &globalDrive, &globalGain, &globalSr })
        k->setBounds (knobArea.removeFromLeft (chops::ui::kKnobW));

    bounds.removeFromTop (8);
    // Bottom info line: hint text left, tuner readout + snap right.
    auto info = bounds.removeFromBottom (22);
    snapButton.setBounds (info.removeFromRight (52).withSizeKeepingCentre (52, 20));
    info.removeFromRight (6);
    tunerRect = info.removeFromRight (128);
    info.removeFromRight (12);
    infoRect = info;

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
    g.setColour (juce::Colours::whitesmoke.withAlpha (0.55f));
    g.setFont (chops::ui::kFontLabel);
    {
        const std::pair<const juce::Slider*, const char*> labels[] =
            { { &globalPitch, "pitch" }, { &globalFine, "fine" }, { &globalDrive, "drive" },
              { &globalGain, "gain" }, { &globalSr, "sr" } };
        for (const auto& [knob, text] : labels)
            g.drawText (text,
                        knob->getBounds().withY (knob->getBottom())
                            .withHeight (chops::ui::kKnobLabelH),
                        juce::Justification::centred);
    }

    const auto info = infoRect;
    g.setFont (chops::ui::kFontLabel);

    // Tuner readout: accent when the output sits on the grid.
    {
        const bool onGrid = tunerReading.valid && std::abs (tunerReading.cents) <= 2.0f;
        g.setColour (! tunerReading.valid ? juce::Colours::whitesmoke.withAlpha (0.35f)
                     : onGrid             ? juce::Colour (0xff5ec8a8)
                                          : juce::Colours::whitesmoke.withAlpha (0.85f));
        g.drawText (tunerText, tunerRect, juce::Justification::centredRight);
    }

    if (doc != nullptr && doc->sample != nullptr)
    {
        const auto& smp = *doc->sample;
        g.setColour (juce::Colours::whitesmoke.withAlpha (0.7f));
        g.drawText (juce::File (smp.originalPath).getFileName()
                        + "   " + juce::String (smp.sourceSampleRate / 1000.0, 1) + " kHz"
                        + "   " + juce::String (doc->sections.size()) + " slice(s)"
                        + "   wave: click adds slice, drag handles, wheel zooms · lane: drag"
                        + " selects loop, inside moves it, edges adjust, double-click clears",
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
