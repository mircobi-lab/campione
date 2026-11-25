#include "Banks.h"

namespace campione
{
void Banks::prepare (double sampleRate, int maxBlockSize)
{
    for (auto& e : engines) e.prepare (sampleRate, maxBlockSize);
    for (auto& m : midiFor) { m.clear(); m.ensureSize (8192); }
    noteBank.fill (-1);
    mix.setSize (2, juce::jmax (32, maxBlockSize));
}

void Banks::requestPanic() noexcept
{
    for (auto& e : engines) e.requestPanic();
    noteBank.fill (-1);
}

int Banks::totalVoices() const noexcept
{
    int n = 0;
    for (auto& e : engines) n += e.activeVoices();
    return n;
}

void Banks::render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, int active, const EngineSettings& s) noexcept
{
    active = juce::jlimit (0, count - 1, active);
    for (auto& m : midiFor) m.clear();

    // smistamento del MIDI tra i banchi
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int pos = meta.samplePosition;
        if (m.isNoteOn())
        {
            noteBank[(size_t) m.getNoteNumber()] = active;
            midiFor[(size_t) active].addEvent (m, pos);
        }
        else if (m.isNoteOff())
        {
            const int b = noteBank[(size_t) m.getNoteNumber()];
            midiFor[(size_t) (b >= 0 ? b : active)].addEvent (m, pos);
            noteBank[(size_t) m.getNoteNumber()] = -1;
        }
        else if (m.isProgramChange() || (m.isController() && (m.getControllerNumber() == 0 || m.getControllerNumber() == 32)))
        {
            midiFor[(size_t) active].addEvent (m, pos);
        }
        else
        {
            for (auto& mb : midiFor) mb.addEvent (m, pos);
        }
    }

    // ogni banco caricato suona nel suo buffer e si somma all'uscita
    const int n = out.getNumSamples(), chans = out.getNumChannels();
    out.clear();
    for (int b = 0; b < count; ++b)
    {
        auto& e = engines[(size_t) b];
        if (! e.isLoaded()) continue;
        for (int start = 0; start < n;)
        {
            const int len = juce::jmin (n - start, mix.getNumSamples());
            juce::AudioBuffer<float> view (mix.getArrayOfWritePointers(), juce::jmin (2, chans), len);
            // gli eventi di questo pezzo, con la posizione spostata all'inizio del pezzo
            if (start == 0 && len == n)
                e.render (view, midiFor[(size_t) b], s);
            else
            {
                juce::MidiBuffer part;   // caso raro (blocco dell'host più lungo di quello preparato)
                for (const auto meta : midiFor[(size_t) b])
                    if (meta.samplePosition >= start && meta.samplePosition < start + len)
                        part.addEvent (meta.getMessage(), meta.samplePosition - start);
                e.render (view, part, s);
            }
            for (int c = 0; c < juce::jmin (2, chans); ++c) out.addFrom (c, start, view, c, 0, len);
            start += len;
        }
    }
}
}
