#include "PluginProcessor.h"
#include "SfzToSf2.h"
#include "YamahaToSf2.h"
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
    roomOn = raw (id::roomOn); roomSize = raw (id::roomSize); roomDamp = raw (id::roomDamp); roomMix = raw (id::roomMix); volume = raw (id::volume); bankParam = raw (id::bank); multiParam = raw (id::multi);

    // banchi per i profili dei controller: A suono, B inviluppo, C pedali e stanza, D uscita
    midiMap.setBanks ({ juce::StringArray { id::tone, id::tune, id::transpose, id::velCurve, id::softAmt, id::roomMix, id::roomSize, id::volume },
                        juce::StringArray { id::attack, id::decay, id::sustain, id::release, id::sf2Env, id::a4ref, id::roomDamp, id::roomOn },
                        juce::StringArray { id::pedal, id::soft, id::softAmt, id::roomOn, id::roomSize, id::roomDamp, id::roomMix, id::volume },
                        juce::StringArray { id::bank, id::volume, id::roomMix, id::tone, id::tune, id::release, id::pedal, id::soft } });
}

bool CampioneProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void CampioneProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    banks.prepare (sampleRate, juce::jmax (samplesPerBlock, 32));
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
    s.multi = multiParam->load() > .5f;
    banks.render (buffer, midi, activeBank(), s);

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

// ------------------------------------------------------------------------------------------------ SoundFont e banchi
bool CampioneProcessor::loadSoundFont (const juce::File& f, int bank)
{
    const int b = resolve (bank);
    juce::String err;
    const bool ok = banks[b].loadFile (f, err);
    errors[(size_t) b] = ok ? juce::String() : err;
    if (onSoundFontChanged) onSoundFontChanged();
    return ok;
}

bool CampioneProcessor::canSaveSf2() const
{
    const auto& e = banks[activeBank()];
    const auto f = e.currentFile();
    return e.isLoaded() && (f.hasFileExtension ("sfz") || campione::ysfc::isYamahaFile (f));
}

juce::String CampioneProcessor::saveSf2 (const juce::File& destination)
{
    const auto src = banks[activeBank()].currentFile();
    if (! src.existsAsFile()) return "SOURCE FILE NOT FOUND";
    // si rifà la conversione dal file originale: niente copia in più tenuta in memoria
    juce::MemoryBlock sf2;
    if (src.hasFileExtension ("sfz"))
    {
        auto r = campione::sfz::convert (src);
        if (r.error.isNotEmpty()) return r.error;
        sf2 = std::move (r.sf2);
    }
    else if (campione::ysfc::isYamahaFile (src))
    {
        auto r = campione::ysfc::convert (src);
        if (r.error.isNotEmpty()) return r.error;
        sf2 = std::move (r.sf2);
    }
    else return "NOTHING TO CONVERT";
    if (! destination.replaceWithData (sf2.getData(), sf2.getSize())) return "CANNOT WRITE " + destination.getFileName().toUpperCase();
    return {};
}

void CampioneProcessor::selectPreset (int index)
{
    activeEngine().selectPreset (index);
}

void CampioneProcessor::stepPreset (int delta)
{
    auto& e = activeEngine();
    const int count = (int) e.presets().size();
    if (count == 0) return;
    const int cur = juce::jmax (0, e.currentPreset());
    e.selectPreset ((cur + delta + count) % count);
}

void CampioneProcessor::setActiveBank (int b)
{
    if (auto* p = state.getParameter (id::bank))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jlimit (0, Banks::count - 1, b)));
}

// ------------------------------------------------------------------------------------------------ stato
void CampioneProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = state.copyState();
    for (int b = 0; b < Banks::count; ++b)
    {
        st.setProperty ("soundfont" + juce::String (b), banks[b].currentFile().getFullPathName(), nullptr);
        st.setProperty ("preset" + juce::String (b), banks[b].currentPreset(), nullptr);
    }
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
    // progetti della versione con un solo SoundFont: "soundfont" e "preset" vanno nel banco A
    if (st.hasProperty ("soundfont") && ! st.hasProperty ("soundfont0"))
    {
        st.setProperty ("soundfont0", st.getProperty ("soundfont"), nullptr);
        st.setProperty ("preset0", st.getProperty ("preset", -1), nullptr);
    }
    std::array<juce::String, Banks::count> paths;
    std::array<int, Banks::count> presets {};
    for (int b = 0; b < Banks::count; ++b)
    {
        paths[(size_t) b] = st.getProperty ("soundfont" + juce::String (b), {}).toString();
        presets[(size_t) b] = (int) st.getProperty ("preset" + juce::String (b), -1);
    }
    state.replaceState (st);
    for (int b = 0; b < Banks::count; ++b)
    {
        const auto& path = paths[(size_t) b];
        if (path.isEmpty() || ! juce::File::isAbsolutePath (path))
        {
            if (banks[b].isLoaded()) banks[b].unload();
            errors[(size_t) b] = {};
            continue;
        }
        const juce::File f (path);
        if (f != banks[b].currentFile() && ! loadSoundFont (f, b))
            errors[(size_t) b] = "MISSING: " + f.getFileName().toUpperCase();
        if (presets[(size_t) b] >= 0) banks[b].selectPreset (presets[(size_t) b]);
    }
    if (onSoundFontChanged) onSoundFontChanged();
}

juce::AudioProcessorEditor* CampioneProcessor::createEditor() { return new CampioneEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CampioneProcessor(); }
