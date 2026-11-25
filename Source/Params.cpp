#include "Params.h"

namespace campione
{
namespace
{
using Float = juce::AudioParameterFloat;
using Range = juce::NormalisableRange<float>;

juce::String secs (float s)
{
    if (s < 0.1f) return juce::String (juce::roundToInt (s * 1000.f)) + " MS";
    return juce::String (s, s < 10.f ? 2 : 1) + " S";
}

juce::String db (float d) { return d <= -59.9f ? "-INF DB" : juce::String (d, 1) + " DB"; }
juce::String pct (float v) { return juce::String (juce::roundToInt (v * 100.f)) + "%"; }

std::unique_ptr<Float> makeFloat (const char* pid, const char* name, Range r, float def, std::function<juce::String (float)> fmt)
{
    return std::make_unique<Float> (juce::ParameterID { pid, 1 }, name, r, def,
                                    juce::AudioParameterFloatAttributes().withStringFromValueFunction ([fmt] (float v, int) { return fmt (v); }));
}

Range timeRange (float maxSecs) { Range r (0.f, maxSecs); r.setSkewForCentre (maxSecs * .08f); return r; }
}

juce::String formatValue (const juce::String& p, float v)
{
    if (p == id::tone)
    {
        const int n = juce::roundToInt (std::abs (v) * 100.f);
        return n == 0 ? juce::String ("FLAT") : (v < 0 ? "DARK " : "BRIGHT ") + juce::String (n);
    }
    if (p == id::tune) { const int c = juce::roundToInt (v); return (c > 0 ? "+" : "") + juce::String (c) + " CT"; }
    if (p == id::transpose) { const int s = juce::roundToInt (v); return (s > 0 ? "+" : "") + juce::String (s) + " ST"; }
    if (p == id::attack || p == id::decay || p == id::release) return secs (v);
    if (p == id::volume) return db (v);
    return pct (v);
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto fmt = [] (const char* pid) { return [pid] (float v) { return formatValue (pid, v); }; };

    l.add (makeFloat (id::tone, "Tone", Range (-1.f, 1.f), 0.f, fmt (id::tone)));
    l.add (makeFloat (id::tune, "Fine Tune", Range (-50.f, 50.f, .1f), 0.f, fmt (id::tune)));
    l.add (makeFloat (id::transpose, "Transpose", Range (-12.f, 12.f, 1.f), 0.f, fmt (id::transpose)));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::a4ref, 1 }, "A4 Reference", juce::StringArray { "440 HZ", "432 HZ" }, 0));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::velCurve, 1 }, "Velocity Curve",
                                                         juce::StringArray { "NORMAL", "SOFT", "HARD", "FIXED" }, 0));

    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::sf2Env, 1 }, "Use SF2 Envelope", true));
    l.add (makeFloat (id::attack, "Attack", timeRange (5.f), 0.002f, fmt (id::attack)));
    l.add (makeFloat (id::decay, "Decay", timeRange (20.f), 6.f, fmt (id::decay)));
    l.add (makeFloat (id::sustain, "Sustain", Range (0.f, 1.f), 0.f, fmt (id::sustain)));
    l.add (makeFloat (id::release, "Release", timeRange (10.f), 0.6f, fmt (id::release)));

    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::pedal, 1 }, "Sustain Pedal", false));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::soft, 1 }, "Soft Pedal", false));
    l.add (makeFloat (id::softAmt, "Soft Amount", Range (0.f, 1.f), .4f, fmt (id::softAmt)));

    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::roomOn, 1 }, "Room", true));
    l.add (makeFloat (id::roomSize, "Room Size", Range (0.f, 1.f), .45f, fmt (id::roomSize)));
    l.add (makeFloat (id::roomDamp, "Room Damping", Range (0.f, 1.f), .5f, fmt (id::roomDamp)));
    l.add (makeFloat (id::roomMix, "Room Mix", Range (0.f, 1.f), .22f, fmt (id::roomMix)));

    Range vol (-60.f, 6.f, .1f); vol.setSkewForCentre (-12.f);
    l.add (makeFloat (id::volume, "Volume", vol, -3.f, fmt (id::volume)));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::bank, 1 }, "Bank", juce::StringArray { "A", "B", "C", "D" }, 0));
    return l;
}
}
