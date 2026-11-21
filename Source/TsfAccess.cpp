// Implementazione di TinySoundFont (third_party/TinySoundFont/tsf.h, licenza MIT) e accesso alle sue regioni.
// inviluppi e volume aggiornati ogni 16 campioni invece di 64: gradini più piccoli, niente "zip" sui rilasci brevi
#define TSF_RENDER_EFFECTSAMPLEBLOCK 16
#define TSF_IMPLEMENTATION
#include "tsf.h"
#include "TsfAccess.h"

namespace campione::tsfaccess
{
int regionCount (const tsf* f)
{
    int n = 0;
    for (int p = 0; p < f->presetNum; ++p) n += f->presets[p].regionNum;
    return n;
}

void presetNumbers (const tsf* f, int i, int& bank, int& number)
{
    bank = f->presets[i].bank;
    number = f->presets[i].preset;
}

void readEnvelopes (const tsf* f, float* out)
{
    for (int p = 0; p < f->presetNum; ++p)
        for (int r = 0; r < f->presets[p].regionNum; ++r)
        {
            const tsf_envelope& e = f->presets[p].regions[r].ampenv;
            const float v[envFloats] = { e.delay, e.attack, e.hold, e.decay, e.sustain, e.release, e.keynumToHold, e.keynumToDecay };
            for (int i = 0; i < envFloats; ++i) *out++ = v[i];
        }
}

void writeEnvelopes (tsf* f, const float* in)
{
    for (int p = 0; p < f->presetNum; ++p)
        for (int r = 0; r < f->presets[p].regionNum; ++r)
        {
            tsf_envelope& e = f->presets[p].regions[r].ampenv;
            e.delay = in[0]; e.attack = in[1]; e.hold = in[2]; e.decay = in[3];
            e.sustain = in[4]; e.release = in[5]; e.keynumToHold = in[6]; e.keynumToDecay = in[7];
            in += envFloats;
        }
}

int voicesOnKey (const tsf* f, int key)
{
    int n = 0;
    for (int i = 0; i < f->voiceNum; ++i)
        if (f->voices[i].playingPreset != -1 && f->voices[i].playingKey == key && f->voices[i].ampenv.segment < TSF_SEGMENT_DONE) ++n;
    return n;
}

namespace
{
void fadeVoice (tsf* f, tsf_voice* v, float seconds)
{
    // rilascio esponenziale breve a partire dal livello attuale
    v->heldSustain = 0;
    v->ampenv.parameters.release = seconds;
    tsf_voice_envelope_nextsegment (&v->ampenv, TSF_SEGMENT_SUSTAIN, f->outSampleRate);
    v->modenv.parameters.release = seconds;
    tsf_voice_envelope_nextsegment (&v->modenv, TSF_SEGMENT_SUSTAIN, f->outSampleRate);
}

bool fading (const tsf_voice& v, float seconds)
{
    return v.ampenv.segment == TSF_SEGMENT_RELEASE && v.ampenv.parameters.release <= seconds + 1e-6f;
}
}

void prepareNoteOn (tsf* f, int channel, int key, int minFree, float restrikeFade, float stealFade)
{
    int free = 0;
    for (int i = 0; i < f->voiceNum; ++i)
    {
        tsf_voice& v = f->voices[i];
        if (v.playingPreset == -1) { ++free; continue; }
        if (v.playingKey == key && v.playingChannel == channel && ! fading (v, restrikeFade)) fadeVoice (f, &v, restrikeFade);
    }
    // voci da liberare: le più vecchie (playIndex più basso) che non sono già in dissolvenza
    int fadingNow = 0;
    for (int i = 0; i < f->voiceNum; ++i) if (f->voices[i].playingPreset != -1 && fading (f->voices[i], stealFade)) ++fadingNow;
    while (free + fadingNow < minFree)
    {
        tsf_voice* oldest = nullptr;
        for (int i = 0; i < f->voiceNum; ++i)
        {
            tsf_voice& v = f->voices[i];
            if (v.playingPreset == -1 || fading (v, stealFade)) continue;
            if (oldest == nullptr || (int) (v.playIndex - oldest->playIndex) < 0) oldest = &v;
        }
        if (oldest == nullptr) break;
        fadeVoice (f, oldest, stealFade);
        ++fadingNow;
    }
}

void overrideEnvelopes (tsf* f, float attack, float decay, float sustain, float release)
{
    for (int p = 0; p < f->presetNum; ++p)
        for (int r = 0; r < f->presets[p].regionNum; ++r)
        {
            tsf_envelope& e = f->presets[p].regions[r].ampenv;
            e.delay = 0; e.hold = 0; e.keynumToHold = 0; e.keynumToDecay = 0;
            e.attack = attack; e.decay = decay; e.sustain = sustain; e.release = release;
        }
}
}
