#include "SoundFontEngine.h"
#include "TsfAccess.h"
#include "tsf.h"

namespace campione
{
SoundFontEngine::SoundFontEngine() = default;

SoundFontEngine::~SoundFontEngine() { unload(); }

bool SoundFontEngine::loadFile (const juce::File& f, juce::String& error)
{
    juce::MemoryBlock data;
    if (! f.existsAsFile()) { error = "FILE NOT FOUND"; return false; }
    if (! f.loadFileAsData (data) || data.getSize() < 12) { error = "CANNOT READ FILE"; return false; }
    if (data.getSize() > (size_t) std::numeric_limits<int>::max()) { error = "FILE TOO LARGE"; return false; }

    // il file si prepara tutto qui, fuori dal lock: il thread audio continua a suonare il vecchio
    tsf* fresh = tsf_load_memory (data.getData(), (int) data.getSize());
    if (fresh == nullptr) { error = "NOT A VALID SF2 FILE"; return false; }
    if (tsf_get_presetcount (fresh) <= 0) { tsf_close (fresh); error = "NO PRESETS IN FILE"; return false; }

    tsf_set_output (fresh, TSF_STEREO_INTERLEAVED, (int) sampleRate, 0.f);
    if (! tsf_set_max_voices (fresh, maxVoices)) { tsf_close (fresh); error = "OUT OF MEMORY"; return false; }
    tsf_channel_set_presetindex (fresh, 0, 0);       // crea il canale 0 (alloca qui, non nel thread audio)
    tsf_channel_set_pitchrange (fresh, 0, 2.f);

    std::vector<PresetInfo> list;
    for (int i = 0; i < tsf_get_presetcount (fresh); ++i)
    {
        int bank = 0, number = 0;
        tsfaccess::presetNumbers (fresh, i, bank, number);
        list.push_back ({ juce::String::fromUTF8 (tsf_get_presetname (fresh, i)).trim(), bank, number });
    }

    std::vector<float> env ((size_t) tsfaccess::regionCount (fresh) * tsfaccess::envFloats);
    if (! env.empty()) tsfaccess::readEnvelopes (fresh, env.data());

    // primo preset: il pianoforte se c'è un banco 0 programma 0 (General MIDI Acoustic Grand), altrimenti il primo dell'elenco
    int first = tsf_get_presetindex (fresh, 0, 0);
    if (first < 0) first = 0;

    tsf* old = nullptr;
    {
        const juce::ScopedLock sl (fontLock);
        old = font;
        font = fresh;
        file = f;
        presetList = std::move (list);
        originalEnv = std::move (env);
        appliedTuning = 1.0e9f;
        appliedPedal = false;
        envDirty = true;
        tsf_channel_set_presetindex (font, 0, first);
        presetIndex.store (first);
        pendingPreset.store (-1);
        loaded.store (true);
    }
    if (old != nullptr) tsf_close (old);
    voices.store (0);
    return true;
}

void SoundFontEngine::unload()
{
    tsf* old = nullptr;
    {
        const juce::ScopedLock sl (fontLock);
        old = font;
        font = nullptr;
        file = juce::File();
        presetList.clear();
        originalEnv.clear();
        presetIndex.store (-1);
        loaded.store (false);
    }
    if (old != nullptr) tsf_close (old);
}

std::vector<SoundFontEngine::PresetInfo> SoundFontEngine::presets() const
{
    const juce::ScopedLock sl (fontLock);
    return presetList;
}

juce::File SoundFontEngine::currentFile() const
{
    const juce::ScopedLock sl (fontLock);
    return file;
}

void SoundFontEngine::prepare (double sr, int maxBlockSize)
{
    const juce::ScopedLock sl (fontLock);
    sampleRate = sr;
    scratch.assign ((size_t) juce::jmax (32, maxBlockSize) * 2, 0.f);
    if (font != nullptr)
    {
        tsf_set_output (font, TSF_STEREO_INTERLEAVED, (int) sr, 0.f);
        tsf_reset (font);
        tsf_channel_set_presetindex (font, 0, juce::jmax (0, presetIndex.load()));
    }
    appliedPedal = false;
    appliedTuning = 1.0e9f;
}

// ------------------------------------------------------------------------------------------------ thread audio
void SoundFontEngine::applyPresetIndex (int index) noexcept
{
    if (font == nullptr || index < 0 || index >= tsf_get_presetcount (font)) return;
    tsf_channel_set_presetindex (font, 0, index);
    presetIndex.store (index);
}

void SoundFontEngine::applyEnvelope (const EngineSettings& s) noexcept
{
    // con l'inviluppo del SoundFont contano solo i cambi di modo: le manopole non riscrivono le regioni
    const EnvKey k = s.sf2Envelope ? EnvKey { true, -1, -1, -1, -1 } : EnvKey { false, s.attack, s.decay, s.sustain, s.release };
    if (! envDirty && ! (k != appliedEnv)) return;
    if (k.sf2)
    {
        if (! originalEnv.empty()) tsfaccess::writeEnvelopes (font, originalEnv.data());
    }
    else
    {
        tsfaccess::overrideEnvelopes (font, s.attack, s.decay, s.sustain, s.release);
    }
    appliedEnv = k;
    envDirty = false;
}

float SoundFontEngine::shapeVelocity (float v, const EngineSettings& s) const noexcept
{
    switch (s.velCurve)
    {
        case 1: v = std::pow (v, .6f); break;     // SOFT: più forte con poca forza
        case 2: v = std::pow (v, 1.6f); break;    // HARD: serve più forza
        case 3: v = .8f; break;                   // FIXED
        default: break;
    }
    if (s.softButton || ccSoft) v *= 1.f - .5f * s.softAmount;   // una corda
    return juce::jlimit (1.f / 127.f, 1.f, v);
}

void SoundFontEngine::handleMidi (const juce::MidiMessage& m, const EngineSettings& s) noexcept
{
    if (m.isNoteOn())
    {
        const int key = m.getNoteNumber() + s.transpose;
        if (key < 0 || key > 127) return;
        tsf_channel_note_on (font, 0, key, shapeVelocity (m.getFloatVelocity(), s));
        lastKey.store (key);
    }
    else if (m.isNoteOff())
    {
        const int key = m.getNoteNumber() + s.transpose;
        if (key >= 0 && key <= 127) tsf_channel_note_off (font, 0, key);
    }
    else if (m.isController())
    {
        const int cc = m.getControllerNumber(), v = m.getControllerValue();
        if (cc == 64) ccPedal = v >= 64;
        else if (cc == 67) ccSoft = v >= 64;
        else if (cc == 0) bankMsb = v;
        else if (cc == 32) bankLsb = v;
        else if (cc == 120) { tsf_channel_sounds_off_all (font, 0); ccPedal = false; }
        else if (cc == 123) tsf_channel_note_off_all (font, 0);
        else if (cc == 7 || cc == 10 || cc == 11 || cc == 121) tsf_channel_midi_control (font, 0, cc, v);
    }
    else if (m.isProgramChange())
    {
        // banco dal CC 0 (MSB); se il preset non esiste in quel banco, prova il banco 0
        int idx = tsf_get_presetindex (font, bankMsb * 128 + bankLsb, m.getProgramChangeNumber());
        if (idx < 0) idx = tsf_get_presetindex (font, bankMsb, m.getProgramChangeNumber());
        if (idx < 0) idx = tsf_get_presetindex (font, 0, m.getProgramChangeNumber());
        if (idx >= 0) applyPresetIndex (idx);
    }
    else if (m.isPitchWheel())
    {
        tsf_channel_set_pitchwheel (font, 0, m.getPitchWheelValue());
    }
}

void SoundFontEngine::renderSpan (float* l, float* r, int n) noexcept
{
    if (n <= 0) return;
    float* buf = scratch.data();
    tsf_render_float (font, buf, n, 0);
    for (int i = 0; i < n; ++i) { l[i] = buf[2 * i]; r[i] = buf[2 * i + 1]; }
}

void SoundFontEngine::render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const EngineSettings& s) noexcept
{
    out.clear();
    const juce::ScopedTryLock tl (fontLock);
    if (! tl.isLocked() || font == nullptr) { voices.store (0); return; }

    if (panicRequested.exchange (false))
    {
        // PANIC: voci spente subito, pedali e pitch bend azzerati
        tsf_channel_sounds_off_all (font, 0);
        tsf_channel_set_sustain (font, 0, 0);
        tsf_channel_set_pitchwheel (font, 0, 8192);
        ccPedal = ccSoft = false;
        appliedPedal = false;
    }
    if (const int p = pendingPreset.exchange (-1); p >= 0) applyPresetIndex (p);
    if (! juce::exactlyEqual (s.tuningSemis, appliedTuning)) { tsf_channel_set_tuning (font, 0, s.tuningSemis); appliedTuning = s.tuningSemis; }
    applyEnvelope (s);

    float* l = out.getWritePointer (0);
    float* r = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;
    const int total = out.getNumSamples();
    const int maxChunk = (int) scratch.size() / 2;

    auto renderTo = [&] (int from, int to)
    {
        while (from < to)
        {
            const int n = juce::jmin (to - from, maxChunk);
            if (r != nullptr) renderSpan (l + from, r + from, n);
            else
            {
                float* buf = scratch.data();
                tsf_render_float (font, buf, n, 0);
                for (int i = 0; i < n; ++i) l[from + i] = .5f * (buf[2 * i] + buf[2 * i + 1]);
            }
            from += n;
        }
    };

    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, total, meta.samplePosition);
        renderTo (pos, at);
        pos = at;
        handleMidi (meta.getMessage(), s);
        const bool pedal = s.pedalButton || ccPedal;
        if (pedal != appliedPedal) { tsf_channel_set_sustain (font, 0, pedal ? 1 : 0); appliedPedal = pedal; }
    }
    const bool pedal = s.pedalButton || ccPedal;
    if (pedal != appliedPedal) { tsf_channel_set_sustain (font, 0, pedal ? 1 : 0); appliedPedal = pedal; }
    renderTo (pos, total);

    voices.store (tsf_active_voice_count (font));
    pedalState.store (appliedPedal);
    softState.store (s.softButton || ccSoft);
}
}
