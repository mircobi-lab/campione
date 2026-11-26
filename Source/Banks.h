#pragma once
#include "SoundFontEngine.h"
#include <array>

namespace campione
{
/*  Quattro banchi (A, B, C, D), ognuno con il suo SoundFont e il suo preset, tutti caricati in memoria.
    Suona il banco attivo; il cambio di banco è istantaneo e non taglia le note già suonate:
    - una nota si spegne sempre nel banco in cui è partita (anche se nel frattempo il banco è cambiato)
    - pedali, pitch bend, volume MIDI, CC 120 e 123 vanno a tutti i banchi
    - Program Change e scelta del banco MIDI (CC 0 e 32) vanno solo al banco attivo
    Tutto senza allocazioni nel thread audio: i buffer si preparano in prepare().                       */
class Banks
{
public:
    static constexpr int count = 4;

    void prepare (double sampleRate, int maxBlockSize);
    void render (juce::AudioBuffer<float>&, const juce::MidiBuffer&, int activeBank, const EngineSettings&) noexcept;
    void requestPanic() noexcept;

    SoundFontEngine& operator[] (int i) noexcept { return engines[(size_t) juce::jlimit (0, count - 1, i)]; }
    const SoundFontEngine& operator[] (int i) const noexcept { return engines[(size_t) juce::jlimit (0, count - 1, i)]; }
    int totalVoices() const noexcept;

    static juce::String name (int i) { return juce::String::charToString ((juce::juce_wchar) ('A' + i)); }

private:
    std::array<SoundFontEngine, count> engines;
    std::array<juce::MidiBuffer, count> midiFor;
    std::array<int, 16 * 128> noteBank {};   // per canale MIDI e tasto: il banco in cui la nota è partita
    juce::AudioBuffer<float> mix;
};
}
