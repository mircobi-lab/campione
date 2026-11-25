#include "PluginEditor.h"
#include "LcdFont.h"

namespace ui = mbstudio::ui;
namespace C = mbstudio::ui::colours;
using namespace campione;

namespace
{
constexpr int margin = 14, lineY = 112, chipY = 124, cellY = 144, cellH = 88, knobW = 60, step = 64;
constexpr int row2LabelY = 236, row2Y = 252, row2H = 28, footerY = 300, keyboardY = 322;
constexpr int lcdY = chipY, lcdH = 16, lcdW = 136, pianoShift = 0;   // display LCD a matrice, alto come i titoli-chip
constexpr float lcdPitch = 1.32f;                                    // passo dei punti: caratteri alti 9 px come il testo dei chip
constexpr int pianoCellY = cellY + pianoShift, pianoRow2Y = row2Y + pianoShift;
const juce::Colour lcdGlass (0xff07142e), lcdDotOff (0xff10285a);   // punti accesi: blu LED (colours::ledBlue)

const char* noteName (int n)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[(n % 12 + 12) % 12];
}
}

// ================================================================================================ pannello
CampionePanel::CampionePanel (CampioneProcessor& p)
    : proc (p), keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard),
      midiButtons (p.midiMap), midiOverlay (p.midiMap)
{
    sections = { { "AMP ENVELOPE", {}, { 246, chipY, 280, 160 } },
                 { "PEDAL", {}, { 548, chipY, 190, 160 } },
                 { "ROOM", "GLOBAL", { 760, chipY, 210, 160 } },
                 { "OUTPUT", {}, { 992, chipY, 234, 160 } } };

    // PIANO
    add (Kind::knob, id::tone, "TONE").cell = { margin, pianoCellY, knobW, cellH };
    add (Kind::knob, id::tune, "TUNE").cell = { margin + step, pianoCellY, knobW, cellH };
    add (Kind::knob, id::transpose, "TRANSP").cell = { margin + 2 * step, pianoCellY, knobW, cellH };
    add (Kind::combo, id::a4ref, "A4 REF").cell = { margin, pianoRow2Y, 92, row2H };
    add (Kind::combo, id::velCurve, "VEL CURVE").cell = { margin + 100, pianoRow2Y, 104, row2H };
    // AMP ENVELOPE
    add (Kind::knob, id::attack, "ATTACK").cell = { 246, cellY, knobW, cellH };
    add (Kind::knob, id::decay, "DECAY").cell = { 246 + step, cellY, knobW, cellH };
    add (Kind::knob, id::sustain, "SUSTAIN").cell = { 246 + 2 * step, cellY, knobW, cellH };
    add (Kind::knob, id::release, "RELEASE").cell = { 246 + 3 * step, cellY, knobW, cellH };
    add (Kind::toggle, id::sf2Env, "USE SF2 ENV").cell = { 246, row2Y, 132, row2H };
    // PEDAL
    add (Kind::knob, id::softAmt, "SOFT AMT").cell = { 548, cellY, knobW, cellH };
    add (Kind::toggle, id::pedal, "SUSTAIN").cell = { 548, row2Y, 96, row2H };
    add (Kind::toggle, id::soft, "SOFT").cell = { 650, row2Y, 76, row2H };
    // ROOM
    add (Kind::knob, id::roomSize, "SIZE").cell = { 760, cellY, knobW, cellH };
    add (Kind::knob, id::roomDamp, "DAMP").cell = { 760 + step, cellY, knobW, cellH };
    add (Kind::knob, id::roomMix, "MIX").cell = { 760 + 2 * step, cellY, knobW, cellH };
    add (Kind::toggle, id::roomOn, "ROOM").cell = { 760, row2Y, 84, row2H };
    // OUTPUT
    add (Kind::fader, id::volume, "VOLUME").cell = { 998, cellY, 50, 136 };

    find (id::sf2Env)->button.getProperties().set ("ledColour", "ff3d8bff");
    find (id::roomOn)->button.getProperties().set ("ledColour", "ffffb020");

    loadBtn.setTooltip ("Carica un suono: SoundFont 2 (.sf2), SoundFont compresso (.sf3) o SFZ (.sfz con la sua cartella di campioni). Puoi anche trascinarlo sul pannello.");
    loadBtn.onClick = [this] { chooseFile(); };
    prevBtn.onClick = [this] { proc.stepPreset (-1); };
    nextBtn.onClick = [this] { proc.stepPreset (1); };
    panicBtn.setTooltip ("MIDI PANIC: spegne subito tutte le note e azzera pedali e pitch bend");
    panicBtn.getProperties().set ("led", true);
    panicBtn.getProperties().set ("ledColour", "ffff3b30");
    panicBtn.onClick = [this]
    {
        proc.requestPanic();
        proc.keyboardState.allNotesOff (0);
        panicFlash = 15;
        touched = "PANIC"; touchedValue = "ALL NOTES OFF"; touchedMs = juce::Time::getMillisecondCounter();
    };
    for (auto* b : { &loadBtn, &prevBtn, &nextBtn, &panicBtn }) addAndMakeVisible (*b);
    addAndMakeVisible (meter);

    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xffdcdcd8));
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff0d0d0e));
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour (0xff3a3c40));
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, C::ledBlue.withAlpha (.55f));
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, C::ledBlue.withAlpha (.18f));
    keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::black.withAlpha (.5f));
    keyboard.setColour (juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, C::key);
    keyboard.setColour (juce::MidiKeyboardComponent::upDownButtonArrowColourId, C::print);
    keyboard.setAvailableRange (21, 108);          // 88 tasti, da A0 a C8
    keyboard.setLowestVisibleKey (21);
    keyboard.setScrollButtonsVisible (false);
    keyboard.setOctaveForMiddleC (4);
    keyboard.setWantsKeyboardFocus (false);
    addAndMakeVisible (keyboard);
    addAndMakeVisible (midiButtons);
    addAndMakeVisible (midiOverlay);     // per ultimo: sopra a tutto

    juce::Component::SafePointer<CampionePanel> safeThis (this);
    proc.onSoundFontChanged = [safeThis] { juce::MessageManager::callAsync ([safeThis]
    {
        if (safeThis == nullptr) return;
        // SFZ: avvisi della conversione (regioni saltate, campioni mancanti) per qualche secondo sul display
        if (const auto notes = safeThis->proc.engine.lastLoadNotes(); ! notes.isEmpty() && safeThis->proc.lastError().isEmpty())
        {
            safeThis->touched = "SFZ";
            safeThis->touchedValue = notes[0].toUpperCase();
            safeThis->touchedMs = juce::Time::getMillisecondCounter();
        }
        safeThis->updateLcd();
        safeThis->repaint();
    }); };
    updateLcd();
    setSize (baseW, baseH);
    startTimerHz (30);
}

CampionePanel::~CampionePanel()
{
    stopTimer();
    proc.onSoundFontChanged = nullptr;
    for (auto& c : ctls) { c->sa.reset(); c->ca.reset(); c->ba.reset(); }
}

CampionePanel::Ctl& CampionePanel::add (Kind kind, const char* pid, const juce::String& label)
{
    auto c = std::make_unique<Ctl>();
    c->kind = kind; c->pid = pid; c->label = label;
    auto* cp = c.get();
    auto* param = proc.state.getParameter (pid);
    auto show = [this, cp, param]
    {
        touched = cp->label;
        touchedValue = param != nullptr ? param->getCurrentValueAsText().toUpperCase() : juce::String();
        touchedMs = juce::Time::getMillisecondCounter();
        repaint (displayArea);
        repaint (cp->cell.expanded (6, 4));
    };
    switch (kind)
    {
        case Kind::knob:
        case Kind::fader:
            c->slider.setSliderStyle (kind == Kind::knob ? juce::Slider::RotaryHorizontalVerticalDrag : juce::Slider::LinearVertical);
            c->slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            c->slider.setColour (juce::Slider::trackColourId, juce::String (pid).startsWith ("ROOM") ? C::ledAmber : C::ledGreen);
            c->slider.setColour (juce::Slider::thumbColourId, C::print);
            c->slider.setMouseDragSensitivity (200);
            if (param != nullptr) c->slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
            if (juce::String (pid) == id::tone || juce::String (pid) == id::tune || juce::String (pid) == id::transpose)
                c->slider.getProperties().set ("bipolar", true);
            c->sa = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.state, pid, c->slider);
            c->slider.onValueChange = show;
            break;
        case Kind::combo:
            if (auto* ch = dynamic_cast<juce::AudioParameterChoice*> (param)) c->combo.addItemList (ch->choices, 1);
            c->ca = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.state, pid, c->combo);
            c->combo.onChange = show;
            break;
        case Kind::toggle:
            c->button.setButtonText (label);
            c->button.setClickingTogglesState (true);
            c->ba = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.state, pid, c->button);
            c->button.onClick = show;
            break;
    }
    if (auto* tip = dynamic_cast<juce::SettableTooltipClient*> (&c->comp()); tip != nullptr && param != nullptr)
        tip->setTooltip (param->getName (64));
    mbstudio::midi::tag (c->comp(), pid);
    addAndMakeVisible (c->comp());
    ctls.push_back (std::move (c));
    return *cp;
}

CampionePanel::Ctl* CampionePanel::find (const char* pid)
{
    for (auto& c : ctls) if (c->pid == pid) return c.get();
    return nullptr;
}

// ------------------------------------------------------------------------------------------------ SoundFont
void CampionePanel::chooseFile()
{
    auto start = proc.engine.currentFile();
    chooser = std::make_unique<juce::FileChooser> ("Load SoundFont", start.existsAsFile() ? start.getParentDirectory()
                                                                                        : juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.sf2;*.sf3;*.sfz;*.SF2;*.SF3;*.SFZ");
    juce::Component::SafePointer<CampionePanel> safeThis (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safeThis] (const juce::FileChooser& fc)
    {
        if (safeThis == nullptr) return;
        const auto f = fc.getResult();
        if (f.existsAsFile()) safeThis->proc.loadSoundFont (f);
        safeThis->repaint();
    });
}

bool CampionePanel::isSoundFile (const juce::String& f)
{
    return f.endsWithIgnoreCase (".sf2") || f.endsWithIgnoreCase (".sf3") || f.endsWithIgnoreCase (".sfz");
}

bool CampionePanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (isSoundFile (f)) return true;
    return false;
}

void CampionePanel::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    for (auto& f : files)
        if (isSoundFile (f)) { proc.loadSoundFont (juce::File (f)); break; }
    repaint();
}

void CampionePanel::showPresetMenu()
{
    const auto list = proc.engine.presets();
    if (list.empty()) { chooseFile(); return; }
    // ordine per banco e programma, un sottomenu per banco quando ce n'è più di uno
    std::vector<int> order ((size_t) list.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = (int) i;
    std::sort (order.begin(), order.end(), [&] (int a, int b)
    {
        const auto& x = list[(size_t) a]; const auto& y = list[(size_t) b];
        return x.bank != y.bank ? x.bank < y.bank : x.number < y.number;
    });
    juce::PopupMenu menu, bankMenu;
    const int cur = proc.engine.currentPreset();
    bool multiBank = false;
    for (auto& p : list) if (p.bank != list.front().bank) multiBank = true;
    int bank = -1;
    for (int i : order)
    {
        const auto& p = list[(size_t) i];
        if (multiBank && p.bank != bank)
        {
            if (bank >= 0) menu.addSubMenu ("BANK " + juce::String (bank).paddedLeft ('0', 3), bankMenu);
            bankMenu = {};
            bank = p.bank;
        }
        auto& m = multiBank ? bankMenu : menu;
        m.addItem (i + 1, juce::String (p.number).paddedLeft ('0', 3) + "  " + p.name.toUpperCase(), true, i == cur);
    }
    if (multiBank && bank >= 0) menu.addSubMenu ("BANK " + juce::String (bank).paddedLeft ('0', 3), bankMenu);
    juce::Component::SafePointer<CampionePanel> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (displayArea)), [safeThis] (int r)
    {
        if (safeThis != nullptr && r > 0) safeThis->proc.selectPreset (r - 1);
    });
}

void CampionePanel::mouseDown (const juce::MouseEvent& e)
{
    if (displayArea.contains (e.getPosition()) && ! e.mods.isPopupMenu()) showPresetMenu();
}

// ------------------------------------------------------------------------------------------------ layout
void CampionePanel::resized()
{
    displayArea = { 420, 14, 560, 90 };
    lcdArea = { margin, lcdY, lcdW, lcdH };
    loadBtn.setBounds (994, 16, 108, 30);
    prevBtn.setBounds (1108, 16, 38, 30);
    nextBtn.setBounds (1152, 16, 38, 30);
    midiButtons.setBounds (994, 58, 166, 30);
    panicBtn.setBounds (1166, 58, 60, 30);

    for (auto& c : ctls)
    {
        switch (c->kind)
        {
            case Kind::knob:   c->slider.setBounds (c->cell.getX(), c->cell.getY() + 14, knobW, 54); break;
            case Kind::fader:  c->slider.setBounds (c->cell.getX(), c->cell.getY() - 2, c->cell.getWidth(), 112); break;
            case Kind::combo:  c->combo.setBounds (c->cell); break;
            case Kind::toggle: c->button.setBounds (c->cell); break;
        }
    }
    meter.setBounds (1064, 206, 150, 8);
    keyboard.setBounds (margin, keyboardY, baseW - 2 * margin, baseH - keyboardY - 12);
    keyboard.setKeyWidth ((float) (baseW - 2 * margin) / 52.f);   // 52 tasti bianchi
    midiOverlay.setBounds (getLocalBounds());
}

// ------------------------------------------------------------------------------------------------ disegno
void CampionePanel::paint (juce::Graphics& g)
{
    ui::paintChassis (g, getLocalBounds());

    // marchio e sottotitolo
    ui::paintBrand (g, { 18.f, 16.f, 360.f, 34.f }, "CAMPIONE");
    ui::paintPrinted (g, { 18.f, 56.f, 390.f, 16.f }, "SOUNDFONT PIANO PLAYER  -  SF2 SF3 SFZ", 14.f, juce::Justification::centredLeft, C::printDim);
    ui::paintPrinted (g, { 18.f, 76.f, 390.f, 14.f }, "256 VOICES  -  SUSTAIN + SOFT PEDAL  -  ROOM", 11.f, juce::Justification::centredLeft, C::printDim.withAlpha (.7f));

    // display
    const auto d = displayArea.toFloat();
    ui::paintRecess (g, d);
    {
        const auto in = d.reduced (14.f, 9.f);
        const auto list = proc.engine.presets();
        const int cur = proc.engine.currentPreset();
        const bool loaded = proc.engine.isLoaded() && cur >= 0 && cur < (int) list.size();
        g.setColour (C::print);
        g.setFont (ui::Fonts::display (28.f));
        const juce::String title = loaded ? juce::String (cur + 1).paddedLeft ('0', 2) + "  " + list[(size_t) cur].name.toUpperCase()
                                          : juce::String ("NO SOUNDFONT");
        g.drawText (title, in.withHeight (30.f).withTrimmedRight (110.f), juce::Justification::centredLeft, true);

        g.setFont (ui::Fonts::label (13.f));
        auto line = [&] (juce::Rectangle<float> r, std::initializer_list<std::pair<juce::String, juce::String>> parts)
        {
            float x = r.getX();
            for (auto& [label, value] : parts)
            {
                g.setColour (C::printDim);
                const float lw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label);
                g.drawText (label, juce::Rectangle<float> (x, r.getY(), lw + 2.f, r.getHeight()), juce::Justification::centredLeft, false);
                x += lw + 6.f;
                g.setColour (C::print);
                const float vw = juce::jmin (r.getRight() - x, juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), value) + 2.f);
                g.drawText (value, juce::Rectangle<float> (x, r.getY(), vw, r.getHeight()), juce::Justification::centredLeft, true);
                x += vw + 18.f;
            }
        };
        const auto row2 = juce::Rectangle<float> (in.getX(), in.getY() + 34.f, in.getWidth(), 16.f);
        const auto row3 = juce::Rectangle<float> (in.getX(), in.getY() + 54.f, in.getWidth() - 130.f, 16.f);
        if (proc.lastError().isNotEmpty())
            line (row2, { { "ERROR", proc.lastError().toUpperCase() } });
        else if (loaded)
            line (row2, { { "FILE", proc.engine.currentFile().getFileName().toUpperCase() },
                          { "BANK", juce::String (list[(size_t) cur].bank).paddedLeft ('0', 3) },
                          { "PROG", juce::String (list[(size_t) cur].number).paddedLeft ('0', 3) } });
        else
            line (row2, { { "", "CLICK LOAD OR DROP AN SF2, SF3 OR SFZ FILE HERE" } });

        const bool showTouched = touched.isNotEmpty() && juce::Time::getMillisecondCounter() - touchedMs < 2500;
        if (showTouched)
            line (row3, { { touched, touchedValue } });
        else
        {
            const int last = proc.engine.lastNote();
            line (row3, { { "VOICES", juce::String (proc.engine.activeVoices()) + " / " + juce::String (SoundFontEngine::maxVoices) },
                          { "LAST", last >= 0 ? juce::String (noteName (last)) + juce::String (last / 12 - 1) : juce::String ("--") } });
        }
        // LED di stato a destra della terza riga
        auto ledLabel = [&] (float x, const juce::String& t, juce::Colour c, bool on)
        {
            ui::paintLed (g, { x, row3.getCentreY() }, 3.f, c, on ? 1.f : 0.f);
            g.setColour (C::printDim); g.setFont (ui::Fonts::label (11.f));
            g.drawText (t, juce::Rectangle<float> (x + 7.f, row3.getY(), 60.f, row3.getHeight()), juce::Justification::centredLeft, false);
        };
        {
            // SUSTAIN: LED pieno a pedale giù, a metà luce col mezzo pedale
            const float lv = proc.engine.pedalLevel();
            ui::paintLed (g, { d.getRight() - 128.f, row3.getCentreY() }, 3.f, C::ledGreen, lv);
            g.setColour (C::printDim); g.setFont (ui::Fonts::label (11.f));
            g.drawText (lv > .25f && lv < .75f ? "HALF" : "SUSTAIN", juce::Rectangle<float> (d.getRight() - 121.f, row3.getY(), 60.f, row3.getHeight()), juce::Justification::centredLeft, false);
        }
        ledLabel (d.getRight() - 58.f, "SOFT", C::ledGreen, proc.engine.softDown());
        ui::paintLed (g, { d.getRight() - 14.f, d.getY() + 12.f }, 3.5f, C::ledGreen, juce::jmin (1.f, proc.outputLevel() * 3.f));
        ui::paintPrinted (g, { d.getRight() - 120.f, d.getY() + 8.f, 98.f, 14.f }, loaded ? "CLICK = PRESETS" : "", 11.f,
                          juce::Justification::centredRight, C::printDim.withAlpha (.6f));
        if (dragOver)
        {
            g.setColour (C::ledAmber.withAlpha (.8f));
            g.drawRoundedRectangle (d.reduced (2.f), 3.f, 1.5f);
        }
    }
    ui::paintEngravedLine (g, { (float) margin, (float) lineY }, { (float) baseW - margin, (float) lineY });

    // sezioni: chip, linea incisa, eventuale ambito con LED ambra
    for (auto& s : sections)
    {
        const auto a = s.area.toFloat();
        const float cw = ui::chipWidth (s.title, 16.f);
        ui::paintChip (g, { a.getX(), a.getY(), cw, 16.f }, s.title, 14.f);
        float lineEnd = a.getRight();
        if (s.scope.isNotEmpty())
        {
            g.setFont (ui::Fonts::label (11.f));
            const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), s.scope) + 6.f;
            g.setColour (C::printDim);
            g.drawText (s.scope, juce::Rectangle<float> (a.getRight() - tw, a.getY() + 1.f, tw, 14.f), juce::Justification::centredRight, false);
            ui::paintLed (g, { a.getRight() - tw - 6.f, a.getY() + 8.f }, 2.5f, C::ledAmber, 1.f);
            lineEnd = a.getRight() - tw - 14.f;
        }
        ui::paintEngravedLine (g, { a.getX() + cw + 4.f, a.getY() + 8.f }, { lineEnd, a.getY() + 8.f });
    }

    paintLcd (g);

    // etichette e valori dei controlli
    for (auto& c : ctls)
    {
        const auto cell = c->cell.toFloat();
        if (c->kind == Kind::knob)
        {
            ui::paintPrinted (g, cell.withHeight (14.f).expanded (8.f, 0.f), c->label, 12.f, juce::Justification::centred);
            ui::paintPrinted (g, { cell.getX() - 8.f, cell.getY() + 70.f, cell.getWidth() + 16.f, 14.f },
                              formatValue (c->pid, (float) c->slider.getValue()), 11.f, juce::Justification::centred, C::printDim);
        }
        else if (c->kind == Kind::combo)
        {
            ui::paintPrinted (g, { cell.getX(), cell.getY() - (float) (row2Y - row2LabelY) - 2.f, cell.getWidth() + 20.f, 14.f }, c->label, 12.f);
        }
        else if (c->kind == Kind::fader)
        {
            ui::paintPrinted (g, { cell.getX() - 10.f, cell.getY() + 112.f, cell.getWidth() + 20.f, 14.f }, c->label, 12.f, juce::Justification::centred);
            ui::paintPrinted (g, { cell.getX() - 10.f, cell.getY() + 126.f, cell.getWidth() + 20.f, 14.f },
                              formatValue (c->pid, (float) c->slider.getValue()), 11.f, juce::Justification::centred, C::printDim);
        }
    }
    ui::paintPrinted (g, { 1064.f, 186.f, 150.f, 14.f }, "OUT", 12.f);
    ui::paintPrinted (g, { 384.f, (float) row2Y, 140.f, (float) row2H }, "OFF = KNOBS", 11.f, juce::Justification::centredLeft, C::printDim);

    // piede: note e logo impresso
    ui::paintPrinted (g, { (float) margin, (float) footerY, 760.f, 16.f },
                      "DOUBLE CLICK A KNOB TO RESET   -   CC64 SUSTAIN   -   CC67 SOFT   -   CC120 PANIC   -   AMBER = GLOBAL", 11.f,
                      juce::Justification::centredLeft, C::printDim);
    ui::paintLogo (g, { (float) baseW - 260.f, (float) footerY, 246.f, 16.f }, false, ui::Imprint::debossed, juce::Justification::centredRight);
}

// ------------------------------------------------------------------------------------------------ display LCD
void CampionePanel::updateLcd()
{
    juce::String t;
    if (proc.lastError().isNotEmpty()) t = "ERROR - " + proc.lastError();
    else if (proc.engine.isLoaded()) t = proc.engine.currentFile().getFileNameWithoutExtension();
    else t = "NO SOUNDFONT";
    // il font a matrice ha solo i caratteri ASCII: lettere accentate senza accento, il resto diventa '?'
    static const juce::String from = juce::String::fromUTF8 ("\xc3\xa0\xc3\xa1\xc3\xa8\xc3\xa9\xc3\xac\xc3\xad\xc3\xb2\xc3\xb3\xc3\xb9\xc3\xba\xc3\x80\xc3\x88\xc3\x89\xc3\x8c\xc3\x92\xc3\x99\xc3\xb1\xc3\xa7\xc3\xbc\xc3\xb6\xc3\xa4");
    static const juce::String to = "aaeeiioouuAEEIOUncuoa";
    juce::String clean;
    for (auto ch : t)
    {
        const int k = from.indexOfChar (ch);
        if (k >= 0) ch = to[k];
        clean += juce::String::charToString (ch >= 32 && ch <= 126 ? ch : (juce::juce_wchar) '?');
    }
    if (clean != lcdText) { lcdText = clean; lcdOffset = 0; lcdTicks = 0; repaint (lcdArea); }
}

void CampionePanel::paintLcd (juce::Graphics& g)
{
    // cornice incassata, vetro blu scuro, 16 caratteri a matrice 5x7: punti spenti blu scuro, accesi blu LED
    const auto outer = lcdArea.toFloat();
    ui::paintRecess (g, outer);
    const auto glass = outer.reduced (1.5f);
    g.setColour (lcdGlass);
    g.fillRoundedRectangle (glass, 2.f);

    const juce::String loop = lcdText.length() > lcdChars ? lcdText + "    " : lcdText;
    const float pitch = lcdPitch;
    const float dot = pitch * .8f;
    const float textW = pitch * (float) (lcdChars * 6 - 1), textH = pitch * 6.f + dot;
    const float x0 = glass.getCentreX() - textW * .5f, y0 = glass.getCentreY() - textH * .5f;
    const auto glow = C::ledBlue.withAlpha (.3f);
    for (int i = 0; i < lcdChars; ++i)
    {
        juce::juce_wchar ch = ' ';
        if (loop.length() > lcdChars) ch = loop[(lcdOffset + i) % loop.length()];
        else if (i < loop.length()) ch = loop[i];
        const auto* cols = campione::lcdfont::glyph ((char32_t) ch);
        for (int c = 0; c < 5; ++c)
            for (int r = 0; r < 7; ++r)
            {
                const juce::Rectangle<float> d (x0 + (float) (i * 6 + c) * pitch, y0 + (float) r * pitch, dot, dot);
                if ((cols[c] >> r) & 1)
                {
                    g.setColour (glow);
                    g.fillRect (d.expanded (pitch * .22f));
                    g.setColour (C::ledBlue);
                    g.fillRect (d);
                }
                else
                {
                    g.setColour (lcdDotOff);
                    g.fillRect (d);
                }
            }
    }
}

void CampionePanel::timerCallback()
{
    updateLcd();
    if (lcdText.length() > lcdChars)
    {
        // 1,5 s fermo all'inizio, poi un carattere ogni 0,3 s
        ++lcdTicks;
        const bool atStart = lcdOffset == 0;
        if ((atStart && lcdTicks >= 45) || (! atStart && lcdTicks >= 9))
        {
            lcdTicks = 0;
            lcdOffset = (lcdOffset + 1) % (lcdText.length() + 4);
            repaint (lcdArea);
        }
    }
    meter.setLevel (std::pow (proc.outputLevel(), .5f));
    if (panicFlash > 0) --panicFlash;
    panicBtn.setToggleState (panicFlash > 0 && (panicFlash / 3) % 2 == 0, juce::dontSendNotification);
    repaint (displayArea);
}

// ================================================================================================ editor
CampioneEditor::CampioneEditor (CampioneProcessor& p) : AudioProcessorEditor (p), proc (p), panel (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (panel);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) CampionePanel::baseW / CampionePanel::baseH);
    setResizeLimits (CampionePanel::baseW / 2, CampionePanel::baseH / 2, CampionePanel::baseW * 2, CampionePanel::baseH * 2);
    setSize (CampionePanel::baseW, CampionePanel::baseH);
}

CampioneEditor::~CampioneEditor() { setLookAndFeel (nullptr); }

void CampioneEditor::paint (juce::Graphics& g) { ui::paintChassis (g, getLocalBounds()); }

void CampioneEditor::resized()
{
    const float s = (float) getWidth() / (float) CampionePanel::baseW;
    panel.setBounds (0, 0, CampionePanel::baseW, CampionePanel::baseH);
    panel.setTransform (juce::AffineTransform::scale (s));
}
