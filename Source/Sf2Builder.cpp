#include "Sf2Builder.h"
#include <cmath>

namespace campione::sf2
{
namespace
{
struct Writer
{
    juce::MemoryOutputStream os;
    void u8 (int v) { os.writeByte ((char) v); }
    void u16 (int v) { os.writeShort ((short) v); }
    void u32 (juce::uint32 v) { os.writeInt ((int) v); }
    void name20 (const juce::String& s)
    {
        char b[20] {};
        s.substring (0, 19).copyToUTF8 (b, 20);
        for (int i = 0; i < 20; ++i) if ((unsigned char) b[i] > 127) b[i] = '_';
        os.write (b, 20);
    }
};

juce::MemoryBlock chunk (const char* id, const juce::MemoryBlock& data)
{
    juce::MemoryOutputStream m;
    m.write (id, 4);
    m.writeInt ((int) data.getSize());
    m.write (data.getData(), data.getSize());
    if (data.getSize() & 1) m.writeByte (0);
    return m.getMemoryBlock();
}

juce::MemoryBlock list (const char* kind, std::initializer_list<juce::MemoryBlock> parts)
{
    juce::MemoryOutputStream body;
    body.write (kind, 4);
    for (auto& p : parts) body.write (p.getData(), p.getSize());
    return chunk ("LIST", body.getMemoryBlock());
}
}

int timecents (double seconds)
{
    return seconds <= 0.001 ? -12000 : juce::jlimit (-12000, 8000, (int) std::lround (1200.0 * std::log2 (seconds)));
}

void addOffset (std::vector<Gen>& gens, int fineOp, int coarseOp, int offset)
{
    if (offset == 0) return;
    const int sign = offset < 0 ? -1 : 1;
    const int a = std::abs (offset);
    gens.push_back ({ fineOp, sign * (a % 32768) });
    if (a >= 32768) gens.push_back ({ coarseOp, sign * (a / 32768) });
}

int Builder::addSample (const juce::String& name, const int16_t* data, int frames, int rate)
{
    const auto start = (juce::uint32) (smpl.getDataSize() / 2);
    smpl.write (data, (size_t) frames * 2);
    for (int i = 0; i < 46; ++i) smpl.writeShort (0);   // regola SF2: 46 zeri dopo ogni campione
    headers.push_back ({ name, start, start + (juce::uint32) frames, rate, 0, 1 });
    return (int) headers.size() - 1;
}

void Builder::linkStereo (int left, int right)
{
    auto& l = headers[(size_t) left];
    auto& r = headers[(size_t) right];
    l.link = right; l.type = 4;
    r.link = left;  r.type = 2;
}

int Builder::addInstrument (const juce::String& name)
{
    instruments.push_back ({ name, {} });
    return (int) instruments.size() - 1;
}

void Builder::addZone (std::vector<Gen> gens, int sample)
{
    jassert (! instruments.empty());
    instruments.back().zones.push_back ({ std::move (gens), sample });
}

void Builder::addPreset (const juce::String& name, int bank, int program, int instrument)
{
    presets.push_back ({ name, bank, program, instrument });
}

int Builder::zoneCount() const noexcept
{
    int n = 0;
    for (auto& i : instruments) n += (int) i.zones.size();
    return n;
}

juce::MemoryBlock Builder::build (const juce::String& bankName) const
{
    Writer phdr, pbag, pmod, pgen, inst, ibag, imod, igen, sh;

    int presetBag = 0;
    for (auto& p : presets)
    {
        phdr.name20 (p.name); phdr.u16 (p.program); phdr.u16 (p.bank); phdr.u16 (presetBag); phdr.u32 (0); phdr.u32 (0); phdr.u32 (0);
        pbag.u16 (presetBag); pbag.u16 (0);
        pgen.u16 (op::instrument); pgen.u16 (p.instrument);
        ++presetBag;
    }
    phdr.name20 ("EOP"); phdr.u16 (0); phdr.u16 (0); phdr.u16 (presetBag); phdr.u32 (0); phdr.u32 (0); phdr.u32 (0);
    pbag.u16 (presetBag); pbag.u16 (0);
    pgen.u16 (0); pgen.u16 (0);
    for (int i = 0; i < 10; ++i) pmod.u8 (0);
    for (int i = 0; i < 10; ++i) imod.u8 (0);

    int bag = 0, genCount = 0;
    for (auto& in : instruments)
    {
        inst.name20 (in.name); inst.u16 (bag);
        for (auto& z : in.zones)
        {
            ibag.u16 (genCount); ibag.u16 (0);
            for (auto& g : z.gens) { igen.u16 (g.op); igen.u16 (g.amount); ++genCount; }
            igen.u16 (op::sampleID); igen.u16 (z.sample); ++genCount;   // sampleID per ultimo
            ++bag;
        }
    }
    inst.name20 ("EOI"); inst.u16 (bag);
    ibag.u16 (genCount); ibag.u16 (0);
    igen.u16 (0); igen.u16 (0);

    for (auto& h : headers)
    {
        sh.name20 (h.name); sh.u32 (h.start); sh.u32 (h.end); sh.u32 (h.start); sh.u32 (h.end); sh.u32 ((juce::uint32) h.rate);
        sh.u8 (60); sh.u8 (0); sh.u16 (h.link); sh.u16 (h.type);
    }
    sh.name20 ("EOS"); for (int i = 0; i < 5; ++i) sh.u32 (0); sh.u8 (0); sh.u8 (0); sh.u16 (0); sh.u16 (0);

    juce::MemoryOutputStream ifil; ifil.writeShort (2); ifil.writeShort (1);
    juce::MemoryOutputStream isng; isng.write ("EMU8000\0", 8);
    juce::MemoryOutputStream inam; { auto n = bankName.substring (0, 60).toStdString(); n.push_back (0); if (n.size() & 1) n.push_back (0); inam.write (n.data(), n.size()); }
    juce::MemoryOutputStream isft; isft.write ("MBStudio91 Campione\0", 20);

    const auto info = list ("INFO", { chunk ("ifil", ifil.getMemoryBlock()), chunk ("isng", isng.getMemoryBlock()),
                                      chunk ("INAM", inam.getMemoryBlock()), chunk ("ISFT", isft.getMemoryBlock()) });
    const auto sdta = list ("sdta", { chunk ("smpl", smpl.getMemoryBlock()) });
    const auto pdta = list ("pdta", { chunk ("phdr", phdr.os.getMemoryBlock()), chunk ("pbag", pbag.os.getMemoryBlock()),
                                      chunk ("pmod", pmod.os.getMemoryBlock()), chunk ("pgen", pgen.os.getMemoryBlock()),
                                      chunk ("inst", inst.os.getMemoryBlock()), chunk ("ibag", ibag.os.getMemoryBlock()),
                                      chunk ("imod", imod.os.getMemoryBlock()), chunk ("igen", igen.os.getMemoryBlock()),
                                      chunk ("shdr", sh.os.getMemoryBlock()) });
    juce::MemoryOutputStream body;
    body.write ("sfbk", 4);
    for (auto* b : { &info, &sdta, &pdta }) body.write (b->getData(), b->getSize());
    return chunk ("RIFF", body.getMemoryBlock());
}
}
