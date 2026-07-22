#pragma once

#include <juce_core/juce_core.h>
#include <memory>

#include "../model/Document.h"

namespace chops::state
{

// Decode an audio file and build the compressed blob that will live inside
// the plugin state. Returns nullptr and fills `error` on failure.
// Already-compressed sources (flac/ogg/mp3/...) are embedded verbatim;
// PCM sources are FLAC-encoded once, here, and never re-encoded on save.
std::shared_ptr<SampleData> loadSampleFile (const juce::File& file, juce::String& error);

// Full document <-> plugin state chunk.
void toMemory (const Document& doc, juce::MemoryBlock& dest);
std::unique_ptr<Document> fromMemory (const void* data, size_t size, juce::String& error);

// Hard-discard all audio outside the sections' overall [min start, max end)
// and re-encode the embedded blob, shifting every section and loop. Lets long
// files be trimmed right after loading so plugin state never carries minutes
// of unused audio. Returns false (doc untouched) if there is nothing to crop.
bool cropToSections (Document& doc, juce::String& error);

} // namespace chops::state
