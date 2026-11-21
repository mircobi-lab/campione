// Prova offline del motore di Campione: carica un SF2, controlla intonazione, pedale, inviluppo, cambi di programma e PANIC,
// poi scrive un accordo in WAV. Uso: campione_render FILE.sf2 [uscita.wav]   (codice di uscita 0 = tutte le prove passate)
#include "SoundFontEngine.h"
#include <juce_audio_formats/juce_audio_formats.h>

using namespace campione;

namespace
{
constexpr double sr = 48000.0;
constexpr int block = 256;
int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  OK    " : "  FALLITA ") << what << std::endl;
    if (! ok) ++failures;
}

struct Event { double at; juce::MidiMessage msg; };

// suona gli eventi per 'seconds' secondi e restituisce l'uscita stereo
juce::AudioBuffer<float> play (SoundFontEngine& e, std::vector<Event> events, double seconds, const EngineSettings& s = {})
{
    const int total = (int) (seconds * sr);
    juce::AudioBuffer<float> out (2, total), buf (2, block);
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        juce::MidiBuffer midi;
        for (auto& ev : events)
        {
            const int at = (int) (ev.at * sr);
            if (at >= pos && at < pos + n) midi.addEvent (ev.msg, at - pos);
        }
        buf.setSize (2, n, false, false, true);
        e.render (buf, midi, s);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, buf, c, 0, n);
    }
    return out;
}

float rms (const juce::AudioBuffer<float>& b, double from, double to)
{
    return b.getRMSLevel (0, (int) (from * sr), juce::jmax (1, (int) ((to - from) * sr)));
}

// frequenza dai passaggi per lo zero (sale), sulla finestra indicata
double pitch (const juce::AudioBuffer<float>& b, double from, double to)
{
    const auto* d = b.getReadPointer (0);
    int first = -1, last = -1, count = 0;
    for (int i = (int) (from * sr) + 1; i < (int) (to * sr); ++i)
        if (d[i - 1] < 0.f && d[i] >= 0.f) { if (first < 0) first = i; last = i; ++count; }
    return count > 1 ? (count - 1) * sr / (double) (last - first) : 0.0;
}

juce::MidiMessage on (int n, float v = .8f) { return juce::MidiMessage::noteOn (1, n, v); }
juce::MidiMessage off (int n) { return juce::MidiMessage::noteOff (1, n); }
}

int main (int argc, char* argv[])
{
    if (argc < 2) { std::cout << "uso: campione_render FILE.sf2 [uscita.wav]" << std::endl; return 2; }
    SoundFontEngine e;
    e.prepare (sr, block);
    juce::String err;
    const bool ok = e.loadFile (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]), err);
    check (ok, "caricamento " + juce::String (argv[1]) + (ok ? "" : " (" + err + ")"));
    if (! ok) return 1;
    e.prepare (sr, block);
    for (auto& p : e.presets()) std::cout << "    " << juce::String (p.bank).paddedLeft ('0', 3) << ":" << juce::String (p.number).paddedLeft ('0', 3) << "  " << p.name << std::endl;
    check (e.currentPreset() >= 0, "preset iniziale (banco 0, programma 0 se esiste)");

    // intonazione: La4 = 440 Hz, trasposizione, diapason 432
    auto a = play (e, { { 0.0, on (69) }, { 1.0, off (69) } }, 1.6);
    const double f440 = pitch (a, .1, .6);
    check (std::abs (f440 - 440.0) < 2.0, "La4 a " + juce::String (f440, 1) + " Hz (atteso 440)");
    check (rms (a, .1, .5) > .01f, "segnale presente, RMS " + juce::String (rms (a, .1, .5), 4));
    EngineSettings tr; tr.transpose = 12;
    const double f880 = pitch (play (e, { { 0.0, on (69) }, { 1.0, off (69) } }, 1.2, tr), .1, .6);
    check (std::abs (f880 - 880.0) < 4.0, "TRANSP +12: " + juce::String (f880, 1) + " Hz (atteso 880)");
    EngineSettings t432; t432.tuningSemis = 12.f * std::log2 (432.f / 440.f);
    auto b432 = play (e, { { 0.0, on (69) }, { 1.0, off (69) } }, 1.2, t432);
    const double f432 = pitch (b432, .1, .6);
    check (std::abs (f432 - 432.0) < 2.0, "A4 REF 432: " + juce::String (f432, 1) + " Hz (atteso 432)");

    // pedale: nota rilasciata a 0,2 s, col sustain deve suonare ancora a 1 s
    const float dry = rms (play (e, { { 0.0, on (60) }, { .2, off (60) } }, 1.4), 1.0, 1.2);
    EngineSettings ped; ped.pedalButton = true;
    const float held = rms (play (e, { { 0.0, on (60) }, { .2, off (60) } }, 1.4, ped), 1.0, 1.2);
    check (held > dry * 4.f + 1e-5f, "SUSTAIN dal pannello: " + juce::String (held, 5) + " contro " + juce::String (dry, 5));
    const float cc = rms (play (e, { { 0.0, juce::MidiMessage::controllerEvent (1, 64, 127) }, { 0.0, on (60) }, { .2, off (60) } }, 1.4), 1.0, 1.2);
    check (cc > dry * 4.f + 1e-5f, "SUSTAIN dal CC 64: " + juce::String (cc, 5));
    play (e, { { 0.0, juce::MidiMessage::controllerEvent (1, 64, 0) } }, .1);   // pedale su

    // inviluppo sostituito: rilascio di 10 ms contro quello del SoundFont
    EngineSettings env; env.sf2Envelope = false; env.attack = 0.f; env.decay = 5.f; env.sustain = 1.f; env.release = .01f;
    const float tailSf2 = rms (play (e, { { 0.0, on (60) }, { .3, off (60) } }, .8), .45, .6);
    const float tailShort = rms (play (e, { { 0.0, on (60) }, { .3, off (60) } }, .8, env), .45, .6);
    check (tailShort < tailSf2 * .1f + 1e-6f, "RELEASE 10 ms (USE SF2 ENV spento): coda " + juce::String (tailShort, 6) + " contro " + juce::String (tailSf2, 6));
    const float backTail = rms (play (e, { { 0.0, on (60) }, { .3, off (60) } }, .8), .45, .6);
    check (std::abs (backTail - tailSf2) < tailSf2 * .05f + 1e-6f, "inviluppo del SoundFont ripristinato");

    // curva di velocity e pedale una corda
    const float loud = rms (play (e, { { 0.0, on (60, .5f) }, { .4, off (60) } }, .4), .05, .3);
    EngineSettings hard; hard.velCurve = 2;
    const float soft = rms (play (e, { { 0.0, on (60, .5f) }, { .4, off (60) } }, .4, hard), .05, .3);
    check (soft < loud, "VEL CURVE HARD più piano a velocity 64");
    EngineSettings una; una.softButton = true; una.softAmount = 1.f;
    const float corda = rms (play (e, { { 0.0, on (60, .5f) }, { .4, off (60) } }, .4, una), .05, .3);
    check (corda < loud, "SOFT pedal abbassa il volume");

    // cambio di programma (banco 0, programma 16 se esiste) e PANIC
    const int before = e.currentPreset();
    play (e, { { 0.0, juce::MidiMessage::programChange (1, 16) } }, .05);
    std::cout << "    preset dopo Program Change 16: " << e.currentPreset() << " (prima " << before << ")" << std::endl;
    auto org = play (e, { { 0.0, on (69) } }, 1.5);
    e.requestPanic();
    auto after = play (e, {}, .3);
    // TinySoundFont chiude le voci con una dissolvenza di pochi millisecondi (niente clic): dopo 20 ms dev'essere silenzio
    const int from = (int) (.02 * sr);
    check (after.getMagnitude (from, after.getNumSamples() - from) < 1e-6f, "PANIC: silenzio entro 20 ms");
    check (e.activeVoices() == 0, "PANIC: nessuna voce attiva");
    e.selectPreset (before);
    play (e, {}, .01);
    check (e.currentPreset() == before, "selectPreset dal pannello");

    // 88 tasti diversi col sustain premuto (organo in loop: le voci restano tutte occupate): l'ultima nota deve suonare
    {
        int organ = -1;
        const auto list = e.presets();
        for (size_t i = 0; i < list.size(); ++i) if (list[i].bank == 0 && list[i].number == 16) organ = (int) i;
        if (organ < 0) organ = before;
        e.selectPreset (organ);
        std::vector<Event> evs { { 0.0, juce::MidiMessage::controllerEvent (1, 64, 127) } };
        double t = .01;
        for (int k = 21; k <= 108; ++k, t += .02) { evs.push_back ({ t, on (k, .7f) }); evs.push_back ({ t + .01, off (k) }); }
        play (e, evs, t + .05);
        check (e.voicesOnKey (108) > 0, "SUSTAIN + 88 tasti: suona anche l'ultimo, C8 (" + juce::String (e.activeVoices()) + " voci attive)");
        e.requestPanic();
        play (e, { { 0.0, juce::MidiMessage::controllerEvent (1, 64, 0) } }, .1);
    }
    // raffica: 160 note ogni 5 ms su tasti diversi, molte voci in rilascio da togliere: niente salti nel segnale
    {
        e.selectPreset (before);
        std::vector<Event> evs;
        double t = .01;
        for (int i = 0; i < 160; ++i, t += .005) { const int k = 30 + (i * 13) % 70; evs.push_back ({ t, on (k, .9f) }); evs.push_back ({ t + .004, off (k) }); }
        auto burst = play (e, evs, t + .3);
        // click = salto di un campione molto più grande della variazione normale del segnale in quel punto
        const auto* d = burst.getReadPointer (0);
        float worst = 0.f;
        const int w = 64;
        for (int i = w; i < burst.getNumSamples() - w; ++i)
        {
            const float jump = std::abs (d[i] - 2.f * d[i - 1] + d[i - 2]);
            float local = 0.f;
            for (int j = i - w; j < i + w; j += 4) if (j != i) local = juce::jmax (local, std::abs (d[j] - 2.f * d[j - 1] + d[j - 2]));
            if (local > 1e-4f) worst = juce::jmax (worst, jump / local);
        }
        check (worst < 2.5f, "raffica di note senza click: salto peggiore " + juce::String (worst, 2) + " volte la variazione vicina");
        e.requestPanic();
        play (e, {}, .1);
        e.selectPreset (before);
        play (e, {}, .01);
    }

    // accordo in WAV
    if (argc > 2)
    {
        auto chord = play (e, { { 0.0, on (60) }, { .02, on (64) }, { .04, on (67) }, { .06, on (72) }, { 1.5, off (60) }, { 1.5, off (64) }, { 1.5, off (67) }, { 1.5, off (72) } }, 3.0);
        juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
        out.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (out.createOutputStream().release());
        if (os != nullptr)
            if (auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (24)))
                w->writeFromAudioSampleBuffer (chord, 0, chord.getNumSamples());
        std::cout << "    scritto " << out.getFullPathName() << ", picco " << chord.getMagnitude (0, chord.getNumSamples()) << std::endl;
    }
    std::cout << (failures == 0 ? "TUTTE LE PROVE PASSATE" : juce::String (failures) + " PROVE FALLITE") << std::endl;
    return failures == 0 ? 0 : 1;
}
