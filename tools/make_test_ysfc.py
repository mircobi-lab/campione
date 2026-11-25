#!/usr/bin/env python3
"""Crea file Yamaha YSFC di prova con suoni sintetici (nessun campione di terzi).
Uso: python3 tools/make_test_ysfc.py CARTELLA
Scrive:
  CARTELLA/prova_montage.X7L   versione 4.0.5 (Montage): keybank di 64 byte, stereo = due keybank,
                               più una waveform cifrata che Campione deve saltare e una performance
  CARTELLA/prova_motif.X3W     versione 1.0.2 (Motif XF): keybank di 68 byte little endian
  CARTELLA/prova_moxf.X6W      versione 1.0.3 (MOXF): keybank di 76 byte big endian
In ogni file: waveform 1 "Test Piano" mono, La4 che decade; waveform 2 "Test Organ" stereo in loop,
canale sinistro più forte. Nota base 69 (La4 a 440 Hz) per tutte e due.
La disposizione dei byte segue la documentazione di Chris Webb (github.com/arachsys/montage) e lo
scrittore YSFC di ConvertWithMoss (github.com/git-moss/ConvertWithMoss).
"""
import math, os, struct, sys

SR = 44000
ROOT = 69
out = sys.argv[1]
os.makedirs(out, exist_ok=True)

def tone(seconds, decay, gain=1.0):
    res = []
    for i in range(int(SR * seconds)):
        t = i / SR
        env = math.exp(-t * decay) if decay else 1.0
        att = min(1.0, i / 40)
        v = sum(a * math.sin(2 * math.pi * 440 * k * t) for k, a in ((1, .6), (2, .25), (3, .12), (4, .05)))
        res.append(int(max(-1, min(1, v * env * att * gain)) * 20000))
    return res

def pcm(data): return struct.pack("<%dh" % len(data), *data)   # sempre little endian

piano = tone(2.0, 2.2)
organ_l = tone(0.5, 0)
organ_r = tone(0.5, 0, 0.5)
# loop dell'organo: 1000 campioni (10 periodi di 440 Hz a 44 kHz), inizio non multiplo di 16
LS, LE = 20100, 21099   # fine compresa

def keybank(fam, chans, length, loop):
    """fam: 'modern' (Montage/MODX), 'motif', 'moxf'"""
    ls, le, mode = (LS, LE, 0) if loop else (0, length, 1)
    v1 = fam != "modern"
    b = bytes([0, 127, 1, 127, 128 if v1 else 255, 64, 0, 0 if v1 else 0xFF, ROOT, 64, 64, chans,
               0 if v1 else 2, 0 if v1 else 2, 0 if v1 else 5, mode, 0, 0, 0 if v1 else ls % 16])
    b += bytes(13)
    if fam == "motif":
        b += struct.pack("<IIIII", SR, 0, ls, le, length) + bytes(8) + b"\xff" * 8
        assert len(b) == 68
    elif fam == "moxf":
        b += bytes(3) + b"\xff"
        b += struct.pack(">IIIII", SR, 0, ls, le, length)
        b += struct.pack(">HH", 0, 0) + b"\xff" * 4 + struct.pack(">H", 1) + bytes(2) + struct.pack(">I", length * chans) + bytes(4)
        assert len(b) == 76
    else:
        b += bytes(3) + b"\xff"
        b += struct.pack("<IIIIII", SR, 0, ls // 16, le, 0, 1) + struct.pack("<I", length)
        assert len(b) == 64
    return b

def encrypted(b):
    return b[:16] + b"\x01" + b[17:]

def entry(version, number, name, title=""):
    nm = name.encode("ascii") + b"\0" + title.encode("ascii") + b"\0"
    # dimensione e offset del dato corrispondente: Campione non li usa, si lasciano a zero
    if version <= 102:
        head = bytes(4) + struct.pack(">I", 0) + bytes(4) + struct.pack(">II", 0, number) + bytes(1 if version <= 101 else 2)
    elif version == 103:
        head = struct.pack(">III", 0, 0, number)
    else:
        head = struct.pack(">III", 0, 0, number) + bytes(6) + struct.pack(">I", 0x2711 + number)
    return head + nm

def block(cid, magic, items):
    body = b"".join(magic + struct.pack(">I", len(x)) + x for x in items)
    return cid + struct.pack(">II", 4 + len(body), len(items)) + body

def ysfc(path, version, fam, waveforms, performances=0):
    """waveforms: lista di (nome, [keybank...], [canali pcm...])"""
    v1 = fam != "modern"
    ewfm = [entry(version, 0x10001 + i, "%d:%s" % (0 if i == 0 else 32, w[0])) for i, w in enumerate(waveforms)]
    ewim = [entry(version, 0x10001 + i, "%d:%s" % (0 if i == 0 else 32, w[0])) for i, w in enumerate(waveforms)]
    dwfm = [struct.pack(">H" if v1 else "<H", len(w[1])) + bytes(2) + b"".join(w[1]) for w in waveforms]
    dwim = [struct.pack(">I", len(w[2])) + b"".join(struct.pack(">I", len(c)) + c for c in w[2]) for w in waveforms]
    blocks = [block(b"EWFM", b"Entr", ewfm), block(b"EWIM", b"Entr", ewim)]
    if performances:
        blocks.insert(0, block(b"EPFM", b"Entr", [entry(version, 0x3f2000 + i, "0:Perf %d" % i, "Perf %d" % i) for i in range(performances)]))
        blocks.append(block(b"DPFM", b"Data", [bytes(64) for _ in range(performances)]))
    blocks += [block(b"DWFM", b"Data", dwfm), block(b"DWIM", b"Data", dwim)]
    library = b"" if v1 else b"\xff" * 80 + b"\x00"
    catalogue_size = 8 * len(blocks)
    pos = 64 + catalogue_size + len(library)
    catalogue = b""
    for b in blocks:
        catalogue += b[:4] + struct.pack(">I", pos)
        pos += len(b)
    head = b"YAMAHA-YSFC".ljust(16, b"\0") + version_str(version).ljust(16, b"\0")
    head += struct.pack(">I", catalogue_size) + b"\xff" * 12 + struct.pack(">I", len(library)) + b"\xff" * 8
    head += b"\xff" * 4 if v1 else struct.pack(">I", 0x2711 + 10)
    assert len(head) == 64
    with open(path, "wb") as f:
        f.write(head + catalogue + library + b"".join(blocks))

def version_str(v): return ("%d.%d.%d" % (v // 100, v // 10 % 10, v % 10)).encode("ascii")

def waves(fam, with_encrypted=False):
    n = len(organ_l)
    if fam == "modern":
        organ_kb = [keybank(fam, 2, n, True), keybank(fam, 2, n, True)]   # un keybank per canale
    else:
        organ_kb = [keybank(fam, 2, n, True)]
    w = [("Test Piano", [keybank(fam, 1, len(piano), False)], [pcm(piano)]),
         ("Test Organ", organ_kb, [pcm(organ_l), pcm(organ_r)])]
    if with_encrypted:
        w.append(("Test Locked", [encrypted(keybank(fam, 1, len(piano), False))], [pcm(piano)]))
    return w

ysfc(os.path.join(out, "prova_montage.X7L"), 405, "modern", waves("modern", True), performances=2)
ysfc(os.path.join(out, "prova_motif.X3W"), 102, "motif", waves("motif"))
ysfc(os.path.join(out, "prova_moxf.X6W"), 103, "moxf", waves("moxf"))
print("scritti prova_montage.X7L, prova_motif.X3W, prova_moxf.X6W in", out)
