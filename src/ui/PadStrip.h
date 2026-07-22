#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <vector>

#include "../model/Document.h"

namespace chops
{

// One pad per section: shows the assigned note, lights while playing,
// click-and-hold auditions (gate-style, like hitting the MIDI pad). Also the
// selector for which slice the single lane editor below shows.
class PadStrip : public juce::Component
{
public:
    std::function<void (int sectionIndex, bool noteOn)> onPad;

    void setDocument (std::shared_ptr<const Document> newDoc)
    {
        doc = std::move (newDoc);
        repaint();
    }

    // Every currently playing section — polyphony lights multiple pads.
    void setActiveSections (std::vector<int> sectionIndices)
    {
        if (activeSections != sectionIndices)
        {
            activeSections = std::move (sectionIndices);
            repaint();
        }
    }

    void setSelectedSection (int sectionIndex)
    {
        if (selectedSection != sectionIndex)
        {
            selectedSection = sectionIndex;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        if (doc == nullptr || doc->sample == nullptr || doc->sections.empty())
            return;

        const auto pads = (int) doc->sections.size();
        auto bounds = getLocalBounds().toFloat();
        const float padWidth = bounds.getWidth() / (float) pads;

        for (int i = 0; i < pads; ++i)
        {
            auto pad = juce::Rectangle<float> (bounds.getX() + padWidth * (float) i,
                                               bounds.getY(), padWidth, bounds.getHeight())
                           .reduced (2.0f);

            const bool lit = i == pressedIndex
                             || std::find (activeSections.begin(), activeSections.end(), i)
                                    != activeSections.end();
            g.setColour (lit ? juce::Colour (0xff5ec8a8) : juce::Colour (0xff2b2e36));
            g.fillRoundedRectangle (pad, 4.0f);

            if (i == selectedSection)
            {
                g.setColour (juce::Colour (0xff5ec8a8));
                g.drawRoundedRectangle (pad, 4.0f, 1.5f);
            }

            g.setColour (lit ? juce::Colour (0xff17181c) : juce::Colours::whitesmoke.withAlpha (0.8f));
            g.setFont (juce::jmin (13.0f, pad.getHeight() * 0.3f));
            g.drawText (juce::MidiMessage::getMidiNoteName (
                            doc->sections[(size_t) i].midiNote, true, true, 3),
                        pad, juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        pressedIndex = padAt (e.getPosition());
        if (pressedIndex >= 0 && onPad != nullptr)
            onPad (pressedIndex, true);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (pressedIndex >= 0 && onPad != nullptr)
            onPad (pressedIndex, false);
        pressedIndex = -1;
        repaint();
    }

private:
    int padAt (juce::Point<int> pos) const
    {
        if (doc == nullptr || doc->sections.empty() || getWidth() <= 0)
            return -1;

        const auto index = pos.x * (int) doc->sections.size() / getWidth();
        return index >= 0 && index < (int) doc->sections.size() ? index : -1;
    }

    std::shared_ptr<const Document> doc;
    std::vector<int> activeSections;
    int selectedSection = -1;
    int pressedIndex = -1;
};

} // namespace chops
