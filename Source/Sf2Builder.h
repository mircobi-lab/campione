#pragma once
#include <juce_core/juce_core.h>
#include <cstdint>
#include <memory>
#include <vector>

/*  Scrittura di un SoundFont 2 in memoria, usata dalle conversioni SFZ e Yamaha.
    Ogni campione è mono a 16 bit; i campioni stereo sono due campioni collegati (linkStereo).
    Il loop scritto nell'intestazione del campione copre tutto il campione: inizio, fine e loop veri
    stanno nei generatori della zona (offset rispetto all'inizio e alla fine del campione).          */
namespace campione::sf2
{
struct Gen { int op; int amount; };   // amount: per i range (lo | hi << 8)

// generatori SF2 usati dalle conversioni
namespace op
{
    enum : int { startOffset = 0, endOffset = 1, loopStartOffset = 2, loopEndOffset = 3, startCoarse = 4, endCoarse = 12,
                 pan = 17, delayVol = 33, attackVol = 34, holdVol = 35, decayVol = 36, sustainVol = 37, releaseVol = 38,
                 instrument = 41, keyRange = 43, velRange = 44, loopStartCoarse = 45, attenuation = 48, loopEndCoarse = 50,
                 coarseTune = 51, fineTune = 52, sampleID = 53, sampleModes = 54, scaleTuning = 56, exclusiveClass = 57,
                 rootKey = 58 };
}

// tetto dei valori a 16 bit (campioni per canali) in un SoundFont convertito: 1,8 GB di dati,
// circa 3,6 GB in memoria quando il motore li porta in virgola mobile. Basta per un pianoforte
// completo come il Maestro Concert Grand (467 milioni di valori).
inline constexpr double maxSampleValues = 900.0e6;

// secondi -> timecents dell'SF2
int timecents (double seconds);

// offset di un campione in due generatori (fine e grossolano, passi di 32768): op fine e op grossolano
void addOffset (std::vector<Gen>& gens, int fineOp, int coarseOp, int offset);

class Builder
{
public:
    // aggiunge un campione mono; restituisce il suo indice (per il generatore sampleID)
    int addSample (const juce::String& name, const int16_t* data, int frames, int rate);
    void linkStereo (int left, int right);
    // nuovo strumento: le zone aggiunte dopo vanno a questo strumento
    int addInstrument (const juce::String& name);
    void addZone (std::vector<Gen> gens, int sample);
    void addPreset (const juce::String& name, int bank, int program, int instrument);

    int instrumentCount() const noexcept { return (int) instruments.size(); }
    int zoneCount() const noexcept;
    double sampleBytes() const noexcept { return (double) smpl->getDataSize(); }

    // prepara lo spazio per i campioni (in byte), per non far crescere il buffer a raddoppi
    void reserve (size_t bytes) { smpl->preallocate (bytes); }

    // il file si scrive in un solo blocco: i campioni vengono copiati una volta, poi liberati qui
    juce::MemoryBlock build (const juce::String& bankName);

private:
    struct SampleHeader { juce::String name; juce::uint32 start, end; int rate, link, type; };
    struct Zone { std::vector<Gen> gens; int sample; };
    struct Instrument { juce::String name; std::vector<Zone> zones; };
    struct Preset { juce::String name; int bank, program, instrument; };

    std::unique_ptr<juce::MemoryOutputStream> smpl = std::make_unique<juce::MemoryOutputStream>();
    std::vector<SampleHeader> headers;
    std::vector<Instrument> instruments;
    std::vector<Preset> presets;
};
}
