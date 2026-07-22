#include "Engine.h"

#include <cmath>

namespace chops
{

void Engine::prepare (double sampleRate, int)
{
    hostRate = sampleRate;
    for (auto& v : voices)
        v.reset();
    lastDoc = nullptr;
}

void Engine::publishDocument (std::unique_ptr<const Document> doc)
{
    swap.publish (std::move (doc));
}

void Engine::reclaim()
{
    swap.reclaim();
}

static double resolvePlaybackRate (const Section& sec, const GlobalParams& g,
                                   double sourceRate, double hostRate) noexcept
{
    const double semis = (double) (sec.pitchSemis + g.pitchSemis)
                       + (double) (sec.fineCents + g.fineCents) / 100.0;
    return std::exp2 (semis / 12.0) * sourceRate / hostRate;
}

static VoiceFx resolveFx (const Section& sec, const GlobalParams& g,
                          double sourceRate, double hostRate) noexcept
{
    VoiceFx fx;
    fx.rate = resolvePlaybackRate (sec, g, sourceRate, hostRate);
    fx.decimHz = sec.srOverride > 0.0 ? sec.srOverride : g.srReduce;
    fx.drive = sec.driveOverride >= 0.0f ? sec.driveOverride : g.drive;
    fx.gain = sec.gain * g.gain;
    return fx;
}

void Engine::startSectionVoice (const Document& doc, int sectionIdx, int note, float velocity) noexcept
{
    // Mono per section: a retrigger cuts its own predecessor with a fast fade.
    for (auto& v : voices)
        if (v.isActive() && v.sectionIndex() == sectionIdx)
            v.fastFade();

    Voice* target = nullptr;
    for (auto& v : voices)
        if (! v.isActive())
        {
            target = &v;
            break;
        }

    if (target == nullptr)
    {
        // Steal the oldest voice. It gets restarted immediately, so the fade
        // is skipped — the attack ramp of the new note masks the transition.
        Voice* oldest = &voices[0];
        for (auto& v : voices)
            if (v.serial() < oldest->serial())
                oldest = &v;
        target = oldest;
    }

    const auto& sec = doc.sections[(size_t) sectionIdx];
    const auto& smp = *doc.sample;

    VoiceParams p;
    p.start = sec.start;
    p.end = sec.end;
    p.loopStart = sec.loopStart;
    p.loopEnd = sec.loopEnd;
    p.mode = sec.mode;
    p.reverse = sec.reverse;
    p.xfadeFrames = sec.xfadeFrames;

    target->start (makeSampleView (smp), p,
                   resolveFx (sec, doc.global, smp.sourceSampleRate, hostRate),
                   note, velocity, hostRate, sectionIdx, nextSerial++);
}

void Engine::handleMidi (const Document* doc, const juce::uint8* data, int numBytes) noexcept
{
    if (numBytes < 2)
        return;

    const auto status = (juce::uint8) (data[0] & 0xf0);

    if (status == 0x90 && numBytes >= 3 && data[2] > 0)
    {
        if (doc != nullptr && doc->sample != nullptr)
        {
            const int note = data[1];
            const float velocity = (float) data[2] / 127.0f;

            for (int i = 0; i < (int) doc->sections.size(); ++i)
                if (doc->sections[(size_t) i].midiNote == note)
                    startSectionVoice (*doc, i, note, velocity);
        }
    }
    else if (status == 0x80 || (status == 0x90 && numBytes >= 3 && data[2] == 0))
    {
        const int note = data[1];
        for (auto& v : voices)
            if (v.isActive() && v.note() == note)
                v.noteOff();
    }
    else if (status == 0xb0 && numBytes >= 2
             && (data[1] == 120 || data[1] == 123))   // all sound off / all notes off
    {
        stopAllVoices();
    }
}

void Engine::stopAllVoices() noexcept
{
    for (auto& v : voices)
        if (v.isActive())
            v.fastFade();
}

void Engine::renderSpan (juce::AudioBuffer<float>& buffer, int start, int numFrames) noexcept
{
    if (numFrames <= 0)
        return;

    float* outL = buffer.getWritePointer (0) + start;
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) + start : nullptr;

    for (auto& v : voices)
        v.render (outL, outR, numFrames);
}

void Engine::process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi) noexcept
{
    const Document* doc = swap.acquire();

    if (doc != lastDoc)
    {
        // Fade out voices whose sample data is no longer owned by the current
        // document; RealtimeSwap keeps the old data alive far longer than the fade.
        const void* currentId = (doc != nullptr && doc->sample != nullptr)
                                    ? makeSampleView (*doc->sample).channels
                                    : nullptr;
        for (auto& v : voices)
            if (v.isActive() && v.sampleId() != currentId)
                v.fastFade();

        lastDoc = doc;
    }

    // Live tuning: refresh every running voice's FX and loop region from the
    // current document, so parameter and loop-point tweaks land on sustaining
    // notes, not just new ones.
    if (doc != nullptr && doc->sample != nullptr)
    {
        const void* currentId = makeSampleView (*doc->sample).channels;
        for (auto& v : voices)
        {
            if (v.isActive() && v.sampleId() == currentId
                && v.sectionIndex() >= 0 && v.sectionIndex() < (int) doc->sections.size())
            {
                const auto& sec = doc->sections[(size_t) v.sectionIndex()];
                v.updateFx (resolveFx (sec, doc->global,
                                       doc->sample->sourceSampleRate, hostRate));
                v.updateLoop (sec.loopStart, sec.loopEnd, sec.xfadeFrames);
            }
        }
    }

    const int total = buffer.getNumSamples();
    int pos = 0;

    for (const auto metadata : midi)
    {
        const int eventPos = juce::jlimit (0, total, metadata.samplePosition);
        renderSpan (buffer, pos, eventPos - pos);
        pos = eventPos;
        handleMidi (doc, metadata.data, metadata.numBytes);
    }

    renderSpan (buffer, pos, total - pos);

    // Publish the most recently started active voice for the UI playhead.
    const Voice* newest = nullptr;
    for (const auto& v : voices)
        if (v.isActive() && (newest == nullptr || v.serial() > newest->serial()))
            newest = &v;

    uiSectionIndex.store (newest != nullptr ? newest->sectionIndex() : -1,
                          std::memory_order_relaxed);
    uiPositionFrames.store (newest != nullptr ? newest->position() : -1.0,
                            std::memory_order_relaxed);

    swap.endBlock();
}

} // namespace chops
