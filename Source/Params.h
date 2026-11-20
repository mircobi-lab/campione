#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Parametri di Campione. Gli ID restano stabili: sono salvati nei progetti e nelle mappe MIDI Learn.
namespace campione
{
namespace id
{
    inline constexpr const char* tone      = "TONE";      // -1 scuro .. +1 brillante (inclinazione EQ)
    inline constexpr const char* tune      = "TUNE";      // centesimi -50 .. +50
    inline constexpr const char* transpose = "TRANSP";    // semitoni -12 .. +12
    inline constexpr const char* a4ref     = "A4REF";     // 440 Hz / 432 Hz
    inline constexpr const char* velCurve  = "VELCURVE";  // NORMAL / SOFT / HARD / FIXED
    inline constexpr const char* sf2Env    = "SF2ENV";    // usa l'inviluppo del SoundFont
    inline constexpr const char* attack    = "ATTACK";
    inline constexpr const char* decay     = "DECAY";
    inline constexpr const char* sustain   = "SUSTAIN";
    inline constexpr const char* release   = "RELEASE";
    inline constexpr const char* pedal     = "PEDAL";     // sustain dal pannello (si somma al CC 64)
    inline constexpr const char* soft      = "SOFT";      // pedale una corda dal pannello (si somma al CC 67)
    inline constexpr const char* softAmt   = "SOFTAMT";
    inline constexpr const char* roomOn    = "ROOM";
    inline constexpr const char* roomSize  = "ROOMSIZE";
    inline constexpr const char* roomDamp  = "ROOMDAMP";
    inline constexpr const char* roomMix   = "ROOMMIX";
    inline constexpr const char* volume    = "VOLUME";    // dB
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

// Testo breve del valore per il pannello (maiuscolo, unità dopo uno spazio).
juce::String formatValue (const juce::String& paramId, float value);

// Diapason: scostamento in semitoni dal La a 440 Hz.
inline float a4Offset (int choice) { return choice == 1 ? 12.f * std::log2 (432.f / 440.f) : 0.f; }
}
