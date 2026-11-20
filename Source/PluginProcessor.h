#pragma once
#include "MBStudioMidi.h"
#include "Params.h"
#include "SoundFontEngine.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

class CampioneProcessor : public juce::AudioProcessor
{
public:
    CampioneProcessor();
    ~CampioneProcessor() override = default;

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

    // ---- thread dei messaggi
    bool loadSoundFont (const juce::File&);           // false: messaggio in lastError()
    juce::String lastError() const { return error; }
    void selectPreset (int index);
    void stepPreset (int delta);

    float outputLevel() const noexcept { return level.load(); }
    void requestPanic() noexcept { engine.requestPanic(); }   // MIDI PANIC, eseguito all'inizio del blocco audio

    juce::AudioProcessorValueTreeState state;
    juce::MidiKeyboardState keyboardState;
    mbstudio::midi::MidiMapper midiMap;               // MIDI Learn e profili controller
    campione::SoundFontEngine engine;
    std::function<void()> onSoundFontChanged;         // per il pannello (thread dei messaggi)

private:
    std::atomic<float>* tone = nullptr, *tune = nullptr, *transpose = nullptr, *a4ref = nullptr, *velCurve = nullptr,
                       *sf2Env = nullptr, *attack = nullptr, *decay = nullptr, *sustain = nullptr, *release = nullptr,
                       *pedal = nullptr, *soft = nullptr, *softAmt = nullptr,
                       *roomOn = nullptr, *roomSize = nullptr, *roomDamp = nullptr, *roomMix = nullptr, *volume = nullptr;

    using Filter = juce::dsp::IIR::Filter<float>;
    Filter lowShelf[2], highShelf[2];
    float appliedTone = 1.0e9f;
    double sr = 44100.0;
    juce::Reverb reverb;
    bool reverbCleared = false;
    juce::SmoothedValue<float> gain, wet;
    juce::AudioBuffer<float> dry;
    std::atomic<float> level { 0.f };
    juce::String error;

    void updateTone (float t);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CampioneProcessor)
};
