#include "Inspector.h"

#include <sonic/ModuleRegistry.h>

using sonic::Graph;

namespace
{
    juce::String summaryFor (const std::string& type)
    {
        for (auto& m : sonic::availableModules())
            if (m.typeId == type)
                return m.summary;
        return {};
    }

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
}

// ------------------------------------------------------------------ NodeKnob

InspectorPanel::NodeKnob::NodeKnob (Ui& u, int nodeId, bool isMix) : ui (u), id (nodeId), mix (isMix)
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    if (mix) { slider.setRange (0.0, 100.0, 0.1); slider.setDefault (100.0); slider.getProperties().set ("beige", true); slider.setTextValueSuffix (" %"); }
    else { slider.setRange (-24.0, 24.0, 0.01); slider.setDefault (0.0); slider.setTextValueSuffix (" dB"); }
    slider.setNumDecimalPlacesToDisplay (1);
    slider.valueArea = [this] { return getLocalBounds().removeFromBottom (18).reduced (8, 0) - slider.getPosition(); };
    slider.setTooltip (mix ? "Mix: blend between this block's input (0 %) and its processed output (100 %), time-aligned."
                           : "Output level of this block.");
    slider.onValueChange = [this]
    {
        if (auto* n = ui.proc.graph().node (id))
        {
            if (mix) n->mix = (float) slider.getValue() * 0.01f;
            else n->outGainDb = (float) slider.getValue();
        }
        repaint();
    };
    slider.onDragEnd = [this] { ui.proc.checkpoint(); };
    addAndMakeVisible (slider);
    sync();
}

void InspectorPanel::NodeKnob::sync()
{
    if (slider.isMouseButtonDown()) return;
    if (auto* n = ui.proc.graph().node (id))
        slider.setValue (mix ? n->mix.load() * 100.0 : n->outGainDb.load(), juce::dontSendNotification);
    repaint();
}

void InspectorPanel::NodeKnob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (34);
    slider.setBounds (r);
}

void InspectorPanel::NodeKnob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().removeFromBottom (32);
    g.setColour (theme::ink);
    g.setFont (theme::heading (17.0f));
    g.drawText (mix ? "Mix" : "Block output", r.removeFromTop (17), juce::Justification::centred);
    g.setColour (theme::textDim);
    g.setFont (theme::mono (9.5f));
    g.drawText (mix ? juce::String (juce::roundToInt (slider.getValue())) + " %" : formatValue (slider.getValue(), "dB"), r, juce::Justification::centred);
}

// ------------------------------------------------------------------ Waveform

void InspectorPanel::Waveform::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (theme::bg);
    g.fillRect (r);
    auto& thumb = proc.thumbnail();
    const double len = thumb.getTotalLength();
    if (len <= 0.0)
    {
        g.setColour (theme::textDim);
        g.setFont (theme::sans (12.0f));
        g.drawText ("No file loaded", r, juce::Justification::centred);
        return;
    }
    g.setColour (theme::reference.brighter (0.3f));
    thumb.drawChannels (g, r.reduced (0, 3), 0.0, len, 1.0f);
    const double pos = proc.transport().getCurrentPosition();
    g.setColour (theme::beige);
    g.fillRect ((float) (pos / juce::jmax (0.001, proc.transport().getLengthInSeconds())) * (float) r.getWidth(), 0.0f, 1.5f, (float) r.getHeight());
}

void InspectorPanel::Waveform::mouseDown (const juce::MouseEvent& e)
{
    const double len = proc.transport().getLengthInSeconds();
    if (len > 0.0)
        proc.transport().setPosition (juce::jlimit (0.0, len, len * e.position.x / (double) getWidth()));
}

// ------------------------------------------------------------------ InspectorPanel

InspectorPanel::InspectorPanel (Ui& u) : ui (u)
{
    simpleBtn.onClick = [this] { advanced = false; rebuild(); };
    advancedBtn.onClick = [this] { advanced = true; rebuild(); };
    simpleBtn.setTooltip ("The few controls that matter most, with explanations.");
    advancedBtn.setTooltip ("Every parameter of this block.");
    addChildComponent (simpleBtn);
    addChildComponent (advancedBtn);
    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (6);
    addChildComponent (viewport);
    startTimerHz (15);
    rebuild();
}

void InspectorPanel::rebuild()
{
    builtRelevance = relevanceKey();
    knobs.clear();
    nodeKnobs.clear();
    choices.clear();
    rows.clear();
    extras.clear();
    buttons.clear();
    waveform.reset();
    grHistory.clear();
    grArea = {};
    viewport.setVisible (false);
    list.removeAllChildren();
    simpleBtn.setVisible (false);
    advancedBtn.setVisible (false);
    description.clear();
    subtitle.clear();

    builtFor = ui.selected;
    if (ui.selected == Graph::kInputId)
        buildInput();
    else if (ui.selected == Graph::kOutputId)
        buildOutput();
    else if (auto* n = ui.selectedNode())
        buildBlock (*n);
    else
    {
        title = "Inspector"; titleNote = {};
        description = "Select a block in the bridge to shape it.\n\nClick the Input block to choose what you hear: the DAW track, "
                      "the test generator or an audio file. Click Output for A/B listening and output level.";
    }
    resized();
    repaint();
}

void InspectorPanel::buildBlock (sonic::Graph::Node& n)
{
    auto& m = *n.module;
    title = juce::String (n.name);
    {
        const auto order = ui.proc.graph().activeOrder();
        const auto it = std::find (order.begin(), order.end(), n.id);
        titleNote = juce::String (juce::CharPointer_UTF8 ("N\xc2\xb0")) + (it == order.end() ? juce::String ("--") : juce::String ((int) (it - order.begin()) + 1).paddedLeft ('0', 2));
    }
    description = summaryFor (n.type);
    simpleBtn.setVisible (true);
    advancedBtn.setVisible (true);
    simpleBtn.setToggleState (! advanced, juce::dontSendNotification);
    advancedBtn.setToggleState (advanced, juce::dontSendNotification);
    const bool hasEditor = n.type == "tone.eq" || n.type == "character.saturation";

    if (! advanced)
    {
        for (int i = 0; i < m.numParams(); ++i)
        {
            const auto& s = m.params()[(size_t) i];
            if (! s.simple || ! m.isParamRelevant (i)) continue;
            if (s.isChoice())
            {
                auto* c = choices.add (new ModuleChoice (ui.proc, m, i));
                c->setAccent (blockColour (n.type));
                addAndMakeVisible (c);
            }
            else
            {
                auto* k = knobs.add (new ModuleKnob (ui.proc, m, i));
                k->slider.getProperties().set ("arcColour", (int) blockColour (n.type).getARGB());
                addAndMakeVisible (k);
            }
        }
        addAndMakeVisible (nodeKnobs.add (new NodeKnob (ui, n.id, true)));
        if (n.type == "dynamics.compressor")
            grHistory.assign (140, 0.0f);
    }
    else
    {
        viewport.setVisible (true);
        juce::String group;
        int y = 0;
        auto addHeader = [&] (const juce::String& g)
        {
            auto l = makeLabel (g.toUpperCase(), theme::mono (9.5f), theme::beigeText);
            l->setBounds (0, y + 8, 280, 16);
            list.addAndMakeVisible (*l);
            extras.add (l.release());
            y += 28;
        };
        const bool eq = n.type == "tone.eq";
        for (int i = 0; i < m.numParams(); ++i)
        {
            const auto& s = m.params()[(size_t) i];
            if (eq && juce::String (s.group).startsWith ("Band")) continue; // bands: use the EQ editor
            if (! m.isParamRelevant (i)) continue;                          // ignored by the current model
            if (juce::String (s.group) != group)
            {
                group = s.group;
                addHeader (group);
            }
            auto* row = rows.add (new ModuleParamRow (ui.proc, m, i));
            row->setBounds (0, y, 284, 26);
            list.addAndMakeVisible (row);
            y += 28;
        }
        addHeader ("Block");
        auto* gainKnob = nodeKnobs.add (new NodeKnob (ui, n.id, false));
        auto* mixKnob = nodeKnobs.add (new NodeKnob (ui, n.id, true));
        mixKnob->setBounds (20, y, 110, 100);
        gainKnob->setBounds (150, y, 110, 100);
        list.addAndMakeVisible (mixKnob);
        list.addAndMakeVisible (gainKnob);
        y += 110;
        if (eq)
        {
            auto l = makeLabel ("Bands are edited in the EQ editor (button below).", theme::sans (12.0f), theme::textDim);
            l->setBounds (0, y, 280, 34);
            list.addAndMakeVisible (*l);
            extras.add (l.release());
            y += 40;
        }
        list.setSize (288, y + 8);
    }

    auto* bypass = buttons.add (new juce::TextButton (n.enabled.load() ? "BYPASS" : "ENABLE"));
    const int id = n.id;
    bypass->onClick = [this, id]
    {
        if (auto* nn = ui.proc.graph().node (id)) { nn->enabled = ! nn->enabled.load(); ui.proc.checkpoint(); rebuild(); }
    };
    auto* remove = buttons.add (new juce::TextButton ("REMOVE"));
    remove->onClick = [this, id]
    {
        ui.proc.graph().removeNode (id);
        ui.proc.checkpoint();
        ui.select (-1);
        if (ui.onBridgeChanged) ui.onBridgeChanged();
    };
    if (hasEditor)
    {
        auto* open = buttons.add (new juce::TextButton (n.type == "tone.eq" ? "OPEN EQ EDITOR  ->" : "OPEN HARMONIC EDITOR  ->"));
        open->getProperties().set ("beige", true);
        open->onClick = [this, id] { if (ui.openFullEditor) ui.openFullEditor (id); };
    }
    else if (! advanced)
    {
        auto* adv = buttons.add (new juce::TextButton ("ADVANCED - " + juce::String (m.numParams()) + " CONTROLS  ->"));
        adv->onClick = [this] { advanced = true; rebuild(); };
    }
    for (auto* b : buttons)
        addAndMakeVisible (b);
}

void InspectorPanel::buildInput()
{
    title = "Input"; titleNote = {};
    description = "What feeds the bridge. DAW track = the audio on this channel. Generator = test signals with exact, "
                  "known content (best for measuring). Audio file = a reference recording.";
    auto& st = ui.proc.apvts;
    viewport.setVisible (true);
    int y = 0;
    auto addRow = [&] (const char* id, const char* caption)
    {
        auto* r = new ApvtsRow (st, id, caption);
        r->setBounds (0, y, 284, 28);
        list.addAndMakeVisible (r);
        extras.add (r);
        y += 30;
    };
    addRow ("src.mode", "Source");
    addRow ("io.in_trim_db", "Input trim");
    y += 8;
    if (ui.proc.source() == AtelierProcessor::Source::generator)
    {
        auto* run = new juce::TextButton ("GENERATOR  OFF");
        run->setClickingTogglesState (true);
        run->getProperties().set ("attach", true);
        auto att = std::make_unique<ButtonAttachment> (st, "gen.run", *run);
        run->onStateChange = [run] { run->setButtonText (run->getToggleState() ? "GENERATOR  RUNNING" : "GENERATOR  OFF"); };
        run->setButtonText (run->getToggleState() ? "GENERATOR  RUNNING" : "GENERATOR  OFF");
        run->setBounds (0, y, 284, 34);
        list.addAndMakeVisible (run);
        extras.add (run);
        struct Holder : juce::Component { std::unique_ptr<ButtonAttachment> a; };
        auto* h = new Holder();
        h->a = std::move (att);
        extras.add (h);
        y += 44;
        addRow ("gen.waveform", "Waveform");
        addRow ("gen.freq_hz", "Frequency");
        addRow ("gen.level_db", "Level");
        addRow ("gen.phase_deg", "Phase");
        addRow ("gen.channels", "Channels");
        addRow ("gen.dc", "DC offset");
        addRow ("gen.sweep_start_hz", "Sweep start");
        addRow ("gen.sweep_end_hz", "Sweep end");
        addRow ("gen.sweep_s", "Sweep time");
    }
    else if (ui.proc.source() == AtelierProcessor::Source::file)
    {
        auto* load = new juce::TextButton ("LOAD AUDIO FILE...");
        load->onClick = [this] { chooseFile(); };
        load->setBounds (0, y, 284, 32);
        list.addAndMakeVisible (load);
        extras.add (load);
        y += 40;
        auto name = makeLabel (ui.proc.currentFile() == juce::File() ? "WAV, AIFF or FLAC" : ui.proc.currentFile().getFileName(),
                               theme::sans (12.0f), theme::textDim);
        name->setBounds (0, y, 284, 18);
        list.addAndMakeVisible (*name);
        extras.add (name.release());
        y += 22;
        waveform = std::make_unique<Waveform> (ui.proc);
        waveform->setBounds (0, y, 284, 70);
        list.addAndMakeVisible (*waveform);
        y += 78;
        auto* play = new juce::TextButton ("PLAY / PAUSE");
        play->onClick = [this] { auto& t = ui.proc.transport(); if (t.isPlaying()) t.stop(); else t.start(); };
        play->setBounds (0, y, 140, 30);
        auto* stop = new juce::TextButton ("STOP");
        stop->onClick = [this] { ui.proc.transport().stop(); ui.proc.transport().setPosition (0.0); };
        stop->setBounds (146, y, 138, 30);
        list.addAndMakeVisible (play);
        list.addAndMakeVisible (stop);
        extras.add (play);
        extras.add (stop);
        y += 38;
        addRow ("file.gain_db", "File gain");
    }
    else
    {
        auto l = makeLabel ("Atelier is processing the track's audio.\n\nTo analyse another plugin, put it before Atelier, "
                            "route the unprocessed signal to Atelier's sidechain and choose REFERENCE in the readout.",
                            theme::sans (12.5f), theme::textDim);
        l->setBounds (0, y, 284, 110);
        list.addAndMakeVisible (*l);
        extras.add (l.release());
        y += 116;
    }
    list.setSize (288, y + 8);
}

void InspectorPanel::buildOutput()
{
    title = "Output"; titleNote = {};
    description = "Listening and level. B plays the unprocessed signal, delayed to line up with A; with gain match on, "
                  "B is matched to A's loudness so you judge tone, not volume.";
    viewport.setVisible (true);
    int y = 0;
    for (auto [id, cap] : { std::pair<const char*, const char*> { "io.out_trim_db", "Output trim" } })
    {
        auto* r = new ApvtsRow (ui.proc.apvts, id, cap);
        r->setBounds (0, y, 284, 28);
        list.addAndMakeVisible (r);
        extras.add (r);
        y += 34;
    }
    auto info = makeLabel ("", theme::mono (11.0f), theme::text);
    info->setBounds (0, y, 284, 80);
    info->setName ("outinfo");
    list.addAndMakeVisible (*info);
    extras.add (info.release());
    y += 86;
    list.setSize (288, y);
}

void InspectorPanel::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Load reference audio", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg");
    juce::Component::SafePointer<InspectorPanel> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getResult() == juce::File()) return;
        juce::String err;
        if (! safe->ui.proc.loadFile (fc.getResult(), err))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not load file", err, {}, safe.getComponent());
        safe->rebuild();
    });
}

juce::String InspectorPanel::relevanceKey() const
{
    juce::String key;
    if (auto* n = ui.proc.graph().node (ui.selected); n != nullptr && ui.selected >= 2)
        for (int i = 0; i < n->module->numParams(); ++i)
            key << (n->module->isParamRelevant (i) ? '1' : '0');
    return key;
}

void InspectorPanel::timerCallback()
{
    if (builtFor != ui.selected || (ui.selected >= 2 && ui.selectedNode() == nullptr))
    {
        if (ui.selected >= 2 && ui.selectedNode() == nullptr)
            ui.selected = -1;
        rebuild();
        return;
    }
    // rebuild the Input view when the source changes
    static int lastSource = -1;
    if (ui.selected == Graph::kInputId && (int) ui.proc.source() != lastSource)
    {
        lastSource = (int) ui.proc.source();
        rebuild();
        return;
    }
    if (relevanceKey() != builtRelevance)
    {
        rebuild();
        return;
    }
    for (auto* k : knobs) k->sync();
    for (auto* k : nodeKnobs) k->sync();
    for (auto* c : choices) c->sync();
    for (auto* r : rows) r->sync();
    if (waveform) waveform->repaint();
    for (auto* c : extras)
        if (c->getName() == "outinfo")
            if (auto* l = dynamic_cast<juce::Label*> (c))
                l->setText ("LISTENING   " + juce::String (ui.proc.isBypassed() ? "B (DRY)" : "A (PROCESSED)")
                                + "\nA - B LOUDNESS   " + formatValue (ui.proc.gainMatchDb(), "dB").toUpperCase()
                                + "\nBRIDGE LATENCY   " + juce::String (ui.proc.currentLatency()) + " SMP ("
                                + juce::String (ui.proc.currentLatency() / ui.proc.currentSampleRate() * 1000.0, 1) + " MS)",
                            juce::dontSendNotification);
    if (! grHistory.empty())
        if (auto* n = ui.selectedNode())
        {
            grHistory.erase (grHistory.begin());
            grHistory.push_back (n->module->meter (0));
            repaint (grArea);
        }
}

void InspectorPanel::resized()
{
    auto r = getLocalBounds().reduced (16, 0);
    auto head = r.removeFromTop (40);
    advancedBtn.setBounds (head.removeFromRight (78).withSizeKeepingCentre (78, 24));
    simpleBtn.setBounds (head.removeFromRight (62).withSizeKeepingCentre (62, 24));

    r.removeFromTop (8);
    if (description.isNotEmpty())
        r.removeFromTop (description.length() > 90 ? (description.length() > 160 ? 70 : 52) : 36);

    auto bottom = r.removeFromBottom (buttons.isEmpty() ? 0 : (buttons.size() > 2 ? 72 : 34));
    if (! buttons.isEmpty())
    {
        auto row = bottom.removeFromTop (30);
        const int w = (row.getWidth() - 6) / 2;
        buttons[0]->setBounds (row.removeFromLeft (w));
        row.removeFromLeft (6);
        if (buttons.size() > 1) buttons[1]->setBounds (row);
        if (buttons.size() > 2) buttons[2]->setBounds (bottom.withTrimmedTop (8));
        r.removeFromBottom (12);
    }

    if (viewport.isVisible())
    {
        viewport.setBounds (r);
        return;
    }

    // SIMPLE layout: choices, knobs grid, GR graph
    for (auto* c : choices)
    {
        c->setBounds (r.removeFromTop (28));
        r.removeFromTop (8);
    }
    if (! grHistory.empty())
        grArea = r.removeFromBottom (56).withTrimmedTop (4);
    const int total = knobs.size() + nodeKnobs.size();
    const int cols = total <= 4 ? juce::jmax (1, total) : 3;
    const int kw = r.getWidth() / cols;
    const int kh = juce::jmin (108, r.getHeight() / juce::jmax (1, (total + cols - 1) / cols));
    int i = 0;
    auto place = [&] (juce::Component& c)
    {
        c.setBounds (r.getX() + (i % cols) * kw, r.getY() + (i / cols) * kh, kw, kh);
        ++i;
    };
    for (auto* k : knobs) place (*k);
    for (auto* k : nodeKnobs) place (*k);
}

void InspectorPanel::paint (juce::Graphics& g)
{
    theme::drawPanel (g, getLocalBounds());
    theme::columnRule (g, getLocalBounds());
    auto r = getLocalBounds().reduced (16, 0);
    g.setColour (theme::ink);
    const auto tf = theme::heading (25.0f);
    g.setFont (tf);
    auto tr = r.withHeight (42).withTrimmedRight (150).translated (0, titleNote.isNotEmpty() ? 5 : 0);
    g.drawText (title, tr, juce::Justification::centredLeft, true);
    if (titleNote.isNotEmpty())
    {
        g.setColour (theme::textFaint);
        g.setFont (theme::mono (9.5f));
        g.drawText (titleNote, getLocalBounds().withHeight (14).withTrimmedLeft (16).translated (0, 5), juce::Justification::centredLeft);
    }

    if (description.isNotEmpty())
    {
        g.setColour (theme::textDim);
        g.setFont (theme::sans (12.5f));
        g.drawFittedText (description, r.withTrimmedTop (48).withHeight (description.length() > 160 ? 66 : 50), juce::Justification::topLeft, 5, 1.0f);
    }

    if (! grHistory.empty() && ! grArea.isEmpty())
    {
        g.setColour (theme::textFaint);
        g.setFont (theme::mono (9.5f));
        g.drawText ("GAIN REDUCTION", grArea.withHeight (12), juce::Justification::centredLeft);
        g.setColour (theme::oxblood);
        g.drawText (juce::String (grHistory.back(), 1) + " DB", grArea.withHeight (12), juce::Justification::centredRight);
        auto a = grArea.withTrimmedTop (16).toFloat();
        g.setColour (theme::sheet);
        g.fillRect (a);
        g.setColour (theme::edge);
        g.drawRect (a, 1.0f);
        juce::Path p;
        for (size_t k = 0; k < grHistory.size(); ++k)
        {
            const float x = a.getX() + a.getWidth() * (float) k / (float) (grHistory.size() - 1);
            const float yy = a.getY() + juce::jlimit (0.0f, 1.0f, -grHistory[k] / 20.0f) * a.getHeight();
            if (k == 0) p.startNewSubPath (x, yy); else p.lineTo (x, yy);
        }
        g.setColour (theme::oxblood);
        g.strokePath (p, juce::PathStrokeType (1.3f));
    }
}
