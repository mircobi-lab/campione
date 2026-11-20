#!/usr/bin/env python3
"""Crea un piccolo SoundFont 2 di prova (suoni sintetici, nessun campione di terzi) per verificare Campione.
Uso: python3 tools/make_test_sf2.py prova.sf2
Preset: 000:000 TEST PIANO (decade, senza loop), 000:016 TEST ORGAN (loop), 001:000 TEST BANK ONE."""
import math, struct, sys

SR = 44000                      # 440 Hz = periodo di 100 campioni esatti: loop senza scatti
ROOT = 69

def chunk(cid, data):
    if len(data) % 2: data += b"\0"
    return cid + struct.pack("<I", len(data)) + data

def lst(kind, chunks):
    body = kind + b"".join(chunks)
    return b"LIST" + struct.pack("<I", len(body)) + body

def name20(s):
    return s.encode("ascii")[:19].ljust(20, b"\0")

def tone(seconds, decay):
    out = []
    for i in range(int(SR * seconds)):
        t = i / SR
        env = math.exp(-t * decay) if decay else 1.0
        att = min(1.0, i / 40)
        v = sum(a * math.sin(2 * math.pi * 440 * k * t) for k, a in ((1, .6), (2, .25), (3, .12), (4, .05)))
        out.append(int(max(-1, min(1, v * env * att)) * 20000))
    return out

piano = tone(2.0, 2.2)
organ = tone(0.5, 0)
samples, headers = [], []
for nm, data, loop in (("piano", piano, False), ("organ", organ, True)):
    start = len(samples)
    samples += data + [0] * 46
    end = start + len(data)
    ls, le = (end - 2000, end - 1000) if loop else (start, end)
    headers.append(name20(nm) + struct.pack("<IIIIIBbHH", start, end, ls, le, SR, ROOT, 0, 0, 1))
headers.append(name20("EOS") + b"\0" * 26)
smpl = struct.pack("<%dh" % len(samples), *samples)

def gen(op, amount): return struct.pack("<Hh", op, amount)
def gen_range(op, lo, hi): return struct.pack("<HBB", op, lo, hi)

# strumenti: gen 43 keyRange, 54 sampleModes, 38 releaseVolEnv (timecents), 37 sustainVolEnv (cB), 53 sampleID
igen, ibag, inst = [], [], []
for nm, sid, loop, rel in (("TestPiano", 0, False, -1200), ("TestOrgan", 1, True, -2400)):
    inst.append(name20(nm) + struct.pack("<H", len(ibag)))
    ibag.append(struct.pack("<HH", len(igen), 0))
    igen.append(gen_range(43, 0, 127))
    if loop: igen.append(gen(54, 1))
    igen.append(gen(38, rel))
    igen.append(gen(53, sid))
inst.append(name20("EOI") + struct.pack("<H", len(ibag)))
ibag.append(struct.pack("<HH", len(igen), 0))
igen.append(b"\0" * 4)

pgen, pbag, phdr = [], [], []
for nm, prog, bank, ins in (("Test Piano", 0, 0, 0), ("Test Organ", 16, 0, 1), ("Test Bank One", 0, 1, 0)):
    phdr.append(name20(nm) + struct.pack("<HHHIII", prog, bank, len(pbag), 0, 0, 0))
    pbag.append(struct.pack("<HH", len(pgen), 0))
    pgen.append(gen(41, ins))
phdr.append(name20("EOP") + struct.pack("<HHHIII", 0, 0, len(pbag), 0, 0, 0))
pbag.append(struct.pack("<HH", len(pgen), 0))
pgen.append(b"\0" * 4)

info = lst(b"INFO", [chunk(b"ifil", struct.pack("<HH", 2, 1)), chunk(b"isng", b"EMU8000\0"), chunk(b"INAM", b"Campione Test\0")])
sdta = lst(b"sdta", [chunk(b"smpl", smpl)])
pdta = lst(b"pdta", [chunk(b"phdr", b"".join(phdr)), chunk(b"pbag", b"".join(pbag)), chunk(b"pmod", b"\0" * 10),
                     chunk(b"pgen", b"".join(pgen)), chunk(b"inst", b"".join(inst)), chunk(b"ibag", b"".join(ibag)),
                     chunk(b"imod", b"\0" * 10), chunk(b"igen", b"".join(igen)), chunk(b"shdr", b"".join(headers))])
body = b"sfbk" + info + sdta + pdta
open(sys.argv[1], "wb").write(b"RIFF" + struct.pack("<I", len(body)) + body)
print("scritto", sys.argv[1], len(body) + 8, "byte")
