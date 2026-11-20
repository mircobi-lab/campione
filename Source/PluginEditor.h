#pragma once
#include "PluginProcessor.h"
#include "MBStudioLookAndFeel.h"
#include <juce_audio_utils/juce_audio_utils.h>

// Pannello frontale di Campione: display del SoundFont, sezioni incise con titolo-chip, tastiera di 88 tasti.
// Il layout segue il design system "Campione" (componente CampionePanel).
class CampionePanel : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit CampionePanel (CampioneProcessor&);
    ~CampionePanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    static constexpr int baseW = 1240, baseH = 514;

private:
    enum class Kind { knob, combo, toggle, fader };
    struct Ctl
    {
        Kind kind = Kind::knob;
        juce::String pid, label;
        juce::Slider slider;
        juce::ComboBox combo;
        juce::TextButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sa;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> ca;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> ba;
        juce::Rectangle<int> cell;          // area con etichetta e valore
        juce::Component& comp() { return kind == Kind::combo ? (juce::Component&) combo : kind == Kind::toggle ? (juce::Component&) button : (juce::Component&) slider; }
    };
    struct Section { juce::String title, scope; juce::Rectangle<int> area; };

    CampioneProcessor& proc;
    std::vector<std::unique_ptr<Ctl>> ctls;
    std::vector<Section> sections;
    juce::TextButton loadBtn { "LOAD SF2" }, prevBtn { "<" }, nextBtn { ">" }, panicBtn { "PANIC" };
    mbstudio::ui::LedMeter meter { 14 };
    juce::MidiKeyboardComponent keyboard;
    mbstudio::midi::MidiControls midiButtons;
    mbstudio::midi::MidiLearnOverlay midiOverlay;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> displayArea, lcdArea;
    // display LCD della sezione PIANO: nome del SoundFont, 16 caratteri, scorre se è più lungo
    static constexpr int lcdChars = 16;
    juce::String lcdText;
    int lcdOffset = 0, lcdTicks = 0;
    void updateLcd();
    void paintLcd (juce::Graphics&);
    juce::String touched, touchedValue;
    juce::uint32 touchedMs = 0;
    int panicFlash = 0;
    bool dragOver = false;

    Ctl& add (Kind, const char* pid, const juce::String& label);
    Ctl* find (const char* pid);
    void chooseFile();
    void showPresetMenu();
    void timerCallback() override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
};

class CampioneEditor : public juce::AudioProcessorEditor
{
public:
    explicit CampioneEditor (CampioneProcessor&);
    ~CampioneEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    mbstudio::ui::HardwareLookAndFeel lnf;
    CampioneProcessor& proc;
    CampionePanel panel;
};
