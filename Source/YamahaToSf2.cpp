#include "YamahaToSf2.h"
#include "Sf2Builder.h"
#include <cmath>
#include <cstring>
#include <map>
#include <optional>

namespace campione::ysfc
{
namespace
{
const char* const extensions[] = { "x0a", "x0w", "x3a", "x3w", "x6a", "x6w", "x7u", "x7l", "x7a", "x8u", "x8l", "x8a", "y2u", "y2l" };

// Motif XS/XF: keybank di 68 byte little endian. MOXF: 76 byte big endian.
// Montage, MODX, Montage M, MODX M: 64 byte little endian, stereo = due keybank.
enum class Family { motif, moxf, modern };

// ------------------------------------------------------------------------------------------------ lettura sicura
struct Bytes
{
    const juce::uint8* p = nullptr;
    size_t n = 0;
    bool has (size_t off, size_t len) const noexcept { return off <= n && len <= n - off; }
    juce::uint32 be32 (size_t o) const noexcept { return ((juce::uint32) p[o] << 24) | ((juce::uint32) p[o + 1] << 16) | ((juce::uint32) p[o + 2] << 8) | p[o + 3]; }
    juce::uint32 le32 (size_t o) const noexcept { return ((juce::uint32) p[o + 3] << 24) | ((juce::uint32) p[o + 2] << 16) | ((juce::uint32) p[o + 1] << 8) | p[o]; }
    int be16 (size_t o) const noexcept { return (p[o] << 8) | p[o + 1]; }
    int le16 (size_t o) const noexcept { return (p[o + 1] << 8) | p[o]; }
    bool tag (size_t o, const char* t) const noexcept { return has (o, 4) && std::memcmp (p + o, t, 4) == 0; }
};

struct Item { size_t off, size; };   // contenuto di un "Entr" o di un "Data"

// blocco "Exxx" o "Dxxx": id, lunghezza, numero di elementi, poi gli elementi (magic + lunghezza + contenuto)
std::optional<std::vector<Item>> readBlock (const Bytes& f, size_t off, const char* id)
{
    if (! f.has (off, 12) || ! f.tag (off, id)) return std::nullopt;
    const auto count = f.be32 (off + 8);
    std::vector<Item> items;
    size_t pos = off + 12;
    for (juce::uint32 i = 0; i < count; ++i)
    {
        if (! f.has (pos, 8) || ! (f.tag (pos, "Entr") || f.tag (pos, "Data"))) return std::nullopt;
        const size_t size = f.be32 (pos + 4);
        if (! f.has (pos + 8, size)) return std::nullopt;
        items.push_back ({ pos + 8, size });
        pos += 8 + size;
    }
    return items;
}

// posizione dei blocchi: dal catalogo dopo l'intestazione; se manca o non torna, scorrendo il file
std::map<std::string, size_t> findBlocks (const Bytes& f)
{
    std::map<std::string, size_t> out;
    const size_t catalogue = f.be32 (32);
    if (f.has (64, catalogue))
        for (size_t o = 64; o + 8 <= 64 + catalogue; o += 8)
        {
            const std::string id ((const char*) f.p + o, 4);
            const size_t at = f.be32 (o + 4);
            if (f.tag (at, id.c_str())) out[id] = at;
        }
    if (out.count ("EWFM") && out.count ("DWFM") && out.count ("EWIM") && out.count ("DWIM")) return out;

    juce::uint32 library = f.be32 (48);
    if (library == 0xffffffffu) library = 0;
    size_t pos = 64 + catalogue + library;
    while (f.has (pos, 8))
    {
        const std::string id ((const char*) f.p + pos, 4);
        bool ok = id[0] == 'E' || id[0] == 'D';
        for (char c : id) ok = ok && c >= 'A' && c <= 'Z';
        if (! ok) break;
        out.emplace (id, pos);
        pos += 8 + (size_t) f.be32 (pos + 4);
    }
    return out;
}

// nome dell'elemento: stringa terminata da zero dopo i campi fissi, che cambiano da versione a versione.
// Si prova la posizione attesa e poi le altre note; si preferisce il nome con la categoria ("66:Analog Bass1").
juce::String entryName (const Bytes& f, const Item& e, int version)
{
    int expected = 12;
    if (version <= 102) expected = 20 + (version <= 101 ? 1 : 2);
    else if (version >= 400)
    {
        expected = 18;
        if ((version > 402 && version < 410) || version >= 500) expected += 4;
        if (version >= 410 && version < 500) expected += 9;
    }
    auto readAt = [&] (int o) -> juce::String
    {
        if (o < 0 || (size_t) o >= e.size) return {};
        std::string s;
        for (size_t i = e.off + (size_t) o; i < e.off + e.size && f.p[i] != 0; ++i)
        {
            if (f.p[i] < 32 || f.p[i] > 126) return {};
            s.push_back ((char) f.p[i]);
        }
        return juce::String (s).trim();
    };
    const int tries[] = { expected, 12, 18, 21, 22, 27, 31 };
    auto hasCategory = [] (const juce::String& s) { const auto c = s.upToFirstOccurrenceOf (":", false, false); return s.contains (":") && c.isNotEmpty() && c.containsOnly ("0123456789"); };
    for (int o : tries) if (auto s = readAt (o); hasCategory (s)) return s.fromFirstOccurrenceOf (":", false, false).trim();
    for (int o : tries) if (auto s = readAt (o); s.isNotEmpty()) return s;
    return {};
}

struct Keybank
{
    int keyLo = 0, keyHi = 127, velLo = 1, velHi = 127, level = 255, pan = 64, fixedPitch = 0xff, root = 60;
    int coarse = 64, fine = 64, channels = 1, waveFormat = 0, loopMode = 1, encrypted = 0;
    int rate = 44100;
    juce::int64 playStart = 0, loopStart = 0, loopEnd = 0, length = 0;
};

int keybankSize (Family fam) { return fam == Family::motif ? 68 : fam == Family::moxf ? 76 : 64; }

Keybank readKeybank (const Bytes& f, size_t o, Family fam)
{
    Keybank k;
    const auto* b = f.p + o;
    k.keyLo = b[0]; k.keyHi = b[1]; k.velLo = b[2]; k.velHi = b[3];
    k.level = b[4];
    if (fam != Family::modern) k.level = juce::jmin (255, 2 * k.level);   // 0-128 nei modelli vecchi
    k.pan = b[5] & 0x7f;
    k.fixedPitch = b[7]; k.root = b[8]; k.coarse = b[9]; k.fine = b[10]; k.channels = b[11];
    k.waveFormat = b[14]; k.loopMode = b[15]; k.encrypted = b[16];
    const int loopRest = b[18];
    auto u32 = [&] (size_t at) -> juce::int64 { return fam == Family::moxf ? f.be32 (o + at) : f.le32 (o + at); };
    if (fam == Family::motif)
    {
        k.rate = (int) u32 (32); k.playStart = u32 (36); k.loopStart = u32 (40); k.loopEnd = u32 (44); k.length = u32 (48);
    }
    else if (fam == Family::moxf)
    {
        k.rate = (int) u32 (36); k.playStart = u32 (40); k.loopStart = u32 (44); k.loopEnd = u32 (48); k.length = u32 (52);
    }
    else
    {
        k.rate = (int) u32 (36); k.playStart = u32 (40); k.loopStart = 16 * u32 (44) + loopRest; k.loopEnd = u32 (48); k.length = u32 (60);
    }
    return k;
}

juce::String modelName (int version, const juce::File& file)
{
    if (version == 101) return "MOTIF XS";
    if (version == 102) return "MOTIF XF";
    if (version == 103) return "MOXF";
    if (version >= 400 && version < 410) return "MONTAGE";
    if (version >= 410 && version < 420) return "MONTAGE M";
    if (version >= 500 && version < 510) return "MODX";
    if (version >= 510 && version < 520) return "MODX M";
    return "YAMAHA " + file.getFileExtension().substring (1).toUpperCase();
}

Family familyOf (int version, const juce::File& file)
{
    if (version >= 104) return Family::modern;
    if (version == 103) return Family::moxf;
    if (version >= 100) return Family::motif;
    const auto ext = file.getFileExtension().toLowerCase();
    if (ext.startsWith (".x6")) return Family::moxf;
    if (ext.startsWith (".x0") || ext.startsWith (".x3")) return Family::motif;
    return Family::modern;
}
}

// ================================================================================================
bool isYamahaFile (const juce::File& f)
{
    for (auto* e : extensions) if (f.hasFileExtension (e)) return true;
    return false;
}

juce::String wildcard()
{
    juce::StringArray w;
    for (auto* e : extensions) { w.add ("*." + juce::String (e)); w.add ("*." + juce::String (e).toUpperCase()); }
    return w.joinIntoString (";");
}

Result convert (const juce::File& file)
{
    Result res;
    juce::MemoryMappedFile mapped (file, juce::MemoryMappedFile::readOnly);
    juce::MemoryBlock loaded;
    Bytes f;
    if (mapped.getData() != nullptr) f = { (const juce::uint8*) mapped.getData(), mapped.getSize() };
    else if (file.loadFileAsData (loaded)) f = { (const juce::uint8*) loaded.getData(), loaded.getSize() };
    else { res.error = "CANNOT READ FILE"; return res; }

    if (! f.has (0, 64) || std::memcmp (f.p, "YAMAHA-YSFC", 11) != 0) { res.error = "NOT A YAMAHA MOTIF / MONTAGE FILE"; return res; }

    // versione "A.B.C": 4.0.5 Montage, 5.0.1 MODX, 1.0.2 Motif XF...
    int version = 0;
    {
        const char* v = (const char*) f.p + 16;
        if (juce::CharacterFunctions::isDigit (v[0]) && v[1] == '.' && juce::CharacterFunctions::isDigit (v[2]) && v[3] == '.' && juce::CharacterFunctions::isDigit (v[4]))
            version = (v[0] - '0') * 100 + (v[2] - '0') * 10 + (v[4] - '0');
    }
    const auto fam = familyOf (version, file);
    res.model = modelName (version, file);

    const auto blocks = findBlocks (f);
    auto block = [&] (const char* id) -> std::vector<Item>
    {
        auto it = blocks.find (id);
        if (it == blocks.end()) return {};
        auto items = readBlock (f, it->second, id);
        return items ? *items : std::vector<Item> {};
    };
    const auto ewfm = block ("EWFM"), dwfm = block ("DWFM"), dwim = block ("DWIM");
    const auto epfm = block ("EPFM");
    if (dwfm.empty() || dwim.empty())
    {
        res.error = epfm.empty() ? "NO USER WAVEFORMS IN FILE" : "NO USER WAVEFORMS: PERFORMANCES USE FACTORY SOUNDS";
        return res;
    }
    if (dwfm.size() != dwim.size()) { res.error = "DAMAGED FILE: WAVEFORM COUNT MISMATCH"; return res; }

    sf2::Builder out;
    const int kbSize = keybankSize (fam);
    double totalFrames = 0;
    int encrypted = 0, compressed = 0, backwards = 0, damaged = 0;
    bool full = false;

    for (size_t w = 0; w < dwfm.size() && ! full; ++w)
    {
        auto name = w < ewfm.size() ? entryName (f, ewfm[w], version) : juce::String();
        if (name.isEmpty()) name = "Waveform " + juce::String (w + 1);

        // keybank (DWFM): numero, 2 byte, poi i keybank
        const auto& meta = dwfm[w];
        if (meta.size < 4 || (meta.size - 4) % (size_t) kbSize != 0) { ++damaged; continue; }
        std::vector<Keybank> kbs;
        for (size_t o = meta.off + 4; o + (size_t) kbSize <= meta.off + meta.size; o += (size_t) kbSize)
            kbs.push_back (readKeybank (f, o, fam));

        // campioni (DWIM): numero (32 bit big endian), poi per ogni canale lunghezza in byte e PCM 16 bit little endian
        std::vector<Item> waves;
        {
            const auto& d = dwim[w];
            if (d.size < 4) { ++damaged; continue; }
            const auto count = f.be32 (d.off);
            size_t pos = d.off + 4;
            bool ok = true;
            for (juce::uint32 i = 0; i < count && ok; ++i)
            {
                ok = pos + 4 <= d.off + d.size;
                if (! ok) break;
                const size_t len = f.be32 (pos);
                ok = pos + 4 + len <= d.off + d.size;
                if (ok) waves.push_back ({ pos + 4, len });
                pos += 4 + len;
            }
            if (! ok) { ++damaged; continue; }
        }

        bool instrumentMade = false;
        auto makeInstrument = [&]
        {
            if (instrumentMade) return;
            const int i = out.addInstrument (name);
            out.addPreset (name, i / 128, i % 128, i);
            instrumentMade = true;
        };

        size_t k = 0, wv = 0;
        int zoneNo = 0;
        while (k < kbs.size() && wv < waves.size())
        {
            const auto& kb = kbs[k];
            const bool stereo = kb.channels == 2 && wv + 1 < waves.size();
            const size_t kStep = stereo && fam == Family::modern ? 2 : 1;   // Montage: un keybank per canale
            const size_t wStep = stereo ? 2 : 1;
            const Item left = waves[wv], right = stereo ? waves[wv + 1] : Item { 0, 0 };
            k += kStep; wv += wStep;
            ++zoneNo;

            if (kb.encrypted > 0 && fam == Family::modern) { ++encrypted; continue; }
            if (kb.waveFormat != 0 && kb.waveFormat != 5) { ++compressed; continue; }

            // lunghezza in campioni: quella del keybank, mai oltre i dati presenti
            juce::int64 frames = (juce::int64) left.size / 2;
            if (stereo) frames = juce::jmin (frames, (juce::int64) right.size / 2);
            if (kb.length > 0) frames = juce::jmin (frames, kb.length);
            if (frames < 2 || kb.rate < 1000 || kb.rate > 384000) { ++damaged; continue; }
            const bool sameLength = ! stereo || left.size == right.size;
            totalFrames += (double) frames * (stereo ? 2 : 1);
            if (totalFrames > sf2::maxSampleValues) { res.notes.add ("libreria troppo grande: waveform oltre il limite di memoria saltate"); full = true; break; }

            auto addChannel = [&] (const Item& it, const juce::String& suffix)
            {
                std::vector<int16_t> pcm ((size_t) frames);
                for (juce::int64 i = 0; i < frames; ++i) pcm[(size_t) i] = (int16_t) f.le16 (it.off + 2 * (size_t) i);
                return out.addSample ("W" + juce::String (w + 1) + "K" + juce::String (zoneNo) + suffix, pcm.data(), (int) frames, kb.rate);
            };
            const int sL = addChannel (left, stereo ? "L" : "");
            const int sR = stereo ? addChannel (right, "R") : -1;
            if (stereo && sameLength) out.linkStereo (sL, sR);

            // zona: tasti, velocity, inizio, loop, livello, intonazione, pan
            const int lo = juce::jlimit (0, 127, kb.keyLo), hi = juce::jlimit (lo, 127, kb.keyHi);
            const int vlo = juce::jlimit (0, 127, kb.velLo), vhi = juce::jlimit (vlo, 127, kb.velHi);
            const int len = (int) frames;
            const int start = (int) juce::jlimit<juce::int64> (0, len - 2, kb.playStart);
            const juce::int64 loopEndExcl = juce::jmin<juce::int64> (kb.loopEnd + 1, len);   // fine del loop compresa nel file Yamaha
            const bool loops = kb.loopMode != 1 && kb.loopStart >= start && loopEndExcl - kb.loopStart >= 2;
            if (loops && kb.loopMode == 2) ++backwards;

            const double gainDb = kb.level == 0 ? -144.0 : -95.25 + (kb.level - 1) * 0.375;
            const double cents = (kb.coarse - 64) * 100.0 + (kb.fine - 64) * 1.5625;
            const int coarse = (int) std::trunc (cents / 100.0), fine = (int) std::lround (cents - coarse * 100.0);
            const double pan = juce::jlimit (-1.0, 1.0, (kb.pan - 64) / 63.0);

            std::vector<sf2::Gen> base;
            base.push_back ({ sf2::op::keyRange, lo | (hi << 8) });
            base.push_back ({ sf2::op::velRange, vlo | (vhi << 8) });
            sf2::addOffset (base, sf2::op::startOffset, sf2::op::startCoarse, start);
            if (loops)
            {
                sf2::addOffset (base, sf2::op::loopStartOffset, sf2::op::loopStartCoarse, (int) kb.loopStart);
                sf2::addOffset (base, sf2::op::loopEndOffset, sf2::op::loopEndCoarse, (int) (loopEndExcl - len));
            }
            base.push_back ({ sf2::op::releaseVol, sf2::timecents (0.2) });   // la waveform non ha inviluppo: rilascio breve senza click
            if (gainDb < 0) base.push_back ({ sf2::op::attenuation, juce::jlimit (0, 1440, (int) std::lround (-gainDb * 10.0)) });
            if (coarse != 0) base.push_back ({ sf2::op::coarseTune, juce::jlimit (-120, 120, coarse) });
            if (fine != 0) base.push_back ({ sf2::op::fineTune, juce::jlimit (-99, 99, fine) });
            base.push_back ({ sf2::op::sampleModes, loops ? 1 : 0 });
            if (kb.fixedPitch == 1) base.push_back ({ sf2::op::scaleTuning, 0 });
            base.push_back ({ sf2::op::rootKey, juce::jlimit (0, 127, kb.root) });

            makeInstrument();
            auto withPan = [&] (double p) { auto g = base; const int v = (int) std::lround (juce::jlimit (-1.0, 1.0, p) * 500.0); if (v != 0) g.push_back ({ sf2::op::pan, v }); return g; };
            if (stereo)
            {
                out.addZone (withPan (-1.0 + pan), sL);
                out.addZone (withPan (1.0 + pan), sR);
            }
            else out.addZone (withPan (pan), sL);
            ++res.keybanks;
        }
        if (instrumentMade) ++res.waveforms;
    }

    res.skipped = encrypted + compressed + damaged;
    if (out.instrumentCount() == 0)
    {
        if (encrypted > 0) res.error = "PROTECTED YAMAHA CONTENT: WAVEFORMS ARE ENCRYPTED";
        else if (compressed > 0) res.error = "WAVEFORMS ARE COMPRESSED: NOT SUPPORTED";
        else res.error = "NO PLAYABLE WAVEFORMS IN FILE";
        return res;
    }
    if (out.sampleBytes() > 1.9e9) { res.error = "FILE TOO LARGE"; return res; }

    res.notes.add (res.model + ": " + juce::String (res.waveforms) + " waveform, " + juce::String (res.keybanks) + " keybank");
    if (encrypted > 0) res.notes.add (juce::String (encrypted) + " keybank cifrati saltati (contenuti protetti)");
    if (compressed > 0) res.notes.add (juce::String (compressed) + " keybank compressi saltati");
    if (damaged > 0) res.notes.add (juce::String (damaged) + " keybank illeggibili saltati");
    if (backwards > 0) res.notes.add (juce::String (backwards) + " loop all'indietro diventati loop in avanti");
    if (! epfm.empty()) res.notes.add (juce::String ((int) epfm.size()) + " performance non convertite: solo le waveform");

    res.sf2 = out.build (file.getFileNameWithoutExtension());
    return res;
}
}
