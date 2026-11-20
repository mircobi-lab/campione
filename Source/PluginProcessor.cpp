#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace campione;

CampioneProcessor::CampioneProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "CAMPIONE", createLayout()),
      midiMap (*this, "Campione")
{
    auto raw = [this] (const char* pid) { return state.getRawParameterValue (pid); };
    tone = raw (id::tone); tune = raw (id::tune); transpose = raw (id::transpose); a4ref = raw (id::a4ref); velCurve = raw (id::velCurve);
    sf2Env = raw (id::sf2Env); attack = raw (id::attack); decay = raw (id::decay); sustain = raw (id::sustain); release = raw (id::release);
    pedal = raw (id::pedal); soft = raw (id::soft); softAmt = raw (id::softAmt);
    roomOn = raw (id::roomOn); roomSize = raw (id::roomSize); roomDamp = raw (id::roomDamp); roomMix = raw (id::roomMix); volume = raw (id::volume);

    // banchi per i profili dei controller: A suono, B inviluppo, C pedali e stanza, D uscita
    midiMap.setBanks ({ juce::StringArray { id::tone, id::tune, id::transpose, id::velCurve, id::softAmt, id::roomMix, id::roomSize, id::volume },
                        juce::StringArray { id::attack, id::decay, id::sustain, id::release, id::sf2Env, id::a4ref, id::roomDamp, id::roomOn },
                        juce::StringArray { id::pedal, id::soft, id::softAmt, id::roomOn, id::roomSize, id::roomDamp, id::roomMix, id::volume },
                        juce::StringArray { id::volume, id::roomMix, id::tone, id::tune, id::attack, id::release, id::pedal, id::soft } });
}

bool CampioneProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void CampioneProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    engine.prepare (sampleRate, juce::jmax (samplesPerBlock, 32));
    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (samplesPerBlock, 32), 1 };
    for (int c = 0; c < 2; ++c)
    {
        lowShelf[c].coefficients = new juce::dsp::IIR::Coefficients<float> (1, 0, 0, 1, 0, 0);
        highShelf[c].coefficients = new juce::dsp::IIR::Coefficients<float> (1, 0, 0, 1, 0, 0);
        lowShelf[c].prepare (spec);
        highShelf[c].prepare (spec);
    }
    appliedTone = 1.0e9f;
    reverb.setSampleRate (sampleRate);
    reverb.reset();
    gain.reset (sampleRate, .05);
    wet.reset (sampleRate, .05);
    gain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (volume->load(), -60.f));
    wet.setCurrentAndTargetValue (roomOn->load() > .5f ? roomMix->load() : 0.f);
    dry.setSize (2, juce::jmax (samplesPerBlock, 32));
    keyboardState.reset();
}

void CampioneProcessor::updateTone (float t)
{
    // inclinazione: scaffale basso a 250 Hz e alto a 2,5 kHz in senso opposto, fino a 9 dB
    const float dbAmount = 9.f * t;
    const auto lo = juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf (sr, 250.f, .707f, juce::Decibels::decibelsToGain (-dbAmount));
    const auto hi = juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (sr, 2500.f, .707f, juce::Decibels::decibelsToGain (dbAmount));
    for (int c = 0; c < 2; ++c)
    {
        *lowShelf[c].coefficients = lo;
        *highShelf[c].coefficients = hi;
    }
    appliedTone = t;
}

void CampioneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    keyboardState.processNextMidiBuffer (midi, 0, n, true);
    midiMap.process (midi);

    EngineSettings s;
    s.tuningSemis = tune->load() / 100.f + a4Offset ((int) a4ref->load());
    s.transpose = juce::roundToInt (transpose->load());
    s.velCurve = (int) velCurve->load();
    s.pedalButton = pedal->load() > .5f;
    s.softButton = soft->load() > .5f;
    s.softAmount = softAmt->load();
    s.sf2Envelope = sf2Env->load() > .5f;
    s.attack = attack->load(); s.decay = decay->load(); s.sustain = sustain->load(); s.release = release->load();
    engine.render (buffer, midi, s);

    const int chans = juce::jmin (2, buffer.getNumChannels());

    // tono
    if (const float t = tone->load(); ! juce::exactlyEqual (t, appliedTone)) updateTone (t);
    if (std::abs (appliedTone) > .001f)
        for (int c = 0; c < chans; ++c)
        {
            auto* d = buffer.getWritePointer (c);
            for (int i = 0; i < n; ++i) d[i] = highShelf[c].processSample (lowShelf[c].processSample (d[i]));
        }

    // stanza: riverbero solo bagnato, miscelato con un livello che scorre senza scatti
    juce::Reverb::Parameters rp;
    rp.roomSize = .3f + .68f * roomSize->load();
    rp.damping = roomDamp->load();
    rp.wetLevel = 1.f; rp.dryLevel = 0.f; rp.width = 1.f; rp.freezeMode = 0.f;
    reverb.setParameters (rp);
    wet.setTargetValue (roomOn->load() > .5f ? roomMix->load() : 0.f);
    if (wet.isSmoothing() || wet.getTargetValue() > 0.f)
    {
        reverbCleared = false;
        // a pezzi grandi quanto il buffer preparato, se l'host manda blocchi più lunghi del previsto
        for (int start = 0; start < n;)
        {
            const int len = juce::jmin (n - start, dry.getNumSamples());
            for (int c = 0; c < chans; ++c) dry.copyFrom (c, 0, buffer, c, start, len);
            if (chans == 2) reverb.processStereo (buffer.getWritePointer (0, start), buffer.getWritePointer (1, start), len);
            else reverb.processMono (buffer.getWritePointer (0, start), len);
            for (int i = 0; i < len; ++i)
            {
                const float w = wet.getNextValue(), d = 1.f - .5f * w;
                for (int c = 0; c < chans; ++c)
                {
                    auto* o = buffer.getWritePointer (c, start);
                    o[i] = d * dry.getSample (c, i) + w * o[i];
                }
            }
            start += len;
        }
    }
    else if (! reverbCleared)
    {
        reverb.reset();      // una volta sola: la coda non resta nei buffer quando si riaccende
        reverbCleared = true;
    }

    // volume e livello per il LED meter
    gain.setTargetValue (volume->load() <= -59.9f ? 0.f : juce::Decibels::decibelsToGain (volume->load()));
    float peak = 0.f;
    for (int i = 0; i < n; ++i)
    {
        const float g = gain.getNextValue();
        for (int c = 0; c < chans; ++c)
        {
            auto* o = buffer.getWritePointer (c);
            o[i] *= g;
            peak = juce::jmax (peak, std::abs (o[i]));
        }
    }
    level.store (juce::jmax (peak, level.load() * .8f));
}

// ------------------------------------------------------------------------------------------------ SoundFont
bool CampioneProcessor::loadSoundFont (const juce::File& f)
{
    juce::String err;
    const bool ok = engine.loadFile (f, err);
    error = ok ? juce::String() : err;
    if (onSoundFontChanged) onSoundFontChanged();
    return ok;
}

void CampioneProcessor::selectPreset (int index)
{
    engine.selectPreset (index);
}

void CampioneProcessor::stepPreset (int delta)
{
    const int count = (int) engine.presets().size();
    if (count == 0) return;
    const int cur = juce::jmax (0, engine.currentPreset());
    engine.selectPreset ((cur + delta + count) % count);
}

// ------------------------------------------------------------------------------------------------ stato
void CampioneProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = state.copyState();
    st.setProperty ("soundfont", engine.currentFile().getFullPathName(), nullptr);
    st.setProperty ("preset", engine.currentPreset(), nullptr);
    st.removeChild (st.getChildWithName ("MIDIMAP"), nullptr);
    st.appendChild (midiMap.toValueTree(), nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}

void CampioneProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (! xml || ! xml->hasTagName (state.state.getType())) return;
    auto st = juce::ValueTree::fromXml (*xml);
    if (auto mm = st.getChildWithName ("MIDIMAP"); mm.isValid())
    {
        midiMap.fromValueTree (mm);
        st.removeChild (mm, nullptr);
    }
    const juce::String path = st.getProperty ("soundfont", {}).toString();
    const int preset = (int) st.getProperty ("preset", -1);
    state.replaceState (st);
    if (path.isNotEmpty() && juce::File::isAbsolutePath (path))
    {
        const juce::File f (path);
        if (f != engine.currentFile())
        {
            if (! loadSoundFont (f)) error = "MISSING: " + f.getFileName().toUpperCase();
        }
        if (preset >= 0) engine.selectPreset (preset);
    }
    if (onSoundFontChanged) onSoundFontChanged();
}

juce::AudioProcessorEditor* CampioneProcessor::createEditor() { return new CampioneEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CampioneProcessor(); }
