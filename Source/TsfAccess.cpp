// Implementazione di TinySoundFont (third_party/TinySoundFont/tsf.h, licenza MIT) e accesso alle sue regioni.
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
