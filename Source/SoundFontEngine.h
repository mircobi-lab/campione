#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <vector>

struct tsf;

namespace campione
{
// Valori letti dai parametri all'inizio di ogni blocco audio.
struct EngineSettings
{
    float tuningSemis = 0;      // fine tune + diapason
    int transpose = 0;
    int velCurve = 0;           // 0 normal, 1 soft, 2 hard, 3 fixed
    bool pedalButton = false, softButton = false;
    float softAmount = .4f;
    bool sf2Envelope = true;
    float attack = .002f, decay = 6.f, sustain = 0.f, release = .6f;
    bool multi = false;         // GM/XG: 16 canali MIDI, ognuno col suo programma, canale 10 batteria
};

/*  Scelta del preset da banco MIDI (CC 0 MSB, CC 32 LSB) e programma, con le convenzioni dei SoundFont GM, GS e XG:
    - batteria (banco SF2 128): MSB 127 (XG), 126 (XG SFX kit), 120 (GM2), oppure canale 10 con MSB 0
    - XG: MSB 0 e LSB = variante -> banco SF2 uguale alla LSB
    - GS: MSB = variante -> banco SF2 uguale alla MSB; GM2: MSB 121 e LSB = variante
    - se la variante non c'è nel file: lo stesso programma nel banco 0, come fanno i moduli GM
    Restituisce l'indice del preset o -1; 'drum' dice se è una batteria.                                  */
int findPreset (const tsf*, int msb, int lsb, int program, bool drumChannel, bool& drum);

/*  Motore SoundFont 2 basato su TinySoundFont (MIT, third_party/TinySoundFont).
    - il file si carica sul thread dei messaggi (loadFile) e passa al thread audio sotto un lock;
      il thread audio usa un try-lock: durante il cambio file esce in silenzio, senza mai aspettare.
    - 256 voci allocate al caricamento: nessuna allocazione nel thread audio. Prima di ogni nota restano almeno
      24 voci libere: le più vecchie si chiudono in 30 ms invece di essere troncate (niente click, niente note perse
      col pedale premuto); un tasto colpito di nuovo chiude in 80 ms il suono precedente dello stesso tasto.
    - un solo canale MIDI interno: tutto il MIDI in ingresso suona il preset scelto.               */
class SoundFontEngine
{
public:
    struct PresetInfo { juce::String name; int bank = 0, number = 0; };
    static constexpr int maxVoices = 256;
    static constexpr int midiChannels = 16, drumChannel = 9;   // canale 10 (da 0) per la batteria GM

    SoundFontEngine();
    ~SoundFontEngine();

    // ---- thread dei messaggi
    bool loadFile (const juce::File&, juce::String& error);
    void unload();
    std::vector<PresetInfo> presets() const;
    juce::File currentFile() const;
    void selectPreset (int index) noexcept { pendingPreset.store (index); }

    // ---- thread audio
    void prepare (double sampleRate, int maxBlockSize);
    void render (juce::AudioBuffer<float>&, const juce::MidiBuffer&, const EngineSettings&) noexcept;
    void requestPanic() noexcept { panicRequested = true; }

    // ---- letture per il pannello (qualsiasi thread)
    int currentPreset() const noexcept { return presetIndex.load(); }
    int activeVoices() const noexcept { return voices.load(); }
    int lastNote() const noexcept { return lastKey.load(); }
    bool pedalDown() const noexcept { return pedalState.load(); }
    float pedalLevel() const noexcept { return pedalAmount.load(); }   // 0 su, 0,5 mezzo pedale, 1 giù
    juce::StringArray lastLoadNotes() const { const juce::ScopedLock sl (fontLock); return loadNotes; }   // avvisi dell'ultimo SFZ
    static constexpr int halfPedalLow = 24, halfPedalHigh = 100;
    bool softDown() const noexcept { return softState.load(); }
    bool isLoaded() const noexcept { return loaded.load(); }
    int voicesOnKey (int key) const;                     // solo per le prove, prende il lock

private:
    mutable juce::CriticalSection fontLock;        // protegge font, file, elenco preset, inviluppi
    tsf* font = nullptr;
    juce::File file;
    std::vector<PresetInfo> presetList;
    std::vector<float> originalEnv;                 // inviluppo d'ampiezza originale, 8 valori per regione (TsfAccess.h)

    double sampleRate = 44100.0;
    std::vector<float> scratch;                     // stereo interlacciato

    std::atomic<int> pendingPreset { -1 }, presetIndex { -1 }, voices { 0 }, lastKey { -1 };
    std::atomic<bool> panicRequested { false }, pedalState { false }, softState { false }, loaded { false };

    // stato del thread audio
    int pedalValue = 0;             // ultimo CC 64 (0-127)
    bool ccSoft = false, appliedPedal = false;
    std::atomic<float> pedalAmount { 0.f };
    juce::StringArray loadNotes;
    std::array<int, midiChannels> bankMsb {}, bankLsb {};
    std::array<bool, midiChannels> drumCh {};
    int appliedMulti = -1;          // -1: canali da sistemare (file nuovo o prepare)
    void resetChannels (bool multi) noexcept;
    float appliedTuning = 1.0e9f;
    struct EnvKey { bool sf2 = true; float a = -1, d = -1, s = -1, r = -1; bool operator!= (const EnvKey& o) const { return sf2 != o.sf2 || ! juce::exactlyEqual (a, o.a) || ! juce::exactlyEqual (d, o.d) || ! juce::exactlyEqual (s, o.s) || ! juce::exactlyEqual (r, o.r); } };
    EnvKey appliedEnv;
    bool envDirty = true;

    void applyPresetIndex (int index) noexcept;
    void applyEnvelope (const EngineSettings&) noexcept;
    void handleMidi (const juce::MidiMessage&, const EngineSettings&) noexcept;
    float shapeVelocity (float v, const EngineSettings&) const noexcept;
    void renderSpan (float* l, float* r, int n) noexcept;
    void applyPedal (const EngineSettings&) noexcept;
};
}
