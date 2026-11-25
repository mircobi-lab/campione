#!/usr/bin/env python3
"""Crea i file di prova per i formati SF3 e SFZ (suoni sintetici, nessun campione di terzi).
Uso: python3 tools/make_test_formats.py CARTELLA     (serve oggenc, pacchetto vorbis-tools)
Scrive:
  CARTELLA/prova.sf3                         stesso contenuto di make_test_sf2.py, campioni in Ogg Vorbis
  CARTELLA/prova_sfz/prova.sfz               due regioni: piano mono (tasti 0-63) e organo stereo in loop (64-127),
  CARTELLA/prova_sfz/campioni/*.wav          più una regione trigger=release che Campione deve saltare
"""
import math, os, struct, subprocess, sys, tempfile, wave

SR = 44000
ROOT = 69
out = sys.argv[1]
os.makedirs(os.path.join(out, "prova_sfz", "campioni"), exist_ok=True)

def tone(seconds, decay):
    res = []
    for i in range(int(SR * seconds)):
        t = i / SR
        env = math.exp(-t * decay) if decay else 1.0
        att = min(1.0, i / 40)
        v = sum(a * math.sin(2 * math.pi * 440 * k * t) for k, a in ((1, .6), (2, .25), (3, .12), (4, .05)))
        res.append(int(max(-1, min(1, v * env * att)) * 20000))
    return res

def write_wav(path, chans):
    with wave.open(path, "wb") as w:
        w.setnchannels(len(chans)); w.setsampwidth(2); w.setframerate(SR)
        frames = b"".join(struct.pack("<" + "h" * len(chans), *f) for f in zip(*chans))
        w.writeframes(frames)

def chunk(cid, data):
    if len(data) % 2: data += b"\0"
    return cid + struct.pack("<I", len(data)) + data

def lst(kind, chunks):
    body = kind + b"".join(chunks)
    return b"LIST" + struct.pack("<I", len(body)) + body

def name20(s): return s.encode("ascii")[:19].ljust(20, b"\0")
def gen(op, amount): return struct.pack("<Hh", op, amount)
def gen_range(op, lo, hi): return struct.pack("<HBB", op, lo, hi)

piano = tone(2.0, 2.2)
organ = tone(0.5, 0)

# ------------------------------------------------------------------ SF3: smpl = flussi Ogg uno dopo l'altro
blob, headers = b"", []
tmp = tempfile.mkdtemp()
for nm, data, loop in (("piano", piano, False), ("organ", organ, True)):
    wav = os.path.join(tmp, nm + ".wav"); ogg = os.path.join(tmp, nm + ".ogg")
    write_wav(wav, [data])
    subprocess.run(["oggenc", "-Q", "-q", "8", "-o", ogg, wav], check=True)
    o = open(ogg, "rb").read()
    start, end = len(blob), len(blob) + len(o)
    blob += o
    if len(blob) % 2: blob += b"\0"
    # SF3: inizio e fine in byte dentro smpl, punti di loop relativi all'inizio del campione decodificato
    ls, le = (len(data) - 2000, len(data) - 1000) if loop else (0, len(data))
    headers.append(name20(nm) + struct.pack("<IIIIIBbHH", start, end, ls, le, SR, ROOT, 0, 0, 1 | 0x10))
headers.append(name20("EOS") + b"\0" * 26)

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
for nm, prog, bank, ins in (("Test Piano", 0, 0, 0), ("Test Organ", 16, 0, 1)):
    phdr.append(name20(nm) + struct.pack("<HHHIII", prog, bank, len(pbag), 0, 0, 0))
    pbag.append(struct.pack("<HH", len(pgen), 0))
    pgen.append(gen(41, ins))
phdr.append(name20("EOP") + struct.pack("<HHHIII", 0, 0, len(pbag), 0, 0, 0))
pbag.append(struct.pack("<HH", len(pgen), 0))
pgen.append(b"\0" * 4)
info = lst(b"INFO", [chunk(b"ifil", struct.pack("<HH", 3, 1)), chunk(b"isng", b"EMU8000\0"), chunk(b"INAM", b"Campione Test SF3\0")])
sdta = lst(b"sdta", [chunk(b"smpl", blob)])
pdta = lst(b"pdta", [chunk(b"phdr", b"".join(phdr)), chunk(b"pbag", b"".join(pbag)), chunk(b"pmod", b"\0" * 10),
                     chunk(b"pgen", b"".join(pgen)), chunk(b"inst", b"".join(inst)), chunk(b"ibag", b"".join(ibag)),
                     chunk(b"imod", b"\0" * 10), chunk(b"igen", b"".join(igen)), chunk(b"shdr", b"".join(headers))])
body = b"sfbk" + info + sdta + pdta
open(os.path.join(out, "prova.sf3"), "wb").write(b"RIFF" + struct.pack("<I", len(body)) + body)

# ------------------------------------------------------------------ SFZ
d = os.path.join(out, "prova_sfz")
write_wav(os.path.join(d, "campioni", "piano mono.wav"), [piano])
write_wav(os.path.join(d, "campioni", "organo.wav"), [organ, [int(v * .8) for v in organ]])
write_wav(os.path.join(d, "campioni", "rilascio.wav"), [tone(0.2, 20)])
open(os.path.join(d, "prova.sfz"), "w").write(f"""// prova di Campione: commenti, #define, header, nomi di nota e percorsi con spazi
/* commento
   su più righe */
#define $CENTRO 69
<control> default_path=campioni\\
<global> ampeg_release=0.3
<group> volume=-3
<region> sample=piano mono.wav lokey=0 hikey=63 pitch_keycenter=$CENTRO
<region> sample=organo.wav lokey=e4 hikey=127 pitch_keycenter=a4 loop_mode=loop_continuous loop_start={len(organ) - 2000} loop_end={len(organ) - 1001}
<group> trigger=release
<region> sample=rilascio.wav lokey=0 hikey=127
""")
print("scritti", os.path.join(out, "prova.sf3"), "e", os.path.join(d, "prova.sfz"))
