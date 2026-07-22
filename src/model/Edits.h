#pragma once

#include "Document.h"

// Message-thread editing operations. Each takes a mutable Document copy;
// the caller publishes the result. All operations keep sections sorted by
// start and re-assign pad notes chromatically from the root note.
namespace chops::edits
{

constexpr juce::int64 kMinSectionFrames = 128;

void remapNotesChromatic (Document& doc);

// Click in the main waveform: split the section containing `frame`.
bool splitAt (Document& doc, juce::int64 frame);

// Drag a slice marker (the start of sections[index], index >= 1). Keeps the
// partition linked: if the previous section ended exactly here, its end
// follows. Returns the clamped position actually applied.
bool moveSectionStart (Document& doc, int index, juce::int64 newStart);

// Double-click a marker: remove that section, merging its range into the
// previous section when they were contiguous.
bool removeSection (Document& doc, int index);

// Back to one whole-file section.
void clearSlices (Document& doc);

void autoSliceEqual (Document& doc, int parts);

// Energy-flux onset detection; sensitivity 0..1 (higher = more slices).
void autoSliceTransients (Document& doc, float sensitivity);

} // namespace chops::edits
