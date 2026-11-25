#pragma once
#include <juce_core/juce_core.h>

/*  Da SFZ a SoundFont 2 in memoria: Campione suona gli SFZ con lo stesso motore dei file .sf2.
    Il file .sfz viene letto, i campioni (WAV, AIFF, FLAC, OGG) decodificati a 16 bit e tutto viene scritto
    come un SF2 con un preset e uno strumento, una zona per regione (due per i campioni stereo).

    Opcode letti: sample, default_path, note_offset, octave_offset, lokey, hikey, key, lovel, hivel,
    pitch_keycenter, pitch_keytrack (100 o 0), transpose, tune, volume, amplitude, pan, offset, end,
    loop_mode, loop_start/loopstart, loop_end/loopend, ampeg_delay, ampeg_attack, ampeg_hold, ampeg_decay,
    ampeg_sustain, ampeg_release, group con off_by uguale (gruppo esclusivo), #define e #include.
    Regioni saltate perché un SoundFont non le può esprimere: trigger=release, seq_position oltre 1,
    lorand sopra 0, sw_* (keyswitch) diversi dal tasto predefinito. Il conteggio finisce in 'notes'. */
namespace campione::sfz
{
struct Result
{
    juce::MemoryBlock sf2;
    juce::String error;       // vuoto se è andato tutto bene
    int regions = 0, skipped = 0, samples = 0;
    juce::StringArray notes;  // avvisi da mostrare (campioni mancanti, regioni saltate)
};

Result convert (const juce::File& sfzFile);
}
