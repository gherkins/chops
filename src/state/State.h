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

} // namespace chops::state
