#include "BlockView.h"
#include "BlockPresets.h"

#include <sonic/ModuleRegistry.h>

using sonic::Graph;

namespace
{
    std::unique_ptr<juce::Label> makeLabel (const juce::String& text, juce::Font f, juce::Colour c)
    {
        auto l = std::make_unique<juce::Label>();
        l->setText (text, juce::dontSendNotification);
        l->setFont (f);
        l->setColour (juce::Label::textColourId, c);
        l->setJustificationType (juce::Justification::topLeft);
        l->setInterceptsMouseClicks (false, false);
        return l;
    }

    juce::String summaryFor (const std::string& type)
    {
        for (auto& m : sonic::availableModules())
            if (m.typeId == type)
                return m.summary;
        return {};
    }

    // ---------------------------------------------------------------- Input

    class Waveform : public juce::Component
    {
    public:
        explicit Waveform (AtelierProcessor& p) : proc (p) {}
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds();
            g.setColour (theme::sheet);
            g.fillRect (r);
            g.setColour (theme::ink);
            g.drawRect (r, 1);
            auto& thumb = proc.thumbnail();
            const double len = thumb.getTotalLength();
            if (len <= 0.0)
            {
                g.setColour (theme::textDim);
                g.setFont (theme::sans (12.0f));
                g.drawText ("No file loaded", r, juce::Justification::centred);
                return;
            }
            g.setColour (theme::ink.withAlpha (0.75f));
            thumb.drawChannels (g, r.reduced (2, 4), 0.0, len, 1.0f);
            const double pos = proc.transport().getCurrentPosition();
            g.setColour (theme::oxblood);
            g.fillRect ((float) (pos / juce::jmax (0.001, proc.transport().getLengthInSeconds())) * (float) r.getWidth(), 0.0f, 1.5f, (float) r.getHeight());
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            const double len = proc.transport().getLengthInSeconds();
            if (len > 0.0)
                proc.transport().setPosition (juce::jlimit (0.0, len, len * e.position.x / (double) getWidth()));
        }
    private:
        AtelierProcessor& proc;
    };

    class InputBody : public BlockBody
    {
    public:
        explicit InputBody (Ui& u) : ui (u)
        {
            auto& st = ui.proc.apvts;
            auto add = [&] (const char* id, const char* caption, const juce::String& tip)
            {
                auto* r = new ApvtsRow (st, id, caption, tip);
                left.add (r);
                addAndMakeVisible (r);
            };
            add ("src.mode", "Source", "What feeds the bridge. DAW track = the audio on this mixer channel. Generator = test signals "
                                       "with exact content (best for measuring). Audio file = a reference recording.");
            add ("io.in_trim_db", "Input trim", help::tooltip ("Input trim", { "Level going into the bridge.", "hits every block harder", "gentler on every block" }));
            const auto src = ui.proc.source();
            if (src == AtelierProcessor::Source::generator)
            {
                run = std::make_unique<juce::TextButton> ("GENERATOR  OFF");
                run->setClickingTogglesState (true);
                runAtt = std::make_unique<ButtonAttachment> (st, "gen.run", *run);
                auto* rp = run.get();
                run->onStateChange = [rp] { rp->setButtonText (rp->getToggleState() ? "GENERATOR  RUNNING" : "GENERATOR  OFF"); };
                run->setButtonText (run->getToggleState() ? "GENERATOR  RUNNING" : "GENERATOR  OFF");
                addAndMakeVisible (*run);
                add ("gen.waveform", "Waveform", "Test signal shape. Sine shows harmonics clearly; noise and sweeps show the frequency response.");
                add ("gen.freq_hz", "Frequency", "Frequency of the test tone.");
                add ("gen.level_db", "Level", help::tooltip ("Level", { "Level of the test signal.", "drives the blocks harder (more colour)", "cleaner, small-signal behaviour" }));
                add ("gen.phase_deg", "Phase", "Starting phase of the tone.");
                add ("gen.channels", "Channels", "Which channels get the signal.");
                add ("gen.dc", "DC offset", "Adds a constant offset (tests asymmetry).");
                add ("gen.sweep_start_hz", "Sweep start", "Sweep: start frequency.");
                add ("gen.sweep_end_hz", "Sweep end", "Sweep: end frequency.");
                add ("gen.sweep_s", "Sweep time", "Sweep: duration.");
            }
            else if (src == AtelierProcessor::Source::file)
            {
                load = std::make_unique<juce::TextButton> ("LOAD AUDIO FILE...");
                load->onClick = [this] { chooseFile(); };
                play = std::make_unique<juce::TextButton> ("PLAY / PAUSE");
                play->onClick = [this] { auto& t = ui.proc.transport(); if (t.isPlaying()) t.stop(); else t.start(); };
                stop = std::make_unique<juce::TextButton> ("STOP");
                stop->onClick = [this] { ui.proc.transport().stop(); ui.proc.transport().setPosition (0.0); };
                wave = std::make_unique<Waveform> (ui.proc);
                for (juce::Component* c : { (juce::Component*) load.get(), (juce::Component*) play.get(), (juce::Component*) stop.get(), (juce::Component*) wave.get() })
                    addAndMakeVisible (c);
                add ("file.gain_db", "File gain", help::tooltip ("File gain", { "Level of the file.", "louder", "quieter" }));
            }
        }
        void sync() override
        {
            if (wave) wave->repaint();
            repaint (getLocalBounds().removeFromRight (getWidth() / 2));
        }
        void resized() override
        {
            auto r = getLocalBounds();
            auto l = r.removeFromLeft ((int) (r.getWidth() * 0.52f));
            // controls in two columns
            const int colW = (l.getWidth() - 24) / 2;
            int i = 0;
            auto slot = [&] (int h) { auto b = juce::Rectangle<int> (l.getX() + (i / 12) * (colW + 24), l.getY() + (i % 12) * 30, colW, h); ++i; return b; };
            for (int k = 0; k < left.size(); ++k)
            {
                if (k == 2 && run) run->setBounds (slot (28));
                left[k]->setBounds (slot (28));
            }
            if (load)
            {
                auto f = l.withTrimmedLeft (colW + 24);
                load->setBounds (f.removeFromTop (28));
                f.removeFromTop (22);
                wave->setBounds (f.removeFromTop (90));
                f.removeFromTop (8);
                auto row = f.removeFromTop (28);
                play->setBounds (row.removeFromLeft ((row.getWidth() - 6) / 2));
                row.removeFromLeft (6);
                stop->setBounds (row);
            }
            side = r.withTrimmedLeft (32);
        }
        void paint (juce::Graphics& g) override
        {
            if (load)
            {
                g.setColour (theme::textDim);
                g.setFont (theme::sans (12.0f));
                g.drawText (ui.proc.currentFile() == juce::File() ? "WAV, AIFF or FLAC" : ui.proc.currentFile().getFileName(),
                            load->getBounds().translated (0, 30).withHeight (18), juce::Justification::centredLeft, true);
            }
            if (ui.proc.source() == AtelierProcessor::Source::dawInput)
            {
                g.setColour (theme::textDim);
                g.setFont (theme::sans (13.0f));
                g.drawFittedText ("Atelier is processing this mixer track's audio.", juce::Rectangle<int> (0, 80, getWidth() / 2 - 20, 40),
                                  juce::Justification::topLeft, 2);
            }

            // sidechain
            auto s = side;
            g.setColour (theme::ink);
            g.drawVerticalLine (s.getX() - 16, (float) s.getY(), (float) s.getBottom());
            g.setFont (theme::heading (21.0f));
            g.drawText ("Sidechain", s.removeFromTop (28), juce::Justification::centredLeft);
            const auto st = ui.proc.sidechainState();
            const juce::String state = st == AtelierProcessor::SidechainState::active ? "RECEIVING SIGNAL"
                                     : st == AtelierProcessor::SidechainState::silent ? "NO SIGNAL YET" : "NOT AVAILABLE IN THIS HOST";
            g.setFont (theme::mono (10.0f));
            g.setColour (st == AtelierProcessor::SidechainState::active ? theme::good : theme::oxblood);
            g.drawText (state, s.removeFromTop (16), juce::Justification::centredLeft);
            auto meter = s.removeFromTop (14).withTrimmedTop (6).withWidth (220).toFloat();
            drawMiniMeter (g, meter, ui.proc.sidechainMeter.peak[0].load());
            s.removeFromTop (14);
            g.setColour (theme::text);
            g.setFont (theme::sans (12.5f));
            const juce::String steps =
                "Use it to make a Compressor react to another track (set its Key to Sidechain), or to compare a plugin's "
                "input and output (Readout > REFERENCE).\n\n"
                "In FL Studio:\n"
                "1.  Select the source track (e.g. the kick) in the mixer, right-click the arrow under Atelier's track and "
                "choose \"Sidechain to this track\".\n"
                "2.  In Atelier's plugin window: the gear icon > Processing > turn the sidechain input on and pick that track.\n"
                "3.  This line turns green: RECEIVING SIGNAL.";
            g.drawFittedText (steps, s.removeFromTop (220), juce::Justification::topLeft, 16, 1.0f);
        }

    private:
        void chooseFile()
        {
            chooser = std::make_unique<juce::FileChooser> ("Load reference audio", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                           "*.wav;*.aif;*.aiff;*.flac;*.ogg");
            juce::Component::SafePointer<InputBody> safe (this);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
            {
                if (safe == nullptr || fc.getResult() == juce::File()) return;
                juce::String err;
                if (! safe->ui.proc.loadFile (fc.getResult(), err))
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not load file", err, {}, safe.getComponent());
                safe->repaint();
            });
        }
        Ui& ui;
        juce::OwnedArray<ApvtsRow> left;
        std::unique_ptr<juce::TextButton> run, load, play, stop;
        std::unique_ptr<ButtonAttachment> runAtt;
        std::unique_ptr<Waveform> wave;
        std::unique_ptr<juce::FileChooser> chooser;
        juce::Rectangle<int> side;
    };

    // ---------------------------------------------------------------- Output

    class OutputBody : public BlockBody
    {
    public:
        explicit OutputBody (Ui& u) : ui (u)
        {
            trim = std::make_unique<ApvtsRow> (ui.proc.apvts, "io.out_trim_db", "Output trim",
                                               help::tooltip ("Output trim", { "Level leaving Atelier.", "louder", "quieter" }));
            addAndMakeVisible (*trim);
        }
        void sync() override { repaint(); }
        void resized() override { trim->setBounds (0, 0, juce::jmin (420, getWidth() / 2), 28); }
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().withTrimmedTop (48).withWidth (juce::jmin (520, getWidth() / 2));
            auto item = [&] (const juce::String& label, const juce::String& value)
            {
                auto row = r.removeFromTop (54);
                g.setColour (theme::textFaint);
                g.setFont (theme::mono (9.5f));
                g.drawText (label, row.removeFromTop (14), juce::Justification::centredLeft);
                g.setColour (theme::ink);
                g.setFont (theme::numeral (26.0f));
                g.drawText (value, row.removeFromTop (32), juce::Justification::centredLeft);
            };
            item ("LISTENING", ui.proc.isBypassed() ? "B  -  dry" : "A  -  processed");
            item ("A - B LOUDNESS", formatValue (ui.proc.gainMatchDb(), "dB"));
            item ("BRIDGE LATENCY", juce::String (ui.proc.currentLatency()) + " smp  (" + juce::String (ui.proc.currentLatency() / ui.proc.currentSampleRate() * 1000.0, 1) + " ms)");
            auto s = getLocalBounds().withTrimmedLeft (getWidth() / 2 + 32);
            g.setColour (theme::ink);
            g.drawVerticalLine (s.getX() - 16, (float) s.getY(), (float) s.getBottom());
            g.setColour (theme::textDim);
            g.setFont (theme::sans (12.5f));
            g.drawFittedText ("Everything wired into Output is added together. Parallel paths are delayed so they line up "
                              "sample-accurately; any phasing you still hear between two paths comes from the blocks themselves "
                              "(filters, tape, modulation), just like with hardware.\n\n"
                              "A / B in the header switches between the processed sound and the dry signal (time-aligned). "
                              "With MATCH on, B is played at A's loudness so you judge tone, not volume.",
                              s.removeFromTop (180), juce::Justification::topLeft, 12, 1.0f);
        }
    private:
        Ui& ui;
        std::unique_ptr<ApvtsRow> trim;
    };
}

// ================================================================== BlockView

BlockView::BlockView (Ui& u, PreviewEngine& e) : ui (u), engine (e)
{
    presets.setButtonText (juce::String (juce::CharPointer_UTF8 ("PRESETS \xe2\x96\xbe")));
    presets.getProperties().set ("colour", (int) theme::accent.getARGB());
    presets.setTooltip ("Built-in settings for this block, and your own: save, load, delete.");
    presets.onClick = [this] { showPresetMenu(); };
    bypass.setClickingTogglesState (false);
    bypass.setTooltip ("Bypass this block (B). The bridge stays time-aligned.");
    bypass.onClick = [this]
    {
        if (auto* n = ui.proc.graph().node (shown))
        {
            n->enabled = ! n->enabled.load();
            ui.proc.checkpoint();
            if (ui.onBridgeChanged) ui.onBridgeChanged();
        }
    };
    close.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xc3\x97")));
    close.getProperties().set ("link", true);
    close.setTooltip ("Close (Esc). The spectrum comes back.");
    close.onClick = [this] { ui.select (-1); };
    for (auto* b : { &presets, &bypass, &close })
        addAndMakeVisible (b);
    startTimerHz (15);
}

bool BlockView::isStale() const
{
    if (shown < 2) return false;
    auto* n = ui.proc.graph().node (shown);
    return n == nullptr || n->module.get() != shownModule;
}

void BlockView::showFor (int id)
{
    content.reset();
    shown = id;
    shownModule = nullptr;
    presetName.clear();
    lastSource = (int) ui.proc.source();
    auto* n = id >= 2 ? ui.proc.graph().node (id) : nullptr;
    if (n != nullptr) shownModule = n->module.get();
    if (id == Graph::kInputId) content = std::make_unique<InputBody> (ui);
    else if (id == Graph::kOutputId) content = std::make_unique<OutputBody> (ui);
    else if (n != nullptr && n->type == "tone.eq") content = std::make_unique<EqView> (ui, id);
    else if (n != nullptr && n->type == "character.saturation") content = std::make_unique<SaturationView> (ui, id, engine);
    else if (n != nullptr) content = std::make_unique<GenericBody> (ui, id, engine);
    if (content != nullptr)
        addAndMakeVisible (*content);
    presets.setVisible (n != nullptr);
    bypass.setVisible (n != nullptr);
    resized();
    repaint();
}

void BlockView::pumpForTests()
{
    timerCallback();
    std::function<void (juce::Component&)> visit = [&] (juce::Component& c)
    {
        if (auto* f = dynamic_cast<BlockFigure*> (&c)) f->poll();
        for (auto* ch : c.getChildren()) visit (*ch);
    };
    visit (*this);
}

void BlockView::analysisUpdated()
{
    if (content != nullptr)
        content->analysisUpdated();
}

void BlockView::timerCallback()
{
    if (shown >= 2 && ui.proc.graph().node (shown) == nullptr)
    {
        if (isVisible()) ui.select (-1);
        return;
    }
    if (isStale())
    {
        const auto keep = presetName;
        showFor (shown);
        presetName = keep;
        return;
    }
    if (shown == Graph::kInputId && (int) ui.proc.source() != lastSource)
    {
        showFor (shown);
        return;
    }
    if (content != nullptr)
        content->sync();
    if (auto* n = ui.proc.graph().node (shown))
    {
        bypass.setToggleState (! n->enabled.load(), juce::dontSendNotification);
        bypass.setButtonText (n->enabled.load() ? "BYPASS" : "BYPASSED");
    }
    repaint (getLocalBounds().withHeight (46));
}

juce::String BlockView::titleText() const
{
    if (shown == Graph::kInputId) return "Input";
    if (shown == Graph::kOutputId) return "Output";
    if (auto* n = ui.proc.graph().node (shown)) return juce::String (n->name);
    return {};
}

juce::String BlockView::noteText() const
{
    if (shown < 2) return shown == Graph::kInputId ? "WHAT FEEDS THE BRIDGE" : "WHAT LEAVES ATELIER";
    auto* n = ui.proc.graph().node (shown);
    if (n == nullptr) return {};
    const auto order = ui.proc.graph().activeOrder();
    const auto it = std::find (order.begin(), order.end(), n->id);
    juce::String s = juce::String (juce::CharPointer_UTF8 ("N\xc2\xb0")) + (it == order.end() ? juce::String ("--") : juce::String ((int) (it - order.begin()) + 1).paddedLeft ('0', 2));
    s << "   " << juce::String (summaryFor (n->type)).toUpperCase().upToFirstOccurrenceOf (".", false, false);
    if (presetName.isNotEmpty())
        s << "   PRESET: " << presetName.toUpperCase() << (n->module->paramVersion() != presetVersion ? " (EDITED)" : "");
    return s;
}

void BlockView::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
    g.setColour (theme::ink);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());
    auto h = getLocalBounds().withHeight (46).withTrimmedLeft (16);
    const auto f = theme::heading (28.0f);
    g.setFont (f);
    const auto t = titleText();
    g.drawText (t, h, juce::Justification::centredLeft);
    const int w = juce::GlyphArrangement::getStringWidthInt (f, t);
    g.setColour (theme::textFaint);
    g.setFont (theme::mono (9.5f));
    g.drawText (noteText(), h.withTrimmedLeft (w + 16).withTrimmedRight (300).translated (0, 3), juce::Justification::centredLeft, true);
    if (auto* n = ui.proc.graph().node (shown); n != nullptr && ! n->enabled.load())
    {
        g.setColour (theme::oxblood);
        g.setFont (theme::mono (9.5f));
        g.drawText ("BYPASSED - YOU HEAR THIS BLOCK'S INPUT", getLocalBounds().withHeight (46).withTrimmedRight (330), juce::Justification::centredRight);
    }
}

void BlockView::resized()
{
    auto h = getLocalBounds().withHeight (46).reduced (0, 10);
    close.setBounds (h.removeFromRight (26));
    h.removeFromRight (10);
    bypass.setBounds (h.removeFromRight (92));
    h.removeFromRight (6);
    presets.setBounds (h.removeFromRight (104));
    if (content != nullptr)
        content->setBounds (getLocalBounds().withTrimmedTop (58).reduced (16, 0).withTrimmedBottom (8));
}

void BlockView::showPresetMenu()
{
    auto* n = ui.proc.graph().node (shown);
    if (n == nullptr) return;
    const auto type = n->type;
    const auto& fac = blockpresets::factory (type);
    auto mine = blockpresets::personal (type);
    juce::PopupMenu m;
    m.addSectionHeader (juce::String (n->name));
    for (int i = 0; i < (int) fac.size(); ++i)
        m.addItem (1000 + i, fac[(size_t) i].name);
    if (! mine.empty())
    {
        m.addSectionHeader ("My presets");
        for (int i = 0; i < (int) mine.size(); ++i)
            m.addItem (2000 + i, mine[(size_t) i].name);
    }
    m.addSeparator();
    m.addItem (1, "Save these settings as a preset...");
    juce::PopupMenu del;
    for (int i = 0; i < (int) mine.size(); ++i)
        del.addItem (juce::PopupMenu::Item (mine[(size_t) i].name).setID (3000 + i).setColour (theme::danger));
    m.addSubMenu ("Delete one of my presets", del, ! mine.empty());
    m.addItem (2, "Show my presets folder");
    m.addSeparator();
    m.addItem (4, "Reset this block to defaults");

    juce::Component::SafePointer<BlockView> safe (this);
    const int id = shown;
    m.showMenuAsync (ui.menuOptions().withTargetComponent (&presets), [safe, id, type, mine] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto* node = safe->ui.proc.graph().node (id);
        if (node == nullptr) return;
        auto applyPreset = [&] (const blockpresets::Preset& p)
        {
            blockpresets::apply (p, *node);
            safe->ui.proc.checkpoint();
            safe->presetName = p.name;
            safe->presetVersion = node->module->paramVersion();
            safe->showFor (id);   // rebuild: relevance and EQ bands may have changed
            safe->presetName = p.name;
            if (safe->ui.onBridgeChanged) safe->ui.onBridgeChanged();
        };
        if (r >= 1000 && r < 2000) applyPreset (blockpresets::factory (type)[(size_t) (r - 1000)]);
        else if (r >= 2000 && r < 3000) applyPreset (mine[(size_t) (r - 2000)]);
        else if (r >= 3000)
        {
            const auto p = mine[(size_t) (r - 3000)];
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Delete preset",
                "Delete \"" + p.name + "\"? It is moved to the recycle bin.", "Delete", "Cancel", safe.getComponent(),
                juce::ModalCallbackFunction::create ([p] (int res) { if (res == 1) blockpresets::remove (p); }));
        }
        else if (r == 1) safe->savePresetDialog();
        else if (r == 2) { blockpresets::folder (type).createDirectory(); blockpresets::folder (type).startAsProcess(); }
        else if (r == 4)
        {
            blockpresets::Preset p;
            p.name = {};
            blockpresets::apply (p, *node);
            safe->ui.proc.checkpoint();
            safe->showFor (id);
        }
    });
}

void BlockView::savePresetDialog()
{
    auto* n = ui.proc.graph().node (shown);
    if (n == nullptr) return;
    auto* w = new juce::AlertWindow ("Save preset", "Saves every setting of this block (and its Mix and Output).", juce::MessageBoxIconType::NoIcon);
    w->setLookAndFeel (&dialogLookAndFeel());
    w->addTextEditor ("name", presetName.isNotEmpty() ? presetName : juce::String (n->name), "Name");
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<BlockView> safe (this);
    const int id = shown;
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, id] (int res)
    {
        if (safe == nullptr || res != 1) return;
        auto* node = safe->ui.proc.graph().node (id);
        if (node == nullptr) return;
        juce::String err;
        const auto name = w->getTextEditorContents ("name");
        if (! blockpresets::save (*node, name, err))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Preset not saved", err, {}, safe.getComponent());
        else
        {
            safe->presetName = name.trim();
            safe->presetVersion = node->module->paramVersion();
            safe->repaint();
        }
    }), true);
}
