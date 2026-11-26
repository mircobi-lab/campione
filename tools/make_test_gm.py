#!/usr/bin/env python3
"""Crea un SoundFont multibanco di prova, organizzato come un GM/XG/GS (suoni sintetici).
Uso: python3 tools/make_test_gm.py prova_gm.sf2
Ogni preset è una sinusoide in loop con nota base 69: suonando il La4 si sente una frequenza diversa per
ogni preset, così la prova capisce quale preset è stato scelto.
  banco   0 prog  0  "GM Piano"     440 Hz
  banco   0 prog  1  "GM Bright"    500 Hz
  banco   1 prog  0  "XG Variation" 550 Hz   (XG: CC 0 = 0, CC 32 = 1)
  banco   8 prog  0  "GS Variation" 400 Hz   (GS: CC 0 = 8)
  banco 128 prog  0  "Standard Kit" 275 Hz   (canale 10)
  banco 128 prog 25  "Analog Kit"   880 Hz   (XG: CC 0 = 127, programma 25)
"""
import math, struct, sys

SR = 44000          # ogni frequenza ha un numero intero di campioni per periodo: loop senza salti
LEN = SR // 2
PRESETS = [("GM Piano", 0, 0, 440), ("GM Bright", 0, 1, 500), ("XG Variation", 1, 0, 550),
           ("GS Variation", 8, 0, 400), ("Standard Kit", 128, 0, 275), ("Analog Kit", 128, 25, 880)]

def chunk(cid, data):
    if len(data) % 2: data += b"\0"
    return cid + struct.pack("<I", len(data)) + data

def lst(kind, chunks):
    body = kind + b"".join(chunks)
    return b"LIST" + struct.pack("<I", len(body)) + body

def name20(s): return s.encode("ascii")[:19].ljust(20, b"\0")
def gen(op, amount): return struct.pack("<Hh", op, amount)

smpl, shdr = b"", b""
phdr = pbag = pgen = inst = ibag = igen = b""
pos = 0
for i, (nm, bank, prog, freq) in enumerate(PRESETS):
    data = [int(math.sin(2 * math.pi * freq * n / SR) * 16000) for n in range(LEN)]
    smpl += struct.pack("<%dh" % LEN, *data) + b"\0\0" * 46
    shdr += name20(nm) + struct.pack("<IIIIIBbHH", pos, pos + LEN, pos, pos + LEN, SR, 69, 0, 0, 1)
    pos += LEN + 46
    phdr += name20(nm) + struct.pack("<HHHIII", prog, bank, i, 0, 0, 0)
    pbag += struct.pack("<HH", i, 0)
    pgen += gen(41, i)
    inst += name20(nm) + struct.pack("<H", i)
    ibag += struct.pack("<HH", 3 * i, 0)
    igen += gen(54, 1) + gen(58, 69) + gen(53, i)
n = len(PRESETS)
phdr += name20("EOP") + struct.pack("<HHHIII", 0, 0, n, 0, 0, 0)
pbag += struct.pack("<HH", n, 0)
pgen += gen(0, 0)
inst += name20("EOI") + struct.pack("<H", n)
ibag += struct.pack("<HH", 3 * n, 0)
igen += gen(0, 0)
shdr += name20("EOS") + bytes(26)
mod = bytes(10)

info = lst(b"INFO", [chunk(b"ifil", struct.pack("<HH", 2, 1)), chunk(b"isng", b"EMU8000\0"), chunk(b"INAM", b"Campione GM test\0")])
sdta = lst(b"sdta", [chunk(b"smpl", smpl)])
pdta = lst(b"pdta", [chunk(b"phdr", phdr), chunk(b"pbag", pbag), chunk(b"pmod", mod), chunk(b"pgen", pgen),
                     chunk(b"inst", inst), chunk(b"ibag", ibag), chunk(b"imod", mod), chunk(b"igen", igen), chunk(b"shdr", shdr)])
body = b"sfbk" + info + sdta + pdta
open(sys.argv[1], "wb").write(b"RIFF" + struct.pack("<I", len(body)) + body)
print("scritto", sys.argv[1])
