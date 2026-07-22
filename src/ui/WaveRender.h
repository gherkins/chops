#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PeakCache.h"

namespace chops::render
{

// Draw the wave for [viewStart, viewStart + viewLength) frames into `area`.
// Uses peak columns normally; switches to a sample-level path when zoomed in
// far enough that individual samples are wider than half a pixel.
inline void drawWave (juce::Graphics& g, juce::Rectangle<float> area,
                      const juce::AudioBuffer<float>& buffer, const PeakCache& peaks,
                      double viewStart, double viewLength, juce::Colour colour)
{
    if (viewLength <= 0.0 || area.isEmpty())
        return;

    const float midY = area.getCentreY();
    const float halfHeight = area.getHeight() * 0.46f;
    const double framesPerPixel = viewLength / (double) area.getWidth();

    const auto frameToX = [&] (double frame)
    {
        return area.getX() + (float) ((frame - viewStart) / viewLength * (double) area.getWidth());
    };

    g.setColour (colour);

    if (framesPerPixel < 2.0)
    {
        juce::Path path;
        const float* data = buffer.getReadPointer (0);
        const auto total = (juce::int64) buffer.getNumSamples();
        bool started = false;

        for (auto f = std::max ((juce::int64) 0, (juce::int64) viewStart - 1);
             f <= std::min (total - 1, (juce::int64) (viewStart + viewLength) + 1); ++f)
        {
            const auto x = frameToX ((double) f);
            const auto y = midY - data[f] * halfHeight;
            if (! started)
            {
                path.startNewSubPath (x, y);
                started = true;
            }
            else
            {
                path.lineTo (x, y);
            }
        }

        g.strokePath (path, juce::PathStrokeType (1.5f));
    }
    else
    {
        const int width = (int) area.getWidth();
        for (int x = 0; x < width; ++x)
        {
            const auto begin = (juce::int64) (viewStart + (double) x * framesPerPixel);
            const auto end = (juce::int64) (viewStart + (double) (x + 1) * framesPerPixel);
            float lo, hi;
            peaks.query (buffer, begin, end, lo, hi);
            const float top = midY - hi * halfHeight;
            const float bottom = midY - lo * halfHeight;
            g.fillRect (area.getX() + (float) x, top, 1.0f, std::max (bottom - top, 1.0f));
        }
    }
}

} // namespace chops::render
