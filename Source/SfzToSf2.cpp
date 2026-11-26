#include "SfzToSf2.h"
#include "Sf2Builder.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <map>
#include <cmath>

namespace campione::sfz
{
namespace
{
using Opcodes = std::map<juce::String, juce::String>;

// --------------------------------------------------------------------------------- lettura del testo
juce::String stripComments (const juce::String& text)
{
    juce::String out;
    out.preallocateBytes ((size_t) text.length() + 1);
    auto p = text.getCharPointer();
    while (! p.isEmpty())
    {
        if (*p == '/' && p[1] == '/') { while (! p.isEmpty() && *p != '\n') ++p; continue; }
        if (*p == '/' && p[1] == '*') { p += 2; while (! p.isEmpty() && ! (*p == '*' && p[1] == '/')) ++p; if (! p.isEmpty()) p += 2; continue; }
        out += *p;
        ++p;
    }
    return out;
}

// #include e #define, anche in mezzo a una riga ("<region> #define $KEY 021 ... #include \"x.txt\""), nell'ordine
// in cui compaiono: le variabili $NOME si sostituiscono con il valore che hanno in quel punto.
// I percorsi degli #include partono dalla cartella dello SFZ principale (come in sfizz e ARIA);
// se lì il file non c'è, dalla cartella del file che lo include.
juce::String substitute (juce::String text, const std::map<juce::String, juce::String>& defines)
{
    if (! text.containsChar ('$')) return text;
    // le variabili più lunghe prima, così $VEL non rovina $VELOCITY
    std::vector<juce::String> keys;
    for (auto& d : defines) keys.push_back (d.first);
    std::sort (keys.begin(), keys.end(), [] (auto& a, auto& b) { return a.length() > b.length(); });
    for (auto& k : keys) text = text.replace (k, defines.at (k));
    return text;
}

juce::String preprocess (const juce::File& file, const juce::File& rootDir, std::map<juce::String, juce::String>& defines, int depth, juce::StringArray& notes)
{
    if (depth > 8) { notes.add ("#include troppo annidati in " + file.getFileName()); return {}; }
    const auto lines = juce::StringArray::fromLines (stripComments (file.loadFileAsString()));
    juce::String out;
    for (auto line : lines)
    {
        for (;;)
        {
            const int d = line.indexOf ("#define"), i = line.indexOf ("#include");
            const int at = d < 0 ? i : (i < 0 ? d : juce::jmin (d, i));
            if (at < 0) { out << substitute (line, defines) << "\n"; break; }
            out << substitute (line.substring (0, at), defines) << "\n";
            auto rest = line.substring (at);
            if (at == d)
            {
                // #define $NOME valore (il valore è una sola parola)
                auto t = rest.substring (7).trimStart();
                const auto name = t.upToFirstOccurrenceOf (" ", false, false).upToFirstOccurrenceOf ("\t", false, false);
                t = t.substring (name.length()).trimStart();
                auto value = t.upToFirstOccurrenceOf (" ", false, false).upToFirstOccurrenceOf ("\t", false, false);
                line = t.substring (value.length());
                if (name.startsWith ("$")) defines[name] = substitute (value, defines);
            }
            else
            {
                // #include "percorso"
                const auto q1 = rest.indexOfChar ('"'), q2 = q1 >= 0 ? rest.indexOfChar (q1 + 1, '"') : -1;
                if (q1 < 0 || q2 < 0) { line = rest.substring (8); continue; }
                const auto name = substitute (rest.substring (q1 + 1, q2), defines).replaceCharacter ('\\', '/');
                line = rest.substring (q2 + 1);
                auto inc = rootDir.getChildFile (name);
                if (! inc.existsAsFile()) inc = file.getParentDirectory().getChildFile (name);
                if (inc.existsAsFile()) out << preprocess (inc, rootDir, defines, depth + 1, notes) << "\n";
                else notes.addIfNotAlreadyThere ("#include non trovato: " + name);
            }
        }
    }
    return out;
}

bool isOpcodeStart (juce::String::CharPointerType p)
{
    // inizio di "nome=": lettere, cifre e _ fino a '='
    if (! (juce::CharacterFunctions::isLetter (*p) || *p == '_')) return false;
    while (! p.isEmpty() && (juce::CharacterFunctions::isLetterOrDigit (*p) || *p == '_')) ++p;
    return *p == '=';
}

struct Section { juce::String header; Opcodes ops; };

std::vector<Section> parse (const juce::String& text)
{
    std::vector<Section> out;
    auto p = text.getCharPointer();
    auto skipSpace = [&] { while (! p.isEmpty() && juce::CharacterFunctions::isWhitespace (*p)) ++p; };
    for (;;)
    {
        skipSpace();
        if (p.isEmpty()) break;
        if (*p == '<')
        {
            ++p;
            juce::String h;
            while (! p.isEmpty() && *p != '>') { h += *p; ++p; }
            if (! p.isEmpty()) ++p;
            out.push_back ({ h.trim().toLowerCase(), {} });
            continue;
        }
        if (isOpcodeStart (p))
        {
            juce::String name, value;
            while (*p != '=') { name += *p; ++p; }
            ++p;
            // il valore va fino al prossimo opcode o header; per sample= può contenere spazi
            while (! p.isEmpty())
            {
                if (*p == '<') break;
                if (*p == '\n' || *p == '\r') break;
                if (juce::CharacterFunctions::isWhitespace (*p))
                {
                    auto q = p;
                    while (! q.isEmpty() && (*q == ' ' || *q == '\t')) ++q;
                    if (q.isEmpty() || *q == '<' || *q == '\n' || *q == '\r' || isOpcodeStart (q)) break;
                }
                value += *p;
                ++p;
            }
            if (out.empty()) out.push_back ({ "global", {} });
            out.back().ops[name.trim().toLowerCase()] = value.trim();
            continue;
        }
        ++p;   // carattere che non si capisce: si salta
    }
    return out;
}

// "c4" = 60, "c#4"/"db4", numeri; -1 se non valido
int noteNumber (const juce::String& sIn)
{
    const auto s = sIn.trim().toLowerCase();
    if (s.isEmpty()) return -1;
    if (s.containsOnly ("-0123456789")) return s.getIntValue();
    const int base[7] = { 9, 11, 0, 2, 4, 5, 7 };   // a b c d e f g
    const auto c = s[0];
    if (c < 'a' || c > 'g') return -1;
    int n = base[c - 'a'];
    int i = 1;
    if (s[i] == '#') { ++n; ++i; } else if (s[i] == 'b' && s.length() > 2 && (juce::CharacterFunctions::isDigit (s[2]) || s[2] == '-')) { --n; ++i; }
    const auto oct = s.substring (i);
    if (! oct.containsOnly ("-0123456789") || oct.isEmpty()) return -1;
    return n + (oct.getIntValue() + 1) * 12;
}

// --------------------------------------------------------------------------------- campioni
struct Sample
{
    std::vector<int16_t> data[2];
    int channels = 1, rate = 44100;
    int loopStart = -1, loopEnd = -1;   // dal file (WAV smpl), fine compresa
};

using sf2::Gen;
using sf2::timecents;
}

Result convert (const juce::File& sfzFile)
{
    Result res;
    std::map<juce::String, juce::String> defines;
    auto sections = parse (preprocess (sfzFile, sfzFile.getParentDirectory(), defines, 0, res.notes));

    // ereditarietà: control -> global -> master -> group -> region
    Opcodes control, global, master, group;
    struct Region { Opcodes ops; juce::File base; };
    std::vector<Region> regions;
    for (auto& s : sections)
    {
        if (s.header == "control") { control = s.ops; }
        else if (s.header == "global") { global = s.ops; master.clear(); group.clear(); }
        else if (s.header == "master") { master = s.ops; group.clear(); }
        else if (s.header == "group") { group = s.ops; }
        else if (s.header == "region")
        {
            Opcodes o = global;
            for (auto& kv : master) o[kv.first] = kv.second;
            for (auto& kv : group) o[kv.first] = kv.second;
            for (auto& kv : s.ops) o[kv.first] = kv.second;
            regions.push_back ({ o, {} });
        }
        // <curve>, <effect>, <midi> e simili: ignorati
    }
    if (regions.empty()) { res.error = "NO REGIONS IN SFZ"; return res; }

    const auto dir = sfzFile.getParentDirectory();
    const auto defaultPath = control.count ("default_path") ? control["default_path"].replaceCharacter ('\\', '/') : juce::String();
    const int noteOffset = control.count ("note_offset") ? control["note_offset"].getIntValue() : 0;
    const int octaveOffset = control.count ("octave_offset") ? control["octave_offset"].getIntValue() : 0;
    const int keyShift = noteOffset + 12 * octaveOffset;
    const int swDefault = control.count ("sw_default") ? noteNumber (control["sw_default"]) : -1;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::map<juce::String, int> sampleIndex;   // percorso -> indice in 'samples'
    std::vector<Sample> samples;
    double totalFrames = 0;

    auto getSample = [&] (const juce::String& rel) -> int
    {
        const auto path = rel.replaceCharacter ('\\', '/');
        const auto f = dir.getChildFile (defaultPath + path);
        const auto key = f.getFullPathName();
        if (auto it = sampleIndex.find (key); it != sampleIndex.end()) return it->second;
        std::unique_ptr<juce::AudioFormatReader> rd (formats.createReaderFor (f));
        if (rd == nullptr) { res.notes.addIfNotAlreadyThere ("campione non trovato o non leggibile: " + path); sampleIndex[key] = -1; return -1; }
        const auto len = (int) juce::jmin<juce::int64> (rd->lengthInSamples, 60 * 192000);
        totalFrames += (double) len * juce::jmin (2, (int) rd->numChannels);
        if (totalFrames > sf2::maxSampleValues) { res.notes.addIfNotAlreadyThere ("libreria troppo grande: campioni oltre il limite di memoria saltati"); sampleIndex[key] = -1; return -1; }
        Sample s;
        s.channels = juce::jmin (2, (int) rd->numChannels);
        s.rate = (int) std::lround (rd->sampleRate);
        juce::AudioBuffer<float> buf (s.channels, len);
        rd->read (&buf, 0, len, 0, true, s.channels > 1);
        for (int c = 0; c < s.channels; ++c)
        {
            s.data[c].resize ((size_t) len);
            const float* src = buf.getReadPointer (c);
            for (int i = 0; i < len; ++i) s.data[c][(size_t) i] = (int16_t) juce::jlimit (-32767, 32767, (int) std::lround (src[i] * 32767.f));
        }
        const auto& md = rd->metadataValues;
        if (md.getValue ("NumSampleLoops", "0").getIntValue() > 0)
        {
            s.loopStart = md.getValue ("Loop0Start", "-1").getIntValue();
            s.loopEnd = md.getValue ("Loop0End", "-1").getIntValue();
        }
        samples.push_back (std::move (s));
        sampleIndex[key] = (int) samples.size() - 1;
        return (int) samples.size() - 1;
    };

    // zone dello strumento
    struct Zone { std::vector<Gen> gens; int sample; int channel; };   // sample: indice in samples, channel 0/1
    std::vector<Zone> zones;
    for (auto& r : regions)
    {
        auto& o = r.ops;
        auto get = [&o] (const char* k, const juce::String& def = {}) { auto it = o.find (k); return it != o.end() ? it->second : def; };
        auto num = [&] (const char* k, double def) { auto v = get (k); return v.isEmpty() ? def : v.getDoubleValue(); };
        auto key = [&] (const char* k, int def) { auto v = get (k); const int n = v.isEmpty() ? def : noteNumber (v); return n < 0 ? def : n; };

        if (get ("trigger", "attack") != "attack" && get ("trigger") != "first" && get ("trigger") != "legato") { ++res.skipped; continue; }
        if ((int) num ("seq_position", 1) > 1 || num ("lorand", 0) > 0) { ++res.skipped; continue; }
        // regioni suonate da un controller (rumore del pedale, coperchio...) o senza tasto (key=-1): non sono note
        bool ccTriggered = false;
        for (auto& kv : o) if (kv.first.startsWith ("on_locc") || kv.first.startsWith ("on_hicc") || kv.first.startsWith ("start_locc") || kv.first.startsWith ("start_hicc")) ccTriggered = true;
        if (ccTriggered || get ("key").trim() == "-1" || get ("hikey").trim() == "-1") { ++res.skipped; continue; }
        if (get ("sw_last").isNotEmpty() && swDefault >= 0 && noteNumber (get ("sw_last")) != swDefault) { ++res.skipped; continue; }
        const auto smp = get ("sample");
        if (smp.isEmpty() || smp.startsWith ("*")) { ++res.skipped; continue; }   // generatori interni (*sine, *noise) non supportati
        const int si = getSample (smp);
        if (si < 0) { ++res.skipped; continue; }

        int lo = key ("lokey", 0), hi = key ("hikey", 127);
        if (get ("key").isNotEmpty()) lo = hi = key ("key", 60);
        int center = key ("pitch_keycenter", get ("key").isNotEmpty() ? lo : 60);
        lo = juce::jlimit (0, 127, lo + keyShift); hi = juce::jlimit (0, 127, hi + keyShift); center = juce::jlimit (0, 127, center + keyShift);
        if (lo > hi) { ++res.skipped; continue; }
        const int lovel = juce::jlimit (1, 127, (int) num ("lovel", 1)), hivel = juce::jlimit (1, 127, (int) num ("hivel", 127));

        const auto& s = samples[(size_t) si];
        const int len = (int) s.data[0].size();
        const int start = juce::jlimit (0, juce::jmax (0, len - 1), (int) num ("offset", 0));
        const int end = juce::jlimit (start + 1, len, get ("end").isNotEmpty() ? (int) num ("end", len) + 1 : len);

        auto mode = get ("loop_mode", get ("loopmode"));
        int ls = (int) num ("loop_start", num ("loopstart", s.loopStart));
        int le = (int) num ("loop_end", num ("loopend", s.loopEnd));
        if (mode.isEmpty()) mode = (ls >= 0 && le > ls) ? "loop_continuous" : "no_loop";
        const bool loops = (mode == "loop_continuous" || mode == "loop_sustain") && ls >= 0 && le > ls && le < len;
        const bool oneShot = mode == "one_shot";

        const double vol = num ("volume", 0) + 20.0 * std::log10 (juce::jlimit (0.0001, 1.0, num ("amplitude", 100) / 100.0));
        const double tune = num ("transpose", 0) * 100.0 + num ("tune", 0);
        const int coarse = (int) std::trunc (tune / 100.0), fine = (int) std::lround (tune - coarse * 100.0);
        const double sustainPct = juce::jlimit (0.0, 100.0, num ("ampeg_sustain", 100));
        double release = num ("ampeg_release", 0.001);
        if (oneShot) release = juce::jmax (release, (double) len / s.rate);   // one_shot: suona fino in fondo anche al rilascio
        const double pan = juce::jlimit (-100.0, 100.0, num ("pan", 0));
        const int exclusive = (get ("group").isNotEmpty() && get ("off_by") == get ("group")) ? juce::jlimit (1, 127, get ("group").getIntValue()) : 0;
        const bool fixedPitch = std::abs (num ("pitch_keytrack", 100)) < 0.5;

        for (int ch = 0; ch < s.channels; ++ch)
        {
            Zone z { {}, si, ch };
            z.gens.push_back ({ 43, lo | (hi << 8) });
            z.gens.push_back ({ 44, lovel | (hivel << 8) });
            sf2::addOffset (z.gens, sf2::op::startOffset, sf2::op::startCoarse, start);
            sf2::addOffset (z.gens, sf2::op::endOffset, sf2::op::endCoarse, end - len);
            if (loops)
            {
                // offset del loop rispetto al loop del campione (che è 0..len, vedi Sf2Builder.h)
                sf2::addOffset (z.gens, sf2::op::loopStartOffset, sf2::op::loopStartCoarse, ls);
                sf2::addOffset (z.gens, sf2::op::loopEndOffset, sf2::op::loopEndCoarse, le + 1 - len);   // SF2: fine del loop esclusa
            }
            const double chPan = s.channels == 2 ? (ch == 0 ? -100.0 : 100.0) : pan;
            const double finalPan = s.channels == 2 ? juce::jlimit (-100.0, 100.0, chPan + pan) : pan;
            if (std::abs (finalPan) > .01) z.gens.push_back ({ 17, (int) std::lround (finalPan * 5.0) });   // 0,1% unità: -500..500
            z.gens.push_back ({ 33, timecents (num ("ampeg_delay", 0)) });
            z.gens.push_back ({ 34, timecents (num ("ampeg_attack", 0.001)) });
            z.gens.push_back ({ 35, timecents (num ("ampeg_hold", 0)) });
            z.gens.push_back ({ 36, timecents (num ("ampeg_decay", 0)) });
            z.gens.push_back ({ 37, sustainPct <= 0.001 ? 1440 : juce::jlimit (0, 1440, (int) std::lround (-200.0 * std::log10 (sustainPct / 100.0))) });
            z.gens.push_back ({ 38, timecents (release) });
            if (vol < 0) z.gens.push_back ({ 48, juce::jlimit (0, 1440, (int) std::lround (-vol * 10.0)) });
            if (coarse != 0) z.gens.push_back ({ 51, juce::jlimit (-120, 120, coarse) });
            if (fine != 0) z.gens.push_back ({ 52, juce::jlimit (-99, 99, fine) });
            z.gens.push_back ({ 54, loops ? (mode == "loop_sustain" ? 3 : 1) : 0 });
            if (fixedPitch) z.gens.push_back ({ 56, 0 });
            if (exclusive > 0) z.gens.push_back ({ 57, exclusive });
            z.gens.push_back ({ 58, center });
            zones.push_back (std::move (z));
        }
        ++res.regions;
    }
    if (zones.empty()) { res.error = res.notes.isEmpty() ? "NO PLAYABLE REGIONS IN SFZ" : "NO PLAYABLE REGIONS: " + res.notes[0].toUpperCase(); return res; }
    if (zones.size() > 60000) { res.error = "SFZ TOO LARGE"; return res; }

    // ---- SoundFont: un preset, uno strumento, una zona per regione e canale
    sf2::Builder out;
    {
        size_t bytes = 0;
        for (auto& s : samples) for (int ch = 0; ch < s.channels; ++ch) bytes += 2 * (s.data[ch].size() + 46);
        out.reserve (bytes);
    }
    std::map<std::pair<int, int>, int> sampleID;
    res.samples = (int) samples.size();
    for (int si = 0; si < (int) samples.size(); ++si)
    {
        auto& s = samples[(size_t) si];
        if (s.data[0].empty()) continue;
        for (int ch = 0; ch < s.channels; ++ch)
            sampleID[{ si, ch }] = out.addSample ("S" + juce::String (si) + (s.channels == 2 ? (ch == 0 ? "L" : "R") : ""),
                                                  s.data[ch].data(), (int) s.data[ch].size(), s.rate);
        if (s.channels == 2) out.linkStereo (sampleID[{ si, 0 }], sampleID[{ si, 1 }]);
        for (auto& d : s.data) std::vector<int16_t>().swap (d);   // copiato nel SoundFont: si libera subito
    }
    if (out.sampleBytes() > 1.9e9) { res.error = "SFZ TOO LARGE"; return res; }

    const auto presetName = sfzFile.getFileNameWithoutExtension();
    out.addInstrument (presetName);
    for (auto& z : zones) out.addZone (z.gens, sampleID[{ z.sample, z.channel }]);
    out.addPreset (presetName, 0, 0, 0);
    res.sf2 = out.build (presetName);
    if (res.skipped > 0) res.notes.add (juce::String (res.skipped) + " regioni saltate (release, round robin, keyswitch o campioni mancanti)");
    return res;
}
}
