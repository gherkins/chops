#include "PluginEditor.h"

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

    doc = chopsProcessor.document();
    rebuildPeaks();
    chopsProcessor.addChangeListener (this);
    startTimerHz (30);
}

ChopsEditor::~ChopsEditor()
{
    chopsProcessor.removeChangeListener (this);
}

void ChopsEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    doc = chopsProcessor.document();
    rebuildPeaks();
    repaint();
}

juce::Rectangle<int> ChopsEditor::waveArea() const
{
    return getLocalBounds().reduced (16).withTrimmedBottom (getHeight() / 3);
}

void ChopsEditor::rebuildPeaks()
{
    peakMin.assign (kNumBins, 0.0f);
    peakMax.assign (kNumBins, 0.0f);

    if (doc == nullptr || doc->sample == nullptr)
        return;

    const auto& buffer = doc->sample->buffer;
    const auto numFrames = (juce::int64) buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    for (int bin = 0; bin < kNumBins; ++bin)
    {
        const auto begin = numFrames * bin / kNumBins;
        const auto end = std::max (begin + 1, numFrames * (bin + 1) / kNumBins);
        float lo = 0.0f, hi = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* data = buffer.getReadPointer (ch);
            for (auto i = begin; i < end; ++i)
            {
                lo = std::min (lo, data[i]);
                hi = std::max (hi, data[i]);
            }
        }

        peakMin[(size_t) bin] = lo;
        peakMax[(size_t) bin] = hi;
    }
}

void ChopsEditor::timerCallback()
{
    const auto area = waveArea().toFloat();
    float x = -1.0f;

    if (doc != nullptr && doc->sample != nullptr)
    {
        const auto pos = chopsProcessor.engine().uiPositionFrames.load (std::memory_order_relaxed);
        const auto total = (double) doc->sample->numFrames();
        if (pos >= 0.0 && total > 0.0)
            x = area.getX() + (float) (pos / total) * area.getWidth();
    }

    if (! juce::approximatelyEqual (x, lastPlayheadX))
    {
        lastPlayheadX = x;
        repaint (waveArea().expanded (2));
    }
}

void ChopsEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff17181c));

    const auto area = waveArea();
    g.setColour (juce::Colour (0xff22242a));
    g.fillRect (area);

    if (doc != nullptr && doc->sample != nullptr)
    {
        const auto areaF = area.toFloat();
        const float midY = areaF.getCentreY();
        const float halfH = areaF.getHeight() * 0.48f;

        g.setColour (juce::Colour (0xff5ec8a8));
        const float binWidth = areaF.getWidth() / (float) kNumBins;

        for (int bin = 0; bin < kNumBins; ++bin)
        {
            const float x = areaF.getX() + binWidth * (float) bin;
            const float top = midY - peakMax[(size_t) bin] * halfH;
            const float bottom = midY - peakMin[(size_t) bin] * halfH;
            g.fillRect (x, top, std::max (binWidth, 1.0f), std::max (bottom - top, 1.0f));
        }

        if (lastPlayheadX >= areaF.getX())
        {
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.fillRect (lastPlayheadX, areaF.getY(), 1.5f, areaF.getHeight());
        }

        const auto& smp = *doc->sample;
        g.setColour (juce::Colours::whitesmoke);
        g.setFont (14.0f);
        g.drawText (juce::File (smp.originalPath).getFileName()
                        + "   " + juce::String (smp.sourceSampleRate / 1000.0, 1) + " kHz"
                        + "   " + juce::String (smp.numFrames()) + " frames"
                        + "   pad: " + juce::MidiMessage::getMidiNoteName (
                              doc->global.rootNote, true, true, 3)
                        + "   (hold click to audition)",
                    getLocalBounds().reduced (16).removeFromBottom (24),
                    juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (juce::Colours::whitesmoke.withAlpha (dragOver ? 1.0f : 0.6f));
        g.setFont (22.0f);
        g.drawText ("drop an audio file", area, juce::Justification::centred);

        const auto error = chopsProcessor.lastError();
        if (error.isNotEmpty())
        {
            g.setColour (juce::Colours::orangered);
            g.setFont (14.0f);
            g.drawText (error, getLocalBounds().reduced (16).removeFromBottom (24),
                        juce::Justification::centredLeft);
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

void ChopsEditor::mouseDown (const juce::MouseEvent& e)
{
    if (doc != nullptr && doc->sample != nullptr && waveArea().contains (e.getPosition()))
    {
        auditioning = true;
        chopsProcessor.triggerPad (doc->global.rootNote, true);
    }
}

void ChopsEditor::mouseUp (const juce::MouseEvent&)
{
    if (auditioning)
    {
        auditioning = false;
        chopsProcessor.triggerPad (doc->global.rootNote, false);
    }
}
