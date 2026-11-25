# MBStudio91 Campione

Campione è un player SoundFont (SF2 e SF3), SFZ e waveform Yamaha (Motif, MOXF, Montage, MODX) pensato per il pianoforte, in formato VST3, AU (Mac) e applicazione autonoma. L'interfaccia segue lo stile hardware dei plugin MBStudio91 (MuMU, LA-91, ICX7): telaio nero, scritte bianche, colore solo nei LED.

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

Campione non contiene suoni: si carica un proprio file con **LOAD** oppure trascinandolo sul pannello. Formati: SoundFont 2 (`.sf2`), SoundFont compresso (`.sf3`, campioni Ogg Vorbis, come quelli di MuseScore), SFZ (`.sfz` con la cartella dei suoi campioni WAV, AIFF, FLAC o OGG) e file Yamaha con waveform utente (vedi sotto). Parte il preset banco 0 programma 0 (il pianoforte acustico nei SoundFont General MIDI), altrimenti il primo dell'elenco. Un clic sul display apre l'elenco dei preset, diviso per banco; `<` e `>` passano al precedente e al successivo. Il percorso del file e il preset vengono salvati nel progetto. Il display LCD in alto a sinistra della sezione dei controlli mostra il nome del file su una matrice di punti 5x7 in blu LED: se supera i 16 caratteri scorre.

Ci sono quattro banchi, **A B C D**, sotto il nome del prodotto. Ognuno ha il suo SoundFont e il suo preset: si sceglie il banco con il tasto, poi LOAD (o il trascinamento) carica il file in quel banco. Il LED blu indica il banco che suona; i banchi vuoti hanno la scritta spenta. Il cambio è istantaneo: tutti i file restano in memoria, e una nota partita in un banco finisce nel suo banco anche se nel frattempo si è passati a un altro. Pedali, pitch bend, CC 7, 10, 11, 120 e 123 arrivano a tutti i banchi; Program Change e scelta del banco MIDI (CC 0 e 32) solo a quello attivo. Pedali e mezzo pedale valgono per tutti i banchi. Il banco è anche un parametro (BANK), quindi si automatizza dalla DAW o si assegna con LEARN a un tasto o a una manopola del controller.

### SoundFont multibanco GM e XG

Un SoundFont General MIDI, GS o XG (per esempio GeneralUser GS, FluidR3 GM o un banco XG) si carica come gli altri. Con il tasto **GM/XG** acceso, accanto ai banchi, Campione funziona come un modulo GM a 16 canali:

- ogni canale MIDI suona il suo programma, scelto con Program Change
- il banco si sceglie con CC 0 (MSB) e CC 32 (LSB) prima del Program Change: varianti XG con MSB 0 e LSB = variante, varianti GS con MSB = variante, GM2 con MSB 121 e LSB = variante
- il canale 10 suona la batteria (banco 128 del SoundFont); MSB 127 sceglie un kit XG su qualsiasi canale
- se la variante chiesta non c'è nel file suona lo stesso programma del banco 0, come fanno i moduli GM
- volume (CC 7), pan (CC 10), expression (CC 11), pitch bend, sustain e estensione del pitch bend (RPN 0) per canale
- TRANSP non sposta la batteria

Con GM/XG spento tutto il MIDI suona il preset scelto sul display, come prima; CC 0 e CC 32 seguono le stesse regole per scegliere le varianti. Il display mostra il preset del canale 1. Il mezzo pedale vale per il canale 1; sugli altri canali il CC 64 è acceso o spento.

### File Yamaha Motif e Montage

LOAD apre anche i file dei sintetizzatori Yamaha con campioni utente:

| Strumento | File |
|---|---|
| Motif XS | `.X0A` `.X0W` |
| Motif XF | `.X3A` `.X3W` |
| MOXF | `.X6A` `.X6W` |
| Montage | `.X7U` `.X7L` `.X7A` |
| MODX, MODX+ | `.X8U` `.X8L` `.X8A` |
| Montage M, MODX M | `.Y2U` `.Y2L` |

Ogni waveform utente del file diventa un preset con il suo nome (banco 0 programmi 0-127, poi banco 1 e così via) e ogni keybank una zona: tasti, velocity, nota base, intonazione, livello, pan, punto d'inizio e loop. Il display mostra per qualche secondo quante waveform sono state lette e cosa è stato saltato.

**SAVE** scrive il suono del banco attivo come SoundFont 2 (`.sf2`), da usare in qualsiasi altro player o campionatore. Funziona per i file Yamaha e per gli SFZ; il pulsante rifà la conversione dal file originale, quindi il file originale deve essere ancora al suo posto.

Cosa non arriva, perché non sta nel file o non è un campione:

- le waveform di fabbrica: una Performance che usa i suoni interni dello strumento non ha campioni nel file, e se il file contiene solo quelle il display scrive `NO USER WAVEFORMS`
- le parti FM-X
- i parametri delle Performance (filtri, inviluppi, effetti, Motion Control, livelli delle parti): si convertono le waveform, non le Performance
- le waveform cifrate (contenuti protetti di Yamaha e dei partner, anche quelli gratuiti come Character Pianos) e quelle compresse: vengono saltate e contate sul display; se il file ha solo quelle il display scrive `PROTECTED YAMAHA CONTENT`
- i loop all'indietro diventano loop in avanti, perché il SoundFont 2 non ha il loop all'indietro

Le waveform non hanno un inviluppo proprio: Campione dà a ogni zona un rilascio di 0,2 s per evitare il click al rilascio del tasto. Con USE SF2 ENV spento valgono le manopole dell'AMP ENVELOPE, come per gli altri formati.

La lettura dei file Yamaha è scritta da zero per Campione. Il formato è documentato da Chris Webb ([github.com/arachsys/montage](https://github.com/arachsys/montage), `docs/files/structure.md`); la disposizione dei keybank per ogni modello e le conversioni di livello e intonazione vengono dal lettore YSFC di ConvertWithMoss di Jürgen Moßgraber ([github.com/git-moss/ConvertWithMoss](https://github.com/git-moss/ConvertWithMoss), LGPL v3). La prova automatica usa file Yamaha sintetici creati da `tools/make_test_ysfc.py`, non file salvati da uno strumento vero: la prima prova su un file reale va fatta a mano.

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
- Quattro file grandi occupano molta memoria: TinySoundFont tiene i campioni in RAM come numeri a 32 bit, circa il doppio dei campioni a 16 bit del file (per `.sf3` e `.sfz` compressi anche di più, perché vengono decompressi al caricamento).
- 256 voci per banco. Prima di ogni nota ne restano sempre almeno 24 libere: le più vecchie si chiudono con una dissolvenza di 30 ms, anche col pedale premuto. Un tasto colpito di nuovo chiude in 80 ms il suono precedente dello stesso tasto.

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

`make_test_sf2.py` crea un SoundFont con suoni sintetici, `make_test_ysfc.py` tre file Yamaha di prova (Montage, Motif XF, MOXF), `make_test_gm.py` un SoundFont multibanco GM/XG/GS; la prova controlla intonazione, trasposizione, 432 Hz, sustain da pannello e da CC 64, inviluppo sostituito e ripristinato, curva di velocity, pedale una corda, Program Change, PANIC, 88 tasti col pedale premuto, una raffica di note senza click e i quattro banchi (cambio di banco con note in corso, pedale a tutti i banchi, banco vuoto) la lettura dei file Yamaha con il salvataggio in `.sf2` e il modo GM/XG (programmi per canale, varianti XG e GS, batteria sul canale 10). Gira anche su GitHub a ogni commit.

## Licenze

- Codice di Campione: GNU AGPL v3 (`LICENSE.md`), come gli altri plugin MBStudio91 basati su JUCE.
- TinySoundFont di Bernhard Schelling, licenza MIT (`third_party/TinySoundFont/LICENSE`), file `tsf.h` dal commit 790a219.
- stb_vorbis 1.25 di Sean Barrett, pubblico dominio o MIT a scelta (licenza in fondo a `third_party/stb_vorbis/stb_vorbis.c`), dal commit 6b5799b di github.com/nothings/stb: decodifica i campioni dei file SF3.
- Font Bebas Neue e Antonio: SIL Open Font License 1.1 (`MBStudioUI/fonts`).
- VST è un marchio registrato di Steinberg Media Technologies GmbH.
