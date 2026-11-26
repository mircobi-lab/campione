#pragma once
#include "MBStudioMidi.h"
#include "Params.h"
#include "Banks.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

class CampioneProcessor : public juce::AudioProcessor
{
public:
    CampioneProcessor();
    ~CampioneProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- thread dei messaggi (sempre sul banco attivo, se non indicato)
    bool loadSoundFont (const juce::File&, int bank = -1);   // false: messaggio in lastError()
    juce::String lastError (int bank = -1) const { return errors[(size_t) resolve (bank)]; }
    // SAVE SF2: il banco attivo, se viene da un file convertito (SFZ o Yamaha Motif/Montage), salvato come .sf2
    bool canSaveSf2() const;
    juce::String saveSf2 (const juce::File& destination);   // vuoto se è andato bene, altrimenti l'errore
    // SAVE in un thread a parte: 'done' arriva sul thread dei messaggi con l'errore (vuoto se è andato bene)
    void saveSf2Async (const juce::File& destination, std::function<void (juce::String)> done);

    // caricamento in un thread a parte: il pannello resta libero e il suono di prima suona finché il nuovo è pronto.
    // Lo stato del progetto (setStateInformation) carica invece subito, perché il DAW lo aspetta.
    void loadSoundFontAsync (const juce::File&, int bank = -1);
    bool isLoading (int bank = -1) const noexcept { return loadingCount[(size_t) resolve (bank)].load() > 0; }
    juce::String loadingName (int bank = -1) const { const juce::ScopedLock sl (loadingLock); return loadingNames[(size_t) resolve (bank)]; }
    bool isSaving() const noexcept { return saving.load(); }
    void selectPreset (int index);
    void stepPreset (int delta);
    bool multiMode() const noexcept { return multiParam->load() > .5f; }
    int activeBank() const noexcept { return juce::jlimit (0, campione::Banks::count - 1, (int) bankParam->load()); }
    void setActiveBank (int b);
    campione::SoundFontEngine& activeEngine() noexcept { return banks[activeBank()]; }

    float outputLevel() const noexcept { return level.load(); }
    void requestPanic() noexcept { banks.requestPanic(); }    // MIDI PANIC su tutti i banchi, eseguito all'inizio del blocco audio

    juce::AudioProcessorValueTreeState state;
    juce::MidiKeyboardState keyboardState;
    mbstudio::midi::MidiMapper midiMap;               // MIDI Learn e profili controller
    campione::Banks banks;
    std::function<void()> onSoundFontChanged;         // per il pannello (thread dei messaggi)

private:
    std::atomic<float>* tone = nullptr, *tune = nullptr, *transpose = nullptr, *a4ref = nullptr, *velCurve = nullptr,
                       *sf2Env = nullptr, *attack = nullptr, *decay = nullptr, *sustain = nullptr, *release = nullptr,
                       *pedal = nullptr, *soft = nullptr, *softAmt = nullptr,
                       *roomOn = nullptr, *roomSize = nullptr, *roomDamp = nullptr, *roomMix = nullptr, *volume = nullptr, *bankParam = nullptr, *multiParam = nullptr;

    using Filter = juce::dsp::IIR::Filter<float>;
    Filter lowShelf[2], highShelf[2];
    float appliedTone = 1.0e9f;
    double sr = 44100.0;
    juce::Reverb reverb;
    bool reverbCleared = false;
    juce::SmoothedValue<float> gain, wet;
    juce::AudioBuffer<float> dry;
    std::atomic<float> level { 0.f };
    std::array<juce::String, campione::Banks::count> errors;
    static juce::String convertAndWrite (const juce::File& source, const juce::File& destination);
    juce::ThreadPool worker { juce::ThreadPoolOptions{}.withNumberOfThreads (1).withThreadName ("Campione loader") };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    std::array<std::atomic<int>, campione::Banks::count> loadingCount {};
    mutable juce::CriticalSection loadingLock;
    std::array<juce::String, campione::Banks::count> loadingNames;
    std::atomic<bool> saving { false };
    int resolve (int bank) const noexcept { return bank >= 0 && bank < campione::Banks::count ? bank : activeBank(); }

    void updateTone (float t);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CampioneProcessor)
};
