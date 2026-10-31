#include "MainPanels.h"
#include "Templates.h"

#include <sonic/modules/ParametricEqModule.h>

// ================================================================== HEADER

HeaderBar::HeaderBar (Ui& u) : ui (u)
{
    auto& st = ui.proc.apvts;
    bypass = st.getParameter ("chain.bypass");

    study.setText (ui.proc.projectName(), juce::dontSendNotification);
    study.setFont (theme::heading (21.0f));
    study.setColour (juce::Label::textColourId, theme::text);
    study.setEditable (false, true, false);
    study.setTooltip ("Double-click to rename this study. The name is used for the blueprint export.");
    study.onTextChange = [this] { ui.proc.setProjectName (study.getText().trim().isEmpty() ? "Untitled study" : study.getText().trim()); };
    addAndMakeVisible (study);

    aButton.setTooltip ("A: hear the processed signal (the bridge).");
    bButton.setTooltip ("B: hear the unprocessed signal, time-aligned with A.");
    aButton.onClick = [this] { bypass->setValueNotifyingHost (0.0f); };
    bButton.onClick = [this] { bypass->setValueNotifyingHost (1.0f); };
    matchButton.setClickingTogglesState (true);
    matchButton.setTooltip ("Gain match: B is played at the same loudness as A, so you compare tone, not volume.");
    matchAtt = std::make_unique<ButtonAttachment> (st, "ab.gain_match", matchButton);
    undoButton.onClick = [this] { if (ui.proc.undo() && ui.onBridgeChanged) ui.onBridgeChanged(); };
    redoButton.onClick = [this] { if (ui.proc.redo() && ui.onBridgeChanged) ui.onBridgeChanged(); };
    undoButton.setTooltip ("Undo the last change to the bridge.");
    redoButton.setTooltip ("Redo.");
    exportButton.getProperties().set ("beige", true);
    exportButton.setTooltip ("Export a complete engineering blueprint of this sound (document, JSON, graphs, measurements, audio).");
    exportButton.onClick = [this] { if (onExport) onExport(); };

    navButton.setTooltip ("Back to the Bridge (Esc).");
    navButton.onClick = [this] { if (ui.backToBridge) ui.backToBridge(); };
    templatesButton.getProperties().set ("colour", (int) theme::accent.getARGB());
    templatesButton.setTooltip ("Start from a template (hardware styles, mixing chains, effects), or save / delete your own.");
    templatesButton.onClick = [this] { showTemplateMenu(); };
    templatesButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("TEMPLATES \xe2\x96\xbe")));
    navButton.getProperties().set ("link", true);
    exportButton.setButtonText ("Export blueprint");

    for (auto* b : { &aButton, &bButton, &matchButton, &undoButton, &redoButton, &exportButton, &navButton, &templatesButton })
        addAndMakeVisible (b);
    startTimerHz (6);
}

void HeaderBar::loadTemplate (const juce::ValueTree& bridge, const juce::String& name)
{
    if (! bridge.isValid()) return;
    ui.proc.checkpoint();
    ui.proc.bridgeFromTree (bridge);
    ui.proc.checkpoint();
    ui.proc.setProjectName (name);
    ui.select (-1);
    if (ui.backToBridge) ui.backToBridge();
    if (ui.onBridgeChanged) ui.onBridgeChanged();
    if (ui.onTemplateLoaded) ui.onTemplateLoaded();
}

void HeaderBar::showTemplateMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader (theme::q ("templates"));
    const auto& fac = templates::factory();
    juce::StringArray cats;
    for (auto& t : fac) cats.addIfNotAlreadyThere (t.category);
    for (int c = 0; c < cats.size(); ++c)
    {
        juce::PopupMenu sub;
        for (int i = 0; i < (int) fac.size(); ++i)
            if (fac[(size_t) i].category == cats[c])
                sub.addItem (1000 + i, fac[(size_t) i].name);
        m.addSubMenu (cats[c], sub);
    }
    m.addSeparator();
    auto mine = templates::personal();
    juce::PopupMenu load, del;
    for (int i = 0; i < (int) mine.size(); ++i)
    {
        load.addItem (2000 + i, mine[(size_t) i].name + (mine[(size_t) i].category != "My templates" ? "  (" + mine[(size_t) i].category + ")" : juce::String()));
        del.addItem (juce::PopupMenu::Item (mine[(size_t) i].name).setID (3000 + i).setColour (theme::danger));
    }
    m.addSubMenu ("My templates", load, ! mine.empty());
    m.addItem (1, "Save this bridge as a template...");
    m.addSubMenu ("Delete one of my templates", del, ! mine.empty());
    m.addItem (2, "Show my templates folder");
    m.addSeparator();
    m.addItem (3, "Start empty (Input -> Output)");

    juce::Component::SafePointer<HeaderBar> safe (this);
    m.showMenuAsync (ui.menuOptions().withTargetComponent (&templatesButton), [safe, mine] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r >= 1000 && r < 2000)
        {
            const auto& t = templates::factory()[(size_t) (r - 1000)];
            safe->loadTemplate (templates::bridgeFor (t), t.name);
        }
        else if (r >= 2000 && r < 3000)
        {
            const auto& t = mine[(size_t) (r - 2000)];
            safe->loadTemplate (templates::bridgeFor (t), t.name);
        }
        else if (r >= 3000)
        {
            const auto t = mine[(size_t) (r - 3000)];
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Delete template",
                "Delete \"" + t.name + "\"? It is moved to the recycle bin.", "Delete", "Cancel", safe.getComponent(),
                juce::ModalCallbackFunction::create ([t] (int res) { if (res == 1) templates::remove (t); }));
        }
        else if (r == 1) safe->saveTemplateDialog();
        else if (r == 2) { templates::folder().createDirectory(); templates::folder().startAsProcess(); }
        else if (r == 3)
        {
            sonic::Graph empty;
            empty.resetToDefault();
            safe->loadTemplate (AtelierProcessor::graphToTree (empty), "Untitled study");
        }
    });
}

void HeaderBar::saveTemplateDialog()
{
    auto* w = new juce::AlertWindow ("Save as template", "Saves every block, wire and setting of this bridge.", juce::MessageBoxIconType::NoIcon);
    w->setLookAndFeel (&dialogLookAndFeel());
    w->addTextEditor ("name", ui.proc.projectName(), "Name");
    w->addTextEditor ("category", "My templates", "Category");
    w->addTextEditor ("description", ui.proc.objective(), "Description");
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<HeaderBar> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int res)
    {
        if (safe == nullptr || res != 1) return;
        juce::String err;
        if (! templates::save (w->getTextEditorContents ("name"), w->getTextEditorContents ("category"), w->getTextEditorContents ("description"),
                               safe->ui.proc.bridgeToTree(), err))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Template not saved", err, {}, safe.getComponent());
    }), true);
}

void HeaderBar::timerCallback()
{
    const bool b = bypass->getValue() > 0.5f;
    aButton.setToggleState (! b, juce::dontSendNotification);
    bButton.setToggleState (b, juce::dontSendNotification);
    matchButton.setButtonText (matchButton.getToggleState() ? "MATCH  ON" : "MATCH  OFF");
    undoButton.setEnabled (ui.proc.canUndo());
    redoButton.setEnabled (ui.proc.canRedo());
    if (! study.isBeingEdited() && study.getText() != ui.proc.projectName())
        study.setText (ui.proc.projectName(), juce::dontSendNotification);
    // navigation: where am I, and the way back
    juce::String loc = "OVERVIEW";
    if (ui.openEditorId >= 0)
        if (auto* n = ui.proc.graph().node (ui.openEditorId))
            loc = juce::String (n->name).toUpperCase();
    const bool away = ui.openEditorId >= 0;
    navButton.setButtonText (away ? juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x90 Bridge")) : juce::String ("Bridge"));
    navButton.getProperties().set ("colour", (int) (away ? theme::accent : theme::ink).getARGB());
    navButton.setToggleState (! away, juce::dontSendNotification);
    navButton.setSize (juce::GlyphArrangement::getStringWidthInt (theme::heading (20.0f), navButton.getButtonText()) + 6, navButton.getHeight());
    if (away) loc = ui.openEditorId >= 0 && ui.proc.graph().node (ui.openEditorId) ? juce::String (ui.proc.graph().node (ui.openEditorId)->name) : loc;
    if (loc != location) { location = loc; repaint(); }
    repaint (getLocalBounds().removeFromRight (176));
}

void HeaderBar::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
    g.setColour (theme::logo);
    g.setFont (theme::serif (46.0f));
    g.drawText ("Atelier", juce::Rectangle<int> (30, 8, 150, getHeight() - 12), juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (theme::mono (10.0f));
    g.drawText ("BY R4N", juce::Rectangle<int> (168, 36, 60, 16), juce::Justification::centredLeft);

    // separators
    g.setColour (theme::edge);
    g.drawVerticalLine (236, 22.0f, (float) getHeight() - 22.0f);
    g.drawVerticalLine (study.getX() - 10, 22.0f, (float) getHeight() - 22.0f);

    // where am I:  Bridge / OVERVIEW   or   <- Bridge / Air & body  ESC
    const int x = navButton.getRight() + 6;
    g.setColour (theme::textFaint);
    g.setFont (theme::mono (10.0f));
    g.drawText ("/", juce::Rectangle<int> (x, 0, 12, getHeight()), juce::Justification::centred);
    const int room = study.getX() - 22 - (x + 18);
    if (ui.openEditorId >= 0)
    {
        g.setColour (theme::ink);
        const auto f = theme::heading (20.0f);
        g.setFont (f);
        const int w = juce::jmin (room - 34, juce::GlyphArrangement::getStringWidthInt (f, location));
        g.drawText (location, juce::Rectangle<int> (x + 18, 0, w, getHeight()), juce::Justification::centredLeft, true);
        g.setColour (theme::textFaint);
        g.setFont (theme::mono (9.0f));
        g.drawText ("ESC", juce::Rectangle<int> (x + 24 + w, 2, 30, getHeight()), juce::Justification::centredLeft);
    }
    else
        g.drawText (location, juce::Rectangle<int> (x + 18, 0, room, getHeight()), juce::Justification::centredLeft, true);

    g.setColour (theme::textFaint);
    g.setFont (theme::mono (9.5f));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xc3\x89TUDE")), juce::Rectangle<int> (study.getX() + 2, 16, 120, 12), juce::Justification::centredLeft);

    // status (right)
    auto s = getLocalBounds().removeFromRight (176).reduced (24, 0);
    g.setColour (theme::textDim);
    g.setFont (theme::mono (9.5f));
    g.drawFittedText (juce::String (ui.proc.currentSampleRate() / 1000.0, 1) + " KHZ  " + juce::String (ui.proc.currentBlockSize())
                          + "\nLATENCY " + juce::String (ui.proc.currentLatency()) + " SMP",
                      s, juce::Justification::centredRight, 2);

    g.setColour (theme::ink);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void HeaderBar::resized()
{
    navButton.setBounds (250, 22, 96, 32);
    templatesButton.setBounds (690, 25, 112, 26);
    auto r = getLocalBounds().withTrimmedRight (176).reduced (0, 18);
    exportButton.setBounds (r.removeFromRight (178).withSizeKeepingCentre (178, 40));
    r.removeFromRight (18);
    redoButton.setBounds (r.removeFromRight (52).withSizeKeepingCentre (52, 26));
    r.removeFromRight (4);
    undoButton.setBounds (r.removeFromRight (52).withSizeKeepingCentre (52, 26));
    r.removeFromRight (18);
    matchButton.setBounds (r.removeFromRight (96).withSizeKeepingCentre (96, 26));
    bButton.setBounds (r.removeFromRight (78).withSizeKeepingCentre (78, 26));
    aButton.setBounds (r.removeFromRight (78).withSizeKeepingCentre (78, 26));
    templatesButton.setBounds (aButton.getX() - 136, 25, 112, 26);
    study.setBounds (560, 28, templatesButton.getX() - 576, 28);
}

// ================================================================== SPECTRUM

SpectrumPanel::SpectrumPanel (Ui& u) : ui (u)
{
    inOut.setClickingTogglesState (false);
    inOut.setToggleState (true, juce::dontSendNotification);
    inOut.onClick = [this] { showDiff = false; inOut.setToggleState (true, juce::dontSendNotification); diff.setToggleState (false, juce::dontSendNotification); repaint(); };
    diff.onClick = [this] { showDiff = true; inOut.setToggleState (false, juce::dontSendNotification); diff.setToggleState (true, juce::dontSendNotification); repaint(); };
    freeze.onClick = [this] { ui.frozen = ! ui.frozen; freeze.setToggleState (ui.frozen, juce::dontSendNotification); };
    inOut.setTooltip ("Show the input (grey) and output (blue) spectra.");
    diff.setTooltip ("Show output minus input: what the bridge does to the tone.");
    freeze.setTooltip ("Freeze the display.");
    for (auto* b : { &inOut, &diff, &freeze })
        addAndMakeVisible (b);

    const char* labels[] = { "24", "12", "6", "3", "1", "AUTO" };
    const double ranges[] = { 24, 12, 6, 3, 1, 0 };
    for (int i = 0; i < 6; ++i)
    {
        auto* b = scaleButtons.add (new juce::TextButton (i < 5 ? juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + labels[i] : labels[i]));
        b->setTooltip ("Vertical scale of the EQ curve (+-dB). Small ranges reveal fractions of a dB.");
        b->onClick = [this, i, ranges]
        {
            autoScale = ranges[i] == 0.0;
            if (! autoScale) eqRange = ranges[i];
            for (int k = 0; k < scaleButtons.size(); ++k)
                scaleButtons[k]->setToggleState (k == i, juce::dontSendNotification);
            repaint();
        };
        addAndMakeVisible (b);
    }
    scaleButtons[2]->setToggleState (true, juce::dontSendNotification);
}

juce::Rectangle<float> SpectrumPanel::plotArea() const
{
    return juce::Rectangle<float> (48.0f, 52.0f, (float) getWidth() - 60.0f, (float) getHeight() - 102.0f);
}

void SpectrumPanel::resized()
{
    auto r = getLocalBounds().withHeight (40).reduced (16, 8);
    freeze.setBounds (r.removeFromRight (70));
    r.removeFromRight (4);
    diff.setBounds (r.removeFromRight (96));
    r.removeFromRight (4);
    inOut.setBounds (r.removeFromRight (78));

    auto b = getLocalBounds().removeFromBottom (28).reduced (12, 4);
    for (int i = scaleButtons.size() - 1; i >= 0; --i)
    {
        scaleButtons[i]->setBounds (b.removeFromRight (i == 5 ? 46 : 36));
        b.removeFromRight (4);
    }
}

sonic::Graph::Node* SpectrumPanel::overlayEq()
{
    if (auto* n = ui.selectedNode(); n != nullptr && n->type == "tone.eq")
        return n;
    auto& g = ui.proc.graph();
    for (int id : g.activeOrder())
        if (auto* n = g.node (id); n != nullptr && n->type == "tone.eq")
            return n;
    return nullptr;
}

void SpectrumPanel::drawCurve (juce::Graphics& g, const std::vector<float>& s, juce::Rectangle<float> r, juce::Colour c, float w, bool fill)
{
    if (s.size() < 4)
        return;
    const double binHz = ui.res.sampleRate / ui.res.fftSize;
    const double dbMin = -96.0, dbMax = 0.0;
    juce::Path p;
    bool started = false;
    const int cols = (int) r.getWidth();
    const double fMax = juce::jmin (20000.0, ui.res.sampleRate * 0.5);
    float lastX = r.getX();
    for (int x = 0; x < cols; x += 2)
    {
        const double f0 = 20.0 * std::pow (1000.0, (double) x / cols);
        const double f1 = 20.0 * std::pow (1000.0, (double) (x + 2) / cols);
        if (f0 > fMax) break;
        const float v = plot::sampleSpectrum (s, binHz, f0, f1);
        const float y = plot::dbToY (juce::jlimit (dbMin - 6, dbMax, (double) v), r, dbMin, dbMax);
        lastX = r.getX() + (float) x;
        if (! started) { p.startNewSubPath (lastX, y); started = true; }
        else p.lineTo (lastX, y);
    }
    if (fill && started)
    {
        juce::Path a (p);
        a.lineTo (lastX, r.getBottom());
        a.lineTo (r.getX(), r.getBottom());
        a.closeSubPath();
        theme::hatch (g, a, theme::ink, 0.20f, 5.0f);
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else
    {
        juce::Path d;
        const float dash[] = { 2.0f, 2.0f };
        juce::PathStrokeType (w).createDashedStroke (d, p, dash, 2);
        g.setColour (c);
        g.fillPath (d);
    }
}

void SpectrumPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
    {
        auto h = getLocalBounds().withHeight (44).withTrimmedLeft (48);
        g.setColour (theme::ink);
        g.setFont (theme::heading (27.0f));
        g.drawText ("Spectrum", h, juce::Justification::centredLeft);
        g.setColour (theme::textFaint);
        g.setFont (theme::mono (10.0f));
        g.drawText (ui.frozen ? juce::String (juce::CharPointer_UTF8 ("FIG. 01 \xe2\x80\x94 FROZEN")) : juce::String (juce::CharPointer_UTF8 ("FIG. 01 \xe2\x80\x94 LIVE")),
                    h.withTrimmedLeft (124).translated (0, 3), juce::Justification::centredLeft);
    }
    auto r = plotArea();

    // legend: line samples drawn the way the curves are drawn
    {
        auto lg = getLocalBounds().withHeight (44).withTrimmedLeft (310).withTrimmedRight (300).translated (0, 2);
        g.setFont (theme::sans (12.0f));
        auto item = [&] (const juce::String& t, juce::Colour c, int style)
        {
            auto a = lg.removeFromLeft (104);
            const float y = (float) a.getCentreY();
            g.setColour (c);
            if (style == 1) for (float x = 0; x < 18; x += 4) g.fillRect ((float) a.getX() + x, y, 2.0f, 1.0f);
            else g.fillRect ((float) a.getX(), y - (style == 2 ? 1.0f : 0.5f), 18.0f, style == 2 ? 2.0f : 1.5f);
            g.setColour (style == 2 ? theme::accent : (style == 1 ? theme::textDim : theme::ink));
            g.drawText (t, a.withTrimmedLeft (25), juce::Justification::centredLeft);
        };
        if (showDiff) item ("Out - In", theme::ink, 0);
        else { item ("Input", theme::reference, 1); item ("Output", theme::ink, 0); }
        item ("Equaliser", theme::accent, 2);
    }

    // the sheet, its rule border, the ruler and the dB scale
    g.setColour (theme::sheet);
    g.fillRect (r);
    for (double f : { 100.0, 1000.0, 10000.0 })
    {
        g.setColour (theme::edge);
        g.drawVerticalLine ((int) plot::freqToX (f, r), r.getY(), r.getBottom());
    }
    theme::frequencyRuler (g, r, { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 }, false);
    g.setFont (theme::mono (9.5f));
    const double dbMin = showDiff ? -24.0 : -96.0, dbMax = showDiff ? 24.0 : 0.0;
    for (int i = 0; i <= 8; ++i)
    {
        const double db = dbMax - i * (dbMax - dbMin) / 8.0;
        const float y = plot::dbToY (db, r, dbMin, dbMax);
        g.setColour (theme::grid);
        if (i > 0 && i < 8)
            for (float x = r.getX(); x < r.getRight(); x += 4.0f) g.fillRect (x, y, 1.0f, 1.0f);
        g.setColour (theme::ink);
        g.drawLine (r.getX() - (i % 2 == 0 ? 7.0f : 4.0f), y, r.getX(), y, 0.8f);
        if (i % 2 == 0)
        {
            g.setColour (theme::textDim);
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (0.0f, y - 6.0f, r.getX() - 10.0f, 12.0f), juce::Justification::centredRight);
        }
    }

    g.saveState();
    g.reduceClipRegion (r.toNearestInt());
    if (showDiff)
    {
        std::vector<float> d (ui.res.spectrumY.size());
        for (size_t k = 0; k < d.size() && k < ui.res.spectrumX.size(); ++k)
            d[k] = ui.res.spectrumY[k] - ui.res.spectrumX[k] - 48.0f; // shift so 0 dB sits at -48 in -96..0 drawing
        // draw on a +-24 scale: map via a temporary area
        const double binHz = ui.res.sampleRate / juce::jmax (1, ui.res.fftSize);
        juce::Path p;
        bool st = false;
        for (int x = 0; x < (int) r.getWidth(); x += 3)
        {
            const double f = 20.0 * std::pow (1000.0, x / r.getWidth());
            const int k = (int) (f / binHz);
            if (k < 1 || k >= (int) d.size()) continue;
            const bool valid = ui.res.spectrumX[(size_t) k] > -110.0f;
            if (! valid) { st = false; continue; }
            const float y = plot::dbToY (juce::jlimit (-24.0, 24.0, (double) d[(size_t) k] + 48.0), r, -24.0, 24.0);
            if (! st) { p.startNewSubPath (r.getX() + x, y); st = true; } else p.lineTo (r.getX() + x, y);
        }
        g.setColour (theme::ink);
        g.strokePath (p, juce::PathStrokeType (1.4f));
    }
    else
    {
        drawCurve (g, ui.res.spectrumX, r, theme::reference, 1.0f, false);
        drawCurve (g, ui.res.spectrumY, r, theme::ink, 1.4f, true);
    }

    // EQ overlay
    if (auto* n = overlayEq())
    {
        auto& eq = static_cast<sonic::ParametricEqModule&> (*n->module);
        const int N = (int) r.getWidth() / 2;
        std::vector<double> db ((size_t) N + 1);
        double maxAbs = 0.1;
        for (int i = 0; i <= N; ++i)
        {
            db[(size_t) i] = eq.responseDb (20.0 * std::pow (1000.0, (double) i / N));
            maxAbs = juce::jmax (maxAbs, std::abs (db[(size_t) i]));
        }
        const double range = autoScale ? juce::jlimit (0.5, 30.0, maxAbs * 1.25) : eqRange;
        juce::Path p;
        for (int i = 0; i <= N; ++i)
        {
            const float x = r.getX() + r.getWidth() * (float) i / (float) N;
            const float y = plot::dbToY (juce::jlimit (-range * 1.05, range * 1.05, db[(size_t) i]), r, -range, range);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        const float y0 = plot::dbToY (0.0, r, -range, range);
        g.setColour (theme::logo);
        for (float x = r.getX(); x < r.getRight(); x += 10.0f)
            g.drawLine (x, y0, x + 6.0f, y0, 0.8f);
        g.setColour (theme::accent);
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        for (int b = 0; b < sonic::ParametricEqModule::kMaxBands; ++b)
        {
            if (! eq.bandOn (b)) continue;
            const double f = eq.getParam (sonic::ParametricEqModule::bandParam (b, sonic::ParametricEqModule::fFreq));
            const float x = plot::freqToX (f, r);
            const float y = plot::dbToY (juce::jlimit (-range, range, eq.responseDb (f)), r, -range, range);
            theme::numberedHandle (g, { x, y }, b + 1, false, theme::accent, 8.0f);
        }
        g.restoreState();
        g.setColour (theme::accent);
        g.setFont (theme::mono (9.5f));
        g.drawText ("EQ: " + juce::String (n->name).toUpperCase() + "  " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb1"))
                        + numText (range, range < 2 ? 1 : 0) + " dB",
                    getLocalBounds().removeFromBottom (28).withTrimmedRight (300).reduced (48, 0), juce::Justification::centredRight);
    }
    else
        g.restoreState();

    // hover readout
    if (r.contains (hover))
    {
        g.setColour (theme::oxblood);
        g.drawLine (hover.x, r.getY(), hover.x, r.getBottom(), 0.8f);
        const double f = plot::xToFreq (hover.x, r);
        const double binHz = ui.res.sampleRate / juce::jmax (1, ui.res.fftSize);
        const int k = (int) std::lround (f / binHz);
        juce::String t = formatValue (f, "Hz") + "  " + plot::noteName (f);
        if (k > 0 && k < (int) ui.res.spectrumY.size())
            t << "\nOUT " << juce::String (ui.res.spectrumY[(size_t) k], 1) << "  IN " << juce::String (ui.res.spectrumX[(size_t) k], 1) << " dBFS";
        auto box = juce::Rectangle<float> (hover.x + 12, r.getY() + 20, 178, 38);
        if (box.getRight() > r.getRight()) box.setX (hover.x - 190);
        g.setColour (theme::card);
        g.fillRect (box);
        g.setColour (theme::ink);
        g.drawRect (box, 1.0f);
        g.setFont (theme::mono (10.5f));
        g.drawFittedText (t, box.reduced (8, 4).toNearestInt(), juce::Justification::centredLeft, 2);
    }

    g.setColour (theme::ink);
    g.drawRect (r.expanded (0.5f), 1.0f);

    // footnote with conditions
    g.setColour (theme::textFaint);
    g.setFont (theme::mono (9.5f));
    const juce::String mode = ui.res.mode == AnalysisWorker::Mode::chain ? "IN = BRIDGE INPUT, OUT = BRIDGE OUTPUT" : "IN = SIDECHAIN, OUT = MAIN INPUT";
    g.drawText ("FFT " + juce::String (ui.res.fftSize) + " - " + juce::String (sonic::windowName (ui.res.window)).toUpperCase()
                    + " - LEFT CH - " + mode + " - MEASURED",
                getLocalBounds().removeFromBottom (28).withTrimmedLeft (48), juce::Justification::centredLeft);
    theme::cropMarks (g, getLocalBounds());
}

void SpectrumPanel::mouseMove (const juce::MouseEvent& e) { hover = e.position; repaint(); }
void SpectrumPanel::mouseExit (const juce::MouseEvent&) { hover = { -1, -1 }; repaint(); }

// ================================================================== READOUT

ReadoutPanel::ReadoutPanel (Ui& u) : ui (u)
{
    chainMode.setToggleState (true, juce::dontSendNotification);
    chainMode.setTooltip ("Measure Atelier's own bridge: input vs output.");
    refMode.setTooltip ("Measure a plugin placed before Atelier: sidechain (unprocessed) vs main input (processed).");
    chainMode.onClick = [this] { ui.worker.setMode (AnalysisWorker::Mode::chain); chainMode.setToggleState (true, juce::dontSendNotification); refMode.setToggleState (false, juce::dontSendNotification); };
    refMode.onClick = [this] { ui.worker.setMode (AnalysisWorker::Mode::reference); chainMode.setToggleState (false, juce::dontSendNotification); refMode.setToggleState (true, juce::dontSendNotification); };
    addAndMakeVisible (chainMode);
    addAndMakeVisible (refMode);
}

void ReadoutPanel::resized()
{
    auto r = getLocalBounds().withHeight (44).reduced (0, 10);
    refMode.setBounds (r.removeFromRight (86));
    chainMode.setBounds (r.removeFromRight (60));
}

void ReadoutPanel::HarmonicsView::update (const AnalysisWorker::Results& r, double now)
{
    if (r.serial == lastSerial) return;
    lastSerial = r.serial;
    const bool valid = r.harmonicsAvailable && r.harmonics.levelDbc.size() > 1;
    if (valid)
    {
        ++validStreak;
        lastValidMs = now;
        invalidSinceMs = 0.0;
        // a new fundamental (> 3 %) restarts the smoothing instead of sliding across
        const bool jump = f0 <= 0.0 || std::abs (r.harmonics.fundamentalHz / f0 - 1.0) > 0.03;
        if (validStreak >= 3 || showing)
        {
            const double k = (jump || ! showing || held) ? 0.0 : 0.75;   // ~0.5 s smoothing at 8 updates/s
            f0 = jump ? r.harmonics.fundamentalHz : k * f0 + (1.0 - k) * r.harmonics.fundamentalHz;
            thdPct = k * thdPct + (1.0 - k) * r.harmonics.thdPercent;
            for (size_t i = 0; i < dbc.size(); ++i)
            {
                const double v = r.harmonics.levelDbc.size() > i + 1 ? juce::jmax (-120.0, r.harmonics.levelDbc[i + 1]) : -120.0;
                dbc[i] = k * dbc[i] + (1.0 - k) * v;
            }
            estimated = r.f0Estimated;
            showing = true;
            held = false;
        }
    }
    else
    {
        validStreak = 0;
        if (invalidSinceMs <= 0.0) invalidSinceMs = now;
        if (showing)
        {
            held = true;
            if (now - lastValidMs > 4000.0) { showing = false; held = false; f0 = 0.0; }   // hold the last reading for 4 s
        }
        // only change the message after it has been true for a moment
        if (now - invalidSinceMs > 800.0 || note.isEmpty())
            note = r.harmonicsNote.isNotEmpty() ? r.harmonicsNote : juce::String ("Play a steady tone (or the generator sine) to read harmonics.");
    }
}

void ReadoutPanel::paint (juce::Graphics& g)
{
    hv.update (ui.res, juce::Time::getMillisecondCounterHiRes());
    g.fillAll (theme::bg);
    theme::columnRule (g, getLocalBounds().withTrimmedTop (8));
    g.setColour (theme::ink);
    g.setFont (theme::heading (27.0f));
    g.drawText ("Readout", getLocalBounds().withHeight (44).withTrimmedLeft (22), juce::Justification::centredLeft);
    const auto& res = ui.res;
    auto r = getLocalBounds().withTrimmedLeft (22).withTrimmedTop (52);

    auto cell = [&] (juce::Rectangle<int> c, const juce::String& label, const juce::String& value, const juce::String& unit, const juce::String& tip = {})
    {
        juce::ignoreUnused (tip);
        auto cc = c.withTrimmedRight (14);
        g.setColour (theme::textFaint);
        g.setFont (theme::mono (9.0f));
        g.drawText (label, cc.removeFromTop (14), juce::Justification::centredLeft);
        const bool reduction = label == "REDUCTION" && value != "0.0";
        g.setColour (reduction ? theme::oxblood : theme::ink);
        g.setFont (theme::numeral (30.0f));
        const int w = juce::GlyphArrangement::getStringWidthInt (theme::numeral (30.0f), value);
        g.drawText (value, cc.removeFromTop (30), juce::Justification::centredLeft);
        g.setColour (theme::textDim);
        g.setFont (theme::mono (9.5f));
        g.drawText (unit.toUpperCase(), juce::Rectangle<int> (cc.getX() + w + 6, cc.getY() - 16, 60, 12), juce::Justification::centredLeft);
        g.setColour (theme::edge);
        g.drawHorizontalLine (c.getBottom() - 1, (float) c.getX(), (float) c.getRight() - 14.0f);
    };
    auto fmt = [] (double v, int dp) { return v < -150.0 ? juce::String ("-inf") : juce::String (v, dp); };

    const int cw = r.getWidth() / 2;
    auto row = r.removeFromTop (52);
    cell (row.removeFromLeft (cw), "LOUDNESS", fmt (res.lufsShort, 1), "LUFS-S");
    cell (row, "TRUE PEAK", fmt (res.truePeakDb, 1), "dBTP");
    r.removeFromTop (6);
    row = r.removeFromTop (52);
    cell (row.removeFromLeft (cw), "CREST", res.yHasSignal ? juce::String (res.crestDb, 1) : "-", "dB");
    cell (row, "REDUCTION", juce::String (ui.totalGainReduction(), 1), "dB");
    r.removeFromTop (6);
    row = r.removeFromTop (52);
    cell (row.removeFromLeft (cw), "STEREO CORR.", res.stereoValid ? juce::String (res.correlation, 2) : "-", "");
    cell (row, "THD", hv.showing ? juce::String (hv.thdPct, hv.thdPct < 1 ? 3 : 2) : "-", "%");

    r.removeFromTop (14);

    // harmonics
    auto head = r.removeFromTop (16);
    g.setColour (theme::textDim);
    g.setFont (theme::mono (9.5f));
    g.drawText ("HARMONICS" + (hv.showing ? juce::String (" - F0 ") + formatValue (hv.f0, "Hz").toUpperCase() : juce::String()),
                head, juce::Justification::centredLeft);
    if (hv.showing)
        theme::drawTag (g, head.removeFromRight (96).withTrimmedRight (14), hv.held ? "HELD" : (hv.estimated ? "ESTIMATED" : "MEASURED"),
                        hv.held ? theme::textFaint : theme::accent, theme::edge, theme::bg);
    r.removeFromTop (8);
    auto bars = r.removeFromTop (92).withTrimmedRight (14);
    g.setColour (theme::ink);
    g.drawHorizontalLine (bars.getBottom(), (float) bars.getX(), (float) bars.getRight());
    const int nb = 6;
    const int bw = (bars.getWidth() - (nb - 1) * 6) / nb;
    for (int i = 0; i < nb; ++i)
    {
        auto b = juce::Rectangle<int> (bars.getX() + i * (bw + 6), bars.getY(), bw, bars.getHeight());
        const double dbc = hv.showing ? hv.dbc[(size_t) i] : -120.0;
        const float h = (float) juce::jlimit (0.0, 1.0, (dbc + 100.0) / 100.0) * (float) b.getHeight();
        // even harmonics solid ink, odd ones outlined
        const auto bar = juce::Rectangle<float> ((float) b.getX() + b.getWidth() * 0.2f, (float) b.getBottom() - h, b.getWidth() * 0.6f, h);
        const auto inkC = theme::ink.withMultipliedAlpha (hv.held ? 0.4f : 1.0f);
        if (i % 2 == 0) { g.setColour (inkC); g.fillRect (bar); }
        else { g.setColour (theme::card); g.fillRect (bar); g.setColour (inkC); g.drawRect (bar, 1.0f); }
        g.setColour (theme::textDim);
        g.setFont (theme::heading (15.0f));
        g.drawText ("H" + juce::String (i + 2), b.translated (0, b.getHeight() + 2).withHeight (16), juce::Justification::centred);
    }
    r.removeFromTop (24);
    g.setColour (theme::textFaint);
    g.setFont (theme::sans (11.5f));
    juce::String note = hv.showing && ! hv.held
        ? (hv.estimated ? juce::String ("f0 estimated from the spectrum peak: valid for a single steady tone.")
                        : juce::String ("Exact: fundamental known from the generator."))
        : (hv.held ? juce::String ("Holding the last stable reading. ") + hv.note : hv.note);
    if (res.mode == AnalysisWorker::Mode::reference)
    {
        const auto sc = ui.proc.sidechainState();
        note = sc == AtelierProcessor::SidechainState::notConnected ? "Sidechain: NOT CONNECTED - only the processed signal is analysed."
             : sc == AtelierProcessor::SidechainState::silent       ? "Sidechain: SILENT - connected, but no audio is arriving."
                                                                    : "Sidechain: ACTIVE - input/output comparison is valid.";
    }
    g.drawFittedText (note, r.removeFromTop (34).withTrimmedRight (14), juce::Justification::topLeft, 2);
}
