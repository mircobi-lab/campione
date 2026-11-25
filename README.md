# MBStudio91 Campione

Campione è un player SoundFont (SF2 e SF3) e SFZ pensato per il pianoforte, in formato VST3, AU (Mac) e applicazione autonoma. L'interfaccia segue lo stile hardware dei plugin MBStudio91 (MuMU, LA-91, ICX7): telaio nero, scritte bianche, colore solo nei LED.

![Pannello di Campione](docs/pannello.png)

## Scaricare

Ogni aggiornamento del ramo `main` viene compilato da GitHub Actions. I file pronti sono nella pagina **Releases**:

- `Campione-VST3-mac.zip`, `Campione-AU-mac.zip`, `Campione-App-mac.zip` per Mac (Apple Silicon e Intel, macOS 11 o successivo)
- `Campione-VST3-win.zip`, `Campione-App-win.zip` per Windows a 64 bit

Il plugin per Mac non è firmato da Apple. Dopo averlo copiato serve questo comando nel Terminale:

```
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"MBStudio91 Campione.vst3" ~/Library/Audio/Plug-Ins/Components/"MBStudio91 Campione.component" /Applications/"MBStudio91 Campione.app"
```

## Come si usa

Campione non contiene suoni: si carica un proprio file con **LOAD** oppure trascinandolo sul pannello. Formati: SoundFont 2 (`.sf2`), SoundFont compresso (`.sf3`, campioni Ogg Vorbis, come quelli di MuseScore) e SFZ (`.sfz` con la cartella dei suoi campioni WAV, AIFF, FLAC o OGG). Parte il preset banco 0 programma 0 (il pianoforte acustico nei SoundFont General MIDI), altrimenti il primo dell'elenco. Un clic sul display apre l'elenco dei preset, diviso per banco; `<` e `>` passano al precedente e al successivo. Il percorso del file e il preset vengono salvati nel progetto. Il display LCD in alto a sinistra della sezione dei controlli mostra il nome del file su una matrice di punti 5x7 in blu LED: se supera i 16 caratteri scorre.

| Sezione | Controlli |
|---|---|
| PIANO (sotto il display LCD) | TONE (inclinazione ±9 dB, 250 Hz e 2,5 kHz), TUNE (±50 centesimi), TRANSP (±12 semitoni), A4 REF (440 o 432 Hz), VEL CURVE (NORMAL, SOFT, HARD, FIXED) |
| AMP ENVELOPE | Con USE SF2 ENV acceso vale l'inviluppo del SoundFont. Spento, ATTACK, DECAY, SUSTAIN e RELEASE lo sostituiscono per le note nuove |
| PEDAL | SUSTAIN (anche CC 64), SOFT una corda (anche CC 67), SOFT AMT quanto abbassa la dinamica. Il CC 64 è continuo: sotto 24 pedale su, da 100 in su pedale giù, in mezzo **mezzo pedale** (le note tenute si smorzano, da 0,25 s a 10 s secondo quanto è giù il pedale; il LED SUSTAIN diventa HALF a metà luce) |
| ROOM | Riverbero: SIZE, DAMP, MIX, acceso o spento |
| OUTPUT | VOLUME e LED meter |

Altro dal MIDI: Program Change con banco da CC 0 e CC 32, pitch bend (±2 semitoni), CC 7, 10, 11, CC 120 e 123. Il tasto PANIC spegne tutte le voci in pochi millisecondi e azzera pedali e pitch bend. LEARN e CONTROLLER fanno il MIDI Learn su qualsiasi controllo e caricano i profili dei controller, come negli altri plugin MBStudio91.

## Limiti attuali

- SFZ: al caricamento il file viene convertito in un SoundFont in memoria, quindi valgono le possibilità di un SoundFont. Si leggono le regioni con tasti, velocity, intonazione, volume, pan, punti di inizio, fine e loop, inviluppo d'ampiezza e gruppi esclusivi (`group` con `off_by` uguale). Vengono saltate e contate sul display le regioni `trigger=release`, i round robin (`seq_position` oltre 1, `lorand` sopra 0), i keyswitch diversi da `sw_default` e i generatori interni (`*sine`, `*noise`). I campioni diventano a 16 bit; il limite è di circa 400 milioni di campioni per libreria, oltre si salta il resto e il display lo dice.
- Il motore è TinySoundFont, che non applica i modulatori del SoundFont e ignora le mandate di chorus e riverbero scritte nel file. Il riverbero di Campione è la sezione ROOM.
- Il mezzo pedale smorza le note tenute con un rilascio più o meno lungo: non è un modello fisico delle corde e degli smorzatori.
- 256 voci. Prima di ogni nota ne restano sempre almeno 24 libere: le più vecchie si chiudono con una dissolvenza di 30 ms, anche col pedale premuto. Un tasto colpito di nuovo chiude in 80 ms il suono precedente dello stesso tasto.

## Compilare in proprio

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

JUCE 9.0.5 viene scaricato da CMake. Per la prova del motore senza interfaccia:

```
cmake -S . -B build -DCAMPIONE_BUILD_TOOLS=ON
cmake --build build --target campione_render
python3 tools/make_test_sf2.py prova.sf2
./build/campione_render_artefacts/Release/campione_render prova.sf2 accordo.wav
```

`make_test_sf2.py` crea un SoundFont con suoni sintetici; la prova controlla intonazione, trasposizione, 432 Hz, sustain da pannello e da CC 64, inviluppo sostituito e ripristinato, curva di velocity, pedale una corda, Program Change, PANIC, 88 tasti col pedale premuto e una raffica di note senza click. Gira anche su GitHub a ogni commit.

## Licenze

- Codice di Campione: GNU AGPL v3 (`LICENSE.md`), come gli altri plugin MBStudio91 basati su JUCE.
- TinySoundFont di Bernhard Schelling, licenza MIT (`third_party/TinySoundFont/LICENSE`), file `tsf.h` dal commit 790a219.
- stb_vorbis 1.25 di Sean Barrett, pubblico dominio o MIT a scelta (licenza in fondo a `third_party/stb_vorbis/stb_vorbis.c`), dal commit 6b5799b di github.com/nothings/stb: decodifica i campioni dei file SF3.
- Font Bebas Neue e Antonio: SIL Open Font License 1.1 (`MBStudioUI/fonts`).
- VST è un marchio registrato di Steinberg Media Technologies GmbH.
