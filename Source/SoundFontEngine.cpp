#include "SoundFontEngine.h"
#include "TsfAccess.h"
#include "SfzToSf2.h"
#include "YamahaToSf2.h"
#include "tsf.h"

namespace campione
{
int findPreset (const tsf* f, int msb, int lsb, int program, bool drumChannel, bool& drum)
{
    drum = msb == 127 || msb == 126 || msb == 120 || (drumChannel && msb == 0);
    if (drum)
    {
        // kit richiesto, poi il kit standard; se il file non ha batterie, il programma nei banchi normali
        for (int b : { 128 + (msb == 126 ? 1 : 0), 128 })
            if (const int i = tsf_get_presetindex (f, b, program); i >= 0) return i;
        if (const int i = tsf_get_presetindex (f, 128, 0); i >= 0) return i;
        drum = false;
    }
    int tries[3] = { 0, 0, 0 };
    int n = 0;
    if (msb == 0 || msb == 121) tries[n++] = lsb;                       // XG e GM2: variante nella LSB
    else if (msb == 64) tries[n++] = 64;                                // XG SFX voice
    else { if (lsb > 0) tries[n++] = msb * 128 + lsb; tries[n++] = msb; } // GS: variante nella MSB
    tries[n++] = 0;                                                     // ripiego: banco 0 (GM)
    for (int i = 0; i < n; ++i)
        if (const int idx = tsf_get_presetindex (f, tries[i], program); idx >= 0) return idx;
    return -1;
}

namespace
{
// 16 canali creati fuori dal thread audio: TinySoundFont li alloca alla prima chiamata
void createChannels (tsf* f, int preset)
{
    for (int ch = SoundFontEngine::midiChannels - 1; ch >= 0; --ch)
    {
        tsf_channel_set_presetindex (f, ch, preset);
        tsf_channel_set_pitchrange (f, ch, 2.f);
    }
}
}

SoundFontEngine::SoundFontEngine() = default;

SoundFontEngine::~SoundFontEngine() { unload(); }

bool SoundFontEngine::loadFile (const juce::File& f, juce::String& error)
{
    juce::MemoryBlock data;
    if (! f.existsAsFile()) { error = "FILE NOT FOUND"; return false; }
    juce::StringArray notes;
    if (f.hasFileExtension ("sfz"))
    {
        // SFZ: convertito in un SoundFont 2 in memoria (SfzToSf2.h)
        auto conv = sfz::convert (f);
        if (conv.error.isNotEmpty()) { error = conv.error; return false; }
        data = std::move (conv.sf2);
        notes = conv.notes;
    }
    else if (ysfc::isYamahaFile (f))
    {
        // Motif / Montage / MODX: waveform utente convertite in un SoundFont 2 in memoria (YamahaToSf2.h)
        auto conv = ysfc::convert (f);
        if (conv.error.isNotEmpty()) { error = conv.error; return false; }
        data = std::move (conv.sf2);
        notes = conv.notes;
    }
    else if (! f.loadFileAsData (data) || data.getSize() < 12) { error = "CANNOT READ FILE"; return false; }
    if (data.getSize() > (size_t) std::numeric_limits<int>::max()) { error = "FILE TOO LARGE"; return false; }

    // il file si prepara tutto qui, fuori dal lock: il thread audio continua a suonare il vecchio
    // (.sf3: i campioni Ogg Vorbis li decodifica TinySoundFont con stb_vorbis, vedi TsfAccess.cpp)
    tsf* fresh = tsf_load_memory (data.getData(), (int) data.getSize());
    if (fresh == nullptr) { error = f.hasFileExtension ("sfz") || ysfc::isYamahaFile (f) ? "CONVERSION FAILED" : "NOT A VALID SOUNDFONT"; return false; }
    if (tsf_get_presetcount (fresh) <= 0) { tsf_close (fresh); error = "NO PRESETS IN FILE"; return false; }

    tsf_set_output (fresh, TSF_STEREO_INTERLEAVED, (int) sampleRate, 0.f);
    if (! tsf_set_max_voices (fresh, maxVoices)) { tsf_close (fresh); error = "OUT OF MEMORY"; return false; }
    createChannels (fresh, 0);                      // crea i 16 canali (alloca qui, non nel thread audio)

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
        loadNotes = notes;
        appliedTuning = 1.0e9f;
        appliedPedal = false;
        envDirty = true;
        tsf_channel_set_presetindex (font, 0, first);
        appliedMulti = -1;                          // i canali si sistemano al primo blocco audio
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

int SoundFontEngine::voicesOnKey (int key) const
{
    const juce::ScopedLock sl (fontLock);
    return font != nullptr ? tsfaccess::voicesOnKey (font, key) : 0;
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
        createChannels (font, juce::jmax (0, presetIndex.load()));
    }
    appliedMulti = -1;
    appliedPedal = false;
    appliedTuning = 1.0e9f;
}

// ------------------------------------------------------------------------------------------------ thread audio
void SoundFontEngine::resetChannels (bool multi) noexcept
{
    // cambio di modo o file nuovo: tutto spento, controller azzerati; in GM/XG i canali partono dal
    // programma 0 del banco 0 e il canale 10 dal kit di batteria standard
    for (int ch = 0; ch < midiChannels; ++ch)
    {
        tsf_channel_sounds_off_all (font, ch);
        tsf_channel_midi_control (font, ch, 121, 0);
        tsf_channel_set_sustain (font, ch, 0);
        bankMsb[(size_t) ch] = bankLsb[(size_t) ch] = 0;
        drumCh[(size_t) ch] = false;
        if (ch == 0) { tsf_channel_set_presetindex (font, 0, juce::jmax (0, presetIndex.load())); continue; }
        bool drum = false;
        const int idx = findPreset (font, 0, 0, 0, multi && ch == drumChannel, drum);
        tsf_channel_set_presetindex (font, ch, idx >= 0 ? idx : juce::jmax (0, presetIndex.load()));
        drumCh[(size_t) ch] = multi && drum;
    }
    pedalValue = 0;
    appliedPedal = false;
    appliedTuning = 1.0e9f;
    appliedMulti = multi ? 1 : 0;
}

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
    // PIANO: tutto il MIDI suona il preset scelto (canale interno 0). GM/XG: ogni canale MIDI il suo.
    const int ch = s.multi ? juce::jlimit (0, midiChannels - 1, m.getChannel() - 1) : 0;
    const auto c = (size_t) ch;
    const int shift = s.multi && drumCh[c] ? 0 : s.transpose;   // la batteria non si traspone
    if (m.isNoteOn())
    {
        const int key = m.getNoteNumber() + shift;
        if (key < 0 || key > 127) return;
        tsfaccess::prepareNoteOn (font, ch, key, 24, .08f, .03f);
        tsf_channel_note_on (font, ch, key, shapeVelocity (m.getFloatVelocity(), s));
        lastKey.store (key);
    }
    else if (m.isNoteOff())
    {
        const int key = m.getNoteNumber() + shift;
        if (key >= 0 && key <= 127) tsf_channel_note_off (font, ch, key);
    }
    else if (m.isController())
    {
        const int cc = m.getControllerNumber(), v = m.getControllerValue();
        if (cc == 64) { if (ch == 0) pedalValue = v; else tsf_channel_set_sustain (font, ch, v >= 64 ? 1 : 0); }
        else if (cc == 67) ccSoft = v >= 64;
        else if (cc == 0) bankMsb[c] = v;
        else if (cc == 32) bankLsb[c] = v;
        else if (cc == 120) { tsf_channel_sounds_off_all (font, ch); if (ch == 0) pedalValue = 0; }
        else if (cc == 123) tsf_channel_note_off_all (font, ch);
        else if (cc == 121) { tsf_channel_midi_control (font, ch, cc, v); appliedTuning = 1.0e9f; }
        else if (cc == 7 || cc == 10 || cc == 11
                 || (s.multi && (cc == 6 || cc == 38 || cc == 98 || cc == 99 || cc == 100 || cc == 101)))   // RPN: estensione del pitch bend
            tsf_channel_midi_control (font, ch, cc, v);
    }
    else if (m.isProgramChange())
    {
        bool drum = false;
        const int idx = findPreset (font, bankMsb[c], bankLsb[c], m.getProgramChangeNumber(), s.multi && ch == drumChannel, drum);
        if (idx < 0) return;
        if (ch == 0) applyPresetIndex (idx);
        else tsf_channel_set_presetindex (font, ch, idx);
        drumCh[c] = s.multi && drum;
    }
    else if (m.isPitchWheel())
    {
        tsf_channel_set_pitchwheel (font, ch, m.getPitchWheelValue());
    }
}

void SoundFontEngine::applyPedal (const EngineSettings& s) noexcept
{
    // CC 64 continuo: sotto 24 pedale su, da 100 in su pedale giù del tutto; in mezzo mezzo pedale:
    // le note tenute dal pedale si smorzano con un rilascio che va da 0,25 s (poco pedale) a 10 s (quasi tutto)
    const bool full = s.pedalButton || pedalValue >= halfPedalHigh;
    const bool half = ! full && pedalValue >= halfPedalLow;
    const bool on = full || half;
    if (on != appliedPedal) { tsf_channel_set_sustain (font, 0, on ? 1 : 0); appliedPedal = on; }
    if (half)
    {
        const float x = (float) (pedalValue - halfPedalLow) / (float) (halfPedalHigh - halfPedalLow);
        tsfaccess::dampHeld (font, .25f * std::pow (40.f, x));
    }
    pedalAmount.store (full ? 1.f : half ? .5f : 0.f);
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
        for (int ch = 0; ch < midiChannels; ++ch)
        {
            tsf_channel_sounds_off_all (font, ch);
            tsf_channel_set_sustain (font, ch, 0);
            tsf_channel_set_pitchwheel (font, ch, 8192);
        }
        pedalValue = 0; ccSoft = false;
        appliedPedal = false;
    }
    if (appliedMulti != (s.multi ? 1 : 0)) resetChannels (s.multi);
    if (const int p = pendingPreset.exchange (-1); p >= 0) applyPresetIndex (p);
    if (! juce::exactlyEqual (s.tuningSemis, appliedTuning))
    {
        for (int ch = 0; ch < midiChannels; ++ch) tsf_channel_set_tuning (font, ch, s.tuningSemis);
        appliedTuning = s.tuningSemis;
    }
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
        applyPedal (s);
    }
    applyPedal (s);
    renderTo (pos, total);

    voices.store (tsf_active_voice_count (font));
    pedalState.store (appliedPedal);
    softState.store (s.softButton || ccSoft);
}
}
