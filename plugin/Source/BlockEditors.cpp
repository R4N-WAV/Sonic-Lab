#include "BlockEditors.h"

#include <sonic/Analysis.h>

namespace
{
    const char* kShapeShort[] = { "BELL", "LO SHELF", "HI SHELF", "LO CUT", "HI CUT", "NOTCH", "BAND PASS", "TILT" };
    constexpr int kRowH = 27, kHeaderH = 22;

    void dashedRect (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
    {
        juce::Path o, d;
        o.addRectangle (r);
        const float dash[] = { 4.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (d, o, dash, 2);
        g.setColour (c);
        g.fillPath (d);
    }
}

// ================================================================== NodeRow

NodeRow::NodeRow (Ui& u, int nodeId, bool isMix) : ui (u), id (nodeId), mix (isMix)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    if (mix) { slider.setRange (0.0, 100.0, 0.0); slider.setTextValueSuffix (" %"); }
    else { slider.setRange (-24.0, 24.0, 0.0); slider.setTextValueSuffix (" dB"); }
    if (auto* n = ui.proc.graph().node (id))
        slider.setDefault (mix ? n->module->defaultMix() * 100.0 : 0.0);
    slider.valueArea = [this] { return getLocalBounds().removeFromRight (valueW) - slider.getPosition(); };
    const auto tip = mix ? help::tooltip ("Mix", { "Blend between the sound going into this block (0 %) and the block's result (100 %). Time-aligned, so no phasing.",
                                                   "more of the block's effect", "more of the untouched sound" })
                         : help::tooltip ("Block output", { "Level after this block (after Mix).", "louder", "quieter" });
    setTooltip (tip + "\n\nDouble-click: reset.  Right-click or click the number: type a value.");
    slider.setTooltip (getTooltip());
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

void NodeRow::sync()
{
    if (slider.isMouseButtonDown()) return;
    if (auto* n = ui.proc.graph().node (id))
    {
        const double v = mix ? n->mix.load() * 100.0 : n->outGainDb.load();
        if (std::abs (v - slider.getValue()) > 1e-6)
        {
            slider.setValue (v, juce::dontSendNotification);
            repaint();
        }
    }
}

void NodeRow::resized()
{
    slider.setBounds (getLocalBounds().withTrimmedLeft (labelW).withTrimmedRight (valueW + 4));
}

void NodeRow::paint (juce::Graphics& g)
{
    g.setColour (theme::textDim);
    g.setFont (theme::sans (12.5f));
    g.drawText (mix ? "Mix" : "Block output", getLocalBounds().withWidth (labelW - 6), juce::Justification::centredLeft);
    g.setColour (theme::text);
    g.setFont (theme::mono (10.5f));
    g.drawText (mix ? juce::String (juce::roundToInt (slider.getValue())) + " %" : formatValue (slider.getValue(), "dB"),
                getLocalBounds().removeFromRight (valueW), juce::Justification::centredRight);
}

void NodeRow::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || getLocalBounds().removeFromRight (valueW).contains (e.getPosition()))
        slider.beginTextEntry();
}

void NodeRow::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! getLocalBounds().removeFromRight (valueW).contains (e.getPosition()))
        slider.resetToDefault();
}

// ================================================================== ControlsPanel

namespace
{
    struct HeaderLayer : public juce::Component
    {
        std::vector<std::pair<juce::Rectangle<int>, juce::String>>* list = nullptr;
        void paint (juce::Graphics& g) override
        {
            if (list == nullptr) return;
            for (auto& [r, t] : *list)
            {
                g.setColour (theme::accent);
                g.setFont (theme::mono (9.5f));
                g.drawText (t.toUpperCase(), r.withTrimmedBottom (4), juce::Justification::bottomLeft);
                g.setColour (theme::edge);
                g.drawHorizontalLine (r.getBottom() - 2, (float) r.getX(), (float) r.getRight() - 10.0f);
            }
        }
    };
}

ControlsPanel::ControlsPanel (Ui& u, int nodeId, std::function<bool (int)> skip, bool withNodeRows) : ui (u)
{
    auto* layer = new HeaderLayer();
    layer->list = &headers;
    content.addAndMakeVisible (layer);
    content.getProperties().set ("layer", true);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (6);
    addAndMakeVisible (viewport);

    auto* n = ui.proc.graph().node (nodeId);
    if (n == nullptr) return;
    auto& m = *n->module;
    // rows grouped by section, sections in order of first appearance
    std::vector<juce::String> groups;
    for (int i = 0; i < m.numParams(); ++i)
        if (! (skip && skip (i)) && ! juce::StringArray (groups.data(), (int) groups.size()).contains (juce::String (m.params()[(size_t) i].group)))
            groups.push_back (juce::String (m.params()[(size_t) i].group));
    for (auto& grp : groups)
    {
        bool first = true;
        for (int i = 0; i < m.numParams(); ++i)
        {
            if ((skip && skip (i)) || juce::String (m.params()[(size_t) i].group) != grp) continue;
            auto* row = rows.add (new ModuleParamRow (ui.proc, m, i));
            content.addAndMakeVisible (row);
            items.push_back ({ first ? grp : juce::String(), row });
            first = false;
        }
    }
    if (withNodeRows)
    {
        auto* mixRow = nodeRows.add (new NodeRow (ui, nodeId, true));
        auto* outRow = nodeRows.add (new NodeRow (ui, nodeId, false));
        content.addAndMakeVisible (mixRow);
        content.addAndMakeVisible (outRow);
        items.push_back ({ "Block", mixRow });
        items.push_back ({ {}, outRow });
    }
    // the header layer is the first child; keep it under the rows
    layer->toBack();
}

void ControlsPanel::sync()
{
    for (auto* r : rows) r->sync();
    for (auto* r : nodeRows) r->sync();
}

void ControlsPanel::resized()
{
    viewport.setBounds (getLocalBounds());
    const int W = getWidth() - 8, H = getHeight();
    const int cols = juce::jlimit (1, 3, preferredColumns > 0 ? preferredColumns : W / 255);
    const int colW = W / cols;
    auto place = [&] (int colH, bool apply)
    {
        int col = 0, y = 0;
        if (apply) headers.clear();
        for (size_t k = 0; k < items.size(); ++k)
        {
            const auto& it = items[k];
            const int need = (it.header.isNotEmpty() ? kHeaderH + kRowH : kRowH);
            if (y + need > colH && y > 0)
            {
                if (++col >= cols) return false;
                y = 0;
            }
            if (it.header.isNotEmpty())
            {
                if (apply) headers.push_back ({ juce::Rectangle<int> (col * colW, y, colW, kHeaderH), it.header });
                y += kHeaderH;
            }
            if (apply) it.comp->setBounds (col * colW, y, colW - 14, kRowH - 3);
            y += kRowH;
        }
        return true;
    };
    int colH = H;
    while (! place (colH, false) && colH < 6000)
        colH += kRowH;
    place (colH, true);
    content.setSize (W, colH);
    for (auto* c : content.getChildren())
        if (dynamic_cast<HeaderLayer*> (c) != nullptr)
            c->setBounds (content.getLocalBounds());
    for (auto* r : rows) { r->labelW = colW > 300 ? 118 : 104; r->valueW = 68; r->resized(); }
    for (auto* r : nodeRows) { r->labelW = colW > 300 ? 118 : 104; r->valueW = 68; r->resized(); }
}

void ControlsPanel::paint (juce::Graphics&) {}

// ================================================================== GenericBody

GenericBody::GenericBody (Ui& u, int id, PreviewEngine& e) : figure (u, id, e), controls (u, id)
{
    addAndMakeVisible (figure);
    addAndMakeVisible (controls);
}

void GenericBody::resized()
{
    auto r = getLocalBounds();
    figure.setBounds (r.removeFromLeft ((int) (r.getWidth() * 0.47f)));
    r.removeFromLeft (28);
    controls.setBounds (r);
}

// ================================================================== EQ VIEW

EqView::EqView (Ui& u, int id) : nodeId (id), ui (u)
{
    setWantsKeyboardFocus (true);
    const char* labels[] = { "30", "24", "12", "6", "3", "1", "AUTO" };
    const double vals[] = { 30, 24, 12, 6, 3, 1, 0 };
    for (int i = 0; i < 7; ++i)
    {
        auto* b = rangeButtons.add (new juce::TextButton (i < 6 ? juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + labels[i] : labels[i]));
        b->getProperties().set ("small", true);
        b->setTooltip ("Vertical range of the display. Small ranges magnify fractions of a dB.");
        b->onClick = [this, i, vals]
        {
            autoRange = vals[i] == 0.0;
            if (! autoRange) fixedRange = vals[i];
            for (int k = 0; k < rangeButtons.size(); ++k) rangeButtons[k]->setToggleState (k == i, juce::dontSendNotification);
            repaint();
        };
        addAndMakeVisible (b);
    }
    rangeButtons[2]->setToggleState (true, juce::dontSendNotification);

    for (auto* t : { "BAND", "EQ", "MATCH" })
    {
        auto* b = tabs.add (new juce::TextButton (t));
        const int i = tabs.size() - 1;
        b->onClick = [this, i] { showTab (i); };
        addAndMakeVisible (b);
    }

    prevBand.onClick = [this]
    {
        for (int b = (selected < 0 ? EQ::kMaxBands : selected) - 1; b >= 0; --b)
            if (eq().bandOn (b)) { selectBand (b); return; }
    };
    nextBand.onClick = [this]
    {
        for (int b = selected + 1; b < EQ::kMaxBands; ++b)
            if (eq().bandOn (b)) { selectBand (b); return; }
    };
    addBand.onClick = [this] { addBandAt (1000.0, 0.0); };
    removeBand.onClick = [this] { deleteSelectedBands(); };
    addBand.setTooltip ("Add a bell at 1 kHz (or just click the curve).");
    removeBand.setTooltip ("Remove the selected band(s). Delete key works too.");
    for (auto* b : { &prevBand, &nextBand, &addBand, &removeBand })
        addChildComponent (b);

    eqControls = std::make_unique<ControlsPanel> (ui, nodeId, [] (int i) { return i >= 3; });
    eqControls->preferredColumns = 1;
    addChildComponent (*eqControls);
    clearAll.onClick = [this]
    {
        std::vector<int> all;
        for (int b = 0; b < EQ::kMaxBands; ++b) if (eq().bandOn (b)) all.push_back (b);
        removeBands (all);
    };
    clearAll.getProperties().set ("colour", (int) theme::danger.getARGB());
    addChildComponent (clearAll);

    refFile.onClick = [this] { loadReference(); };
    refCapture.setClickingTogglesState (true);
    learnButton.setClickingTogglesState (true);
    refCapture.onClick = [this]
    {
        learnButton.setToggleState (false, juce::dontSendNotification);
        capturing = refCapture.getToggleState() ? 1 : 0;
        if (capturing) { reference = {}; refName = "captured"; }
        updateMatchStatus();
    };
    learnButton.onClick = [this]
    {
        refCapture.setToggleState (false, juce::dontSendNotification);
        capturing = learnButton.getToggleState() ? 2 : 0;
        if (capturing) yours = {};
        updateMatchStatus();
    };
    applyButton.getProperties().set ("beige", true);
    applyButton.onClick = [this]
    {
        capturing = 0;
        refCapture.setToggleState (false, juce::dontSendNotification);
        learnButton.setToggleState (false, juce::dontSendNotification);
        applyMatch (yours, reference);
    };
    undoMatch.onClick = [this] { removeBands (matchBands); matchBands.clear(); lastFit = {}; updateMatchStatus(); };
    refFile.setTooltip ("Use a recording whose tonal balance you want (WAV, AIFF, FLAC).");
    refCapture.setTooltip ("Or play the reference through Atelier and capture it (click again to stop).");
    learnButton.setTooltip ("Play YOUR material through Atelier while this is on; Atelier averages its spectrum.");
    applyButton.setTooltip ("Replaces earlier match bands with bells that move your sound toward the reference.");
    for (auto* s : { &amount, &maxBands })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s->onValueChange = [this] { updateMatchStatus(); repaint(); };
    }
    amount.setRange (0, 100, 1);
    amount.setValue (60, juce::dontSendNotification);
    amount.setDefault (60);
    amount.setTooltip (help::tooltip ("Amount", { "How much of the difference the match corrects.", "closer to the reference", "subtler" }));
    maxBands.setRange (1, 12, 1);
    maxBands.setValue (6, juce::dontSendNotification);
    maxBands.setDefault (6);
    maxBands.setTooltip (help::tooltip ("Max bands", { "How many bells the match may use.", "finer detail", "broader, more musical strokes" }));
    for (juce::Component* c : { (juce::Component*) &refFile, (juce::Component*) &refCapture, (juce::Component*) &learnButton,
                                (juce::Component*) &applyButton, (juce::Component*) &undoMatch, (juce::Component*) &amount, (juce::Component*) &maxBands })
        addChildComponent (c);

    for (int b = 0; b < EQ::kMaxBands; ++b)
        if (eq().bandOn (b)) { selected = b; multi = { b }; break; }
    buildBandControls();
    showTab (0);
    startTimerHz (25);
}

sonic::Graph::Node* EqView::node() { return ui.proc.graph().node (nodeId); }
EqView::EQ& EqView::eq() { return static_cast<EQ&> (*node()->module); }

int EqView::numBands() const
{
    int n = 0;
    for (int b = 0; b < EQ::kMaxBands; ++b) if (const_cast<EqView*> (this)->eq().bandOn (b)) ++n;
    return n;
}

juce::Rectangle<int> EqView::side() const { return getLocalBounds().removeFromRight (344); }

juce::Rectangle<float> EqView::plot() const
{
    return getLocalBounds().withTrimmedRight (344 + 22).toFloat().withTrimmedLeft (42).withTrimmedTop (30).withTrimmedBottom (22);
}

double EqView::range() const
{
    if (! autoRange) return fixedRange;
    auto& m = const_cast<EqView*> (this)->eq();
    double mx = 0.25;
    for (int i = 0; i <= 120; ++i)
        mx = juce::jmax (mx, std::abs (m.responseDb (20.0 * std::pow (1000.0, i / 120.0)) - m.getParam (2)));
    return juce::jlimit (0.5, 30.0, mx * 1.3);
}

juce::Point<float> EqView::bandPoint (int b) const
{
    auto& m = const_cast<EqView*> (this)->eq();
    const auto shape = (EQ::Shape) (int) m.getParam (EQ::bandParam (b, EQ::fShape));
    const double f = m.getParam (EQ::bandParam (b, EQ::fFreq));
    const bool gainShape = shape == EQ::Shape::bell || shape == EQ::Shape::lowShelf || shape == EQ::Shape::highShelf || shape == EQ::Shape::tilt;
    double db = gainShape ? m.getParam (EQ::bandParam (b, EQ::fGain)) : 0.0;
    if (shape == EQ::Shape::lowShelf || shape == EQ::Shape::highShelf || shape == EQ::Shape::tilt) db *= 0.5;
    const double r = range();
    return { plot::freqToX (f, plot()), plot::dbToY (juce::jlimit (-r, r, db), plot(), -r, r) };
}

int EqView::bandAt (juce::Point<float> p) const
{
    auto& m = const_cast<EqView*> (this)->eq();
    for (int b = EQ::kMaxBands - 1; b >= 0; --b)
        if (m.bandOn (b) && bandPoint (b).getDistanceFrom (p) < 11.0f)
            return b;
    return -1;
}

void EqView::selectBand (int b, bool additive)
{
    if (! additive)
        multi.clear();
    if (b >= 0)
    {
        auto it = std::find (multi.begin(), multi.end(), b);
        if (additive && it != multi.end() && multi.size() > 1)
        {
            multi.erase (it);
            b = multi.front();
        }
        else if (it == multi.end())
            multi.push_back (b);
    }
    selected = b;
    buildBandControls();
    repaint();
}

void EqView::selectAllBands()
{
    multi.clear();
    for (int b = 0; b < EQ::kMaxBands; ++b) if (eq().bandOn (b)) multi.push_back (b);
    selected = multi.empty() ? -1 : multi.front();
    buildBandControls();
    repaint();
}

void EqView::addBandAt (double f, double gdb)
{
    const int nb = eq().firstFreeBand();
    if (nb < 0) return;
    int shape = 0;
    if (f < 30.0) shape = (int) EQ::Shape::lowCut;
    else if (f > 16000.0) shape = (int) EQ::Shape::highCut;
    auto& m = eq();
    for (int fld = 1; fld < EQ::kParamsPerBand; ++fld)   // a fresh band: defaults first
        m.setParam (EQ::bandParam (nb, fld), m.params()[(size_t) EQ::bandParam (nb, fld)].defaultValue);
    m.setParam (EQ::bandParam (nb, EQ::fShape), (float) shape);
    m.setParam (EQ::bandParam (nb, EQ::fFreq), (float) juce::jlimit (10.0, 22000.0, f));
    m.setParam (EQ::bandParam (nb, EQ::fGain), shape == 0 ? (float) juce::jlimit (-30.0, 30.0, gdb) : 0.0f);
    m.setParam (EQ::bandParam (nb, EQ::fQ), shape == 0 ? 1.0f : 0.7071f);
    m.setParam (EQ::bandParam (nb, EQ::fOn), 1.0f);
    ui.proc.checkpoint();
    selectBand (nb);
}

void EqView::removeBands (std::vector<int> list)
{
    if (list.empty()) return;
    for (int b : list)
        eq().setParam (EQ::bandParam (b, EQ::fOn), 0.0f);
    ui.proc.checkpoint();
    multi.clear();
    selected = -1;
    buildBandControls();
    repaint();
}

void EqView::deleteSelectedBands()
{
    auto list = multi;
    if (list.empty() && selected >= 0) list.push_back (selected);
    removeBands (list);
}

void EqView::showTab (int t)
{
    tab = t;
    for (int i = 0; i < tabs.size(); ++i) tabs[i]->setToggleState (i == t, juce::dontSendNotification);
    for (auto* b : { &prevBand, &nextBand, &addBand, &removeBand }) b->setVisible (t == 0);
    for (auto* r : bandRows) r->setVisible (t == 0);
    eqControls->setVisible (t == 1);
    clearAll.setVisible (t == 1);
    for (juce::Component* c : { (juce::Component*) &refFile, (juce::Component*) &refCapture, (juce::Component*) &learnButton,
                                (juce::Component*) &applyButton, (juce::Component*) &undoMatch, (juce::Component*) &amount, (juce::Component*) &maxBands })
        c->setVisible (t == 2);
    resized();
    repaint();
}

void EqView::buildBandControls()
{
    bandRows.clear();
    if (selected >= 0 && eq().bandOn (selected))
        for (int f = 1; f < EQ::kParamsPerBand; ++f)
        {
            auto* row = bandRows.add (new ModuleParamRow (ui.proc, eq(), EQ::bandParam (selected, f)));
            row->labelW = 96;
            addChildComponent (row);
            row->setVisible (tab == 0);
        }
    removeBand.setEnabled (selected >= 0 || ! multi.empty());
    addBand.setEnabled (eq().firstFreeBand() >= 0);
    resized();
}

void EqView::resized()
{
    // range buttons along the top of the plot
    auto pr = plot();
    int x = (int) pr.getRight();
    for (int i = rangeButtons.size() - 1; i >= 0; --i)
    {
        const int w = i == 6 ? 40 : 32;
        x -= w;
        rangeButtons[i]->setBounds (x, (int) pr.getY() - 26, w, 20);
        x -= 3;
    }

    auto s = side();
    auto tr = s.removeFromTop (26);
    const int tw = (tr.getWidth() - 8) / 3;
    for (auto* t : tabs) { t->setBounds (tr.removeFromLeft (tw)); tr.removeFromLeft (4); }
    s.removeFromTop (12);

    // BAND
    {
        auto r = s;
        auto line = r.removeFromTop (24);
        prevBand.setBounds (line.removeFromLeft (24));
        line.removeFromLeft (4);
        removeBand.setBounds (line.removeFromRight (74));
        line.removeFromRight (4);
        addBand.setBounds (line.removeFromRight (60));
        line.removeFromRight (4);
        nextBand.setBounds (line.removeFromRight (24));
        r.removeFromTop (8);
        for (auto* row : bandRows)
        {
            row->setBounds (r.removeFromTop (kRowH - 1));
        }
    }
    // EQ
    {
        auto r = s;
        clearAll.setBounds (r.removeFromBottom (26));
        r.removeFromBottom (8);
        eqControls->setBounds (r);
    }
    // MATCH
    {
        auto r = s.withTrimmedTop (18);
        auto row1 = r.removeFromTop (26);
        row1.removeFromLeft (92);
        refFile.setBounds (row1.removeFromLeft ((row1.getWidth() - 4) / 2));
        row1.removeFromLeft (4);
        refCapture.setBounds (row1);
        r.removeFromTop (8);
        auto row2 = r.removeFromTop (26);
        row2.removeFromLeft (92);
        learnButton.setBounds (row2);
        r.removeFromTop (14);
        amount.setBounds (r.removeFromTop (24).withTrimmedLeft (92).withTrimmedRight (52));
        r.removeFromTop (4);
        maxBands.setBounds (r.removeFromTop (24).withTrimmedLeft (92).withTrimmedRight (52));
        r.removeFromTop (12);
        auto row3 = r.removeFromTop (28);
        applyButton.setBounds (row3.removeFromLeft ((row3.getWidth() - 4) / 2));
        row3.removeFromLeft (4);
        undoMatch.setBounds (row3);
    }
}

void EqView::updateMatchStatus()
{
    lastFit = {};
    if (! reference.empty() && ! yours.empty())
        lastFit = sonic::eqmatch::fit (yours, reference, amount.getValue() * 0.01, (int) maxBands.getValue(), ui.proc.currentSampleRate());
    if (capturing == 1) matchStatus = "Capturing the reference: play it now. Click CAPTURE again to stop.";
    else if (capturing == 2) matchStatus = "Capturing your sound: play your material now. Click CAPTURE again to stop.";
    else if (reference.empty()) matchStatus = "1. Load or capture a reference.";
    else if (yours.empty()) matchStatus = "2. Capture your own material.";
    else if (lastFit.bands.empty()) matchStatus = "The two already match within 0.5 dB.";
    else matchStatus = "3. Apply: " + juce::String ((int) lastFit.bands.size()) + " bands. The dashed curve is the difference, the solid one what the bands do.";
    repaint();
}

bool EqView::applyMatch (const sonic::eqmatch::Spectrum& y, const sonic::eqmatch::Spectrum& ref)
{
    if (ref.empty() || y.empty())
    {
        matchStatus = ref.empty() ? "Load or capture a reference first." : "Capture your own material first.";
        repaint();
        return false;
    }
    yours = y;
    reference = ref;
    // replace earlier match bands
    for (int b : matchBands)
        eq().setParam (EQ::bandParam (b, EQ::fOn), 0.0f);
    matchBands.clear();
    lastFit = sonic::eqmatch::fit (yours, reference, amount.getValue() * 0.01, (int) maxBands.getValue(), ui.proc.currentSampleRate());
    auto& m = eq();
    for (auto& band : lastFit.bands)
    {
        const int b = m.firstFreeBand();
        if (b < 0) break;
        for (int fld = 1; fld < EQ::kParamsPerBand; ++fld)
            m.setParam (EQ::bandParam (b, fld), m.params()[(size_t) EQ::bandParam (b, fld)].defaultValue);
        m.setParam (EQ::bandParam (b, EQ::fShape), 0.0f);
        m.setParam (EQ::bandParam (b, EQ::fFreq), (float) band.freq);
        m.setParam (EQ::bandParam (b, EQ::fGain), (float) band.gainDb);
        m.setParam (EQ::bandParam (b, EQ::fQ), (float) band.q);
        m.setParam (EQ::bandParam (b, EQ::fOn), 1.0f);
        matchBands.push_back (b);
    }
    ui.proc.checkpoint();
    matchStatus = "Applied " + juce::String ((int) matchBands.size()) + " bands (earlier match bands replaced). Largest remaining difference "
                  + juce::String (lastFit.largestRemainingDb, 1) + " dB.";
    multi = matchBands;
    selected = matchBands.empty() ? -1 : matchBands.front();
    buildBandControls();
    repaint();
    return true;
}

void EqView::analysisUpdated()
{
    if (capturing != 0 && ui.res.serial != lastSerial && ui.res.xHasSignal && ui.res.spectrumX.size() > 8)
    {
        lastSerial = ui.res.serial;
        auto& dst = capturing == 1 ? reference : yours;
        const double binHz = ui.res.sampleRate / juce::jmax (1, ui.res.fftSize);
        if (dst.power.size() != ui.res.spectrumX.size() || std::abs (dst.binHz - binHz) > 1e-9)
        {
            dst = {};
            dst.binHz = binHz;
            dst.power.assign (ui.res.spectrumX.size(), 0.0);
        }
        for (size_t k = 0; k < dst.power.size(); ++k)
            dst.power[k] += std::pow (10.0, ui.res.spectrumX[k] / 10.0);
        ++dst.frames;
        if (dst.frames % 15 == 0) updateMatchStatus();
    }
    repaint (plot().toNearestInt().expanded (40, 30));
}

void EqView::sync()
{
    for (auto* r : bandRows) r->sync();
    eqControls->sync();
}

void EqView::timerCallback()
{
    if (node() == nullptr) return;
    if (eq().paramVersion() != lastVersion)
    {
        lastVersion = eq().paramVersion();
        if (selected >= 0 && ! eq().bandOn (selected)) { multi.clear(); selected = -1; buildBandControls(); }
        multi.erase (std::remove_if (multi.begin(), multi.end(), [this] (int b) { return ! eq().bandOn (b); }), multi.end());
        repaint();
    }
}

void EqView::paint (juce::Graphics& g)
{
    auto& m = eq();
    const auto pr = plot();
    const double r = range();

    // ---- dense grid ----
    g.setColour (theme::sheet);
    g.fillRect (pr);
    g.setFont (theme::mono (9.5f));
    for (double dec : { 10.0, 100.0, 1000.0, 10000.0 })
        for (int k = 1; k <= 9; ++k)
        {
            const double f = dec * k;
            if (f < 20.0 || f > 20000.0) continue;
            g.setColour (k == 1 ? theme::gridMajor : (k == 2 || k == 5 ? theme::gridMajor.withAlpha (0.6f) : theme::grid));
            g.drawVerticalLine ((int) plot::freqToX (f, pr), pr.getY(), pr.getBottom());
        }
    for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        const float x = plot::freqToX (f, pr);
        g.setColour (f == 100.0 || f == 1000.0 || f == 10000.0 ? theme::text : theme::textFaint);
        g.drawText (f >= 1000.0 ? numText (f / 1000.0, 0) + "k" : juce::String ((int) f),
                    juce::Rectangle<float> (x - 18.0f, pr.getBottom() + 5.0f, 36.0f, 12.0f), juce::Justification::centred);
    }
    const double major = r >= 24 ? 6.0 : r >= 12 ? 3.0 : r >= 6 ? 1.0 : r >= 3 ? 0.5 : r >= 1 ? 0.25 : 0.1;
    const double minor = major / (r >= 12 ? 3.0 : 2.0);
    for (double db = -std::floor (r / minor) * minor; db <= r + 1e-9; db += minor)
    {
        const float y = plot::dbToY (db, pr, -r, r);
        const bool isMajor = std::abs (std::remainder (db, major)) < 1e-6;
        g.setColour (std::abs (db) < 1e-9 ? theme::ink.withAlpha (0.6f) : (isMajor ? theme::gridMajor : theme::grid));
        g.drawHorizontalLine ((int) y, pr.getX(), pr.getRight());
        if (isMajor)
        {
            g.setColour (std::abs (db) < 1e-9 ? theme::text : theme::textFaint);
            g.drawText ((db > 0 ? "+" : "") + numText (db, major < 0.5 ? 2 : (major < 1 ? 1 : 0)),
                        juce::Rectangle<float> (pr.getX() - 42, y - 6, 36, 12), juce::Justification::centredRight);
        }
    }

    g.saveState();
    g.reduceClipRegion (pr.toNearestInt());
    // analyser (output) behind the curves
    if (ui.res.spectrumY.size() > 4)
    {
        const double binHz = ui.res.sampleRate / ui.res.fftSize;
        juce::Path line;
        for (int x = 0; x <= (int) pr.getWidth(); x += 2)
        {
            const double f = 20.0 * std::pow (1000.0, x / pr.getWidth());
            const double fn = 20.0 * std::pow (1000.0, (x + 2) / pr.getWidth());
            const float y = plot::dbToY (juce::jlimit (-100.0, 0.0, (double) plot::sampleSpectrum (ui.res.spectrumY, binHz, f, fn)), pr, -96.0, 0.0);
            if (x == 0) line.startNewSubPath (pr.getX(), y); else line.lineTo (pr.getX() + x, y);
        }
        juce::Path area (line);
        area.lineTo (pr.getRight(), pr.getBottom());
        area.lineTo (pr.getX(), pr.getBottom());
        area.closeSubPath();
        theme::hatch (g, area, theme::ink, 0.13f, 5.0f);
        g.setColour (theme::textFaint);
        g.strokePath (line, juce::PathStrokeType (1.0f));
    }

    const int N = (int) pr.getWidth() / 2;
    auto fAt = [&] (int i) { return 20.0 * std::pow (1000.0, (double) i / N); };
    auto xAt = [&] (int i) { return pr.getX() + pr.getWidth() * (float) i / (float) N; };
    auto yOf = [&] (double db) { return plot::dbToY (juce::jlimit (-r * 1.15, r * 1.15, db), pr, -r, r); };
    const float y0 = plot::dbToY (0.0, pr, -r, r);
    const double outDb = m.getParam (2);

    // selected bands shaded in camel
    for (int b : multi)
    {
        if (! m.bandOn (b)) continue;
        juce::Path band;
        band.startNewSubPath (pr.getX(), y0);
        for (int i = 0; i <= N; ++i)
            band.lineTo (xAt (i), yOf (m.bandResponseDb (b, fAt (i))));
        band.lineTo (pr.getRight(), y0);
        band.closeSubPath();
        g.setColour (theme::logo.withAlpha (b == selected ? 0.24f : 0.14f));
        g.fillPath (band);
    }

    // dynamic bands: the range they can move through (hatched) and where they are now
    std::vector<double> total ((size_t) N + 1);
    for (int i = 0; i <= N; ++i) total[(size_t) i] = m.responseDb (fAt (i)) - outDb;
    for (int b = 0; b < EQ::kMaxBands; ++b)
    {
        if (! m.bandOn (b) || m.getParam (EQ::bandParam (b, EQ::fDyn)) < 0.5f || ! m.isParamRelevant (EQ::bandParam (b, EQ::fRange))) continue;
        const double gFull = m.getParam (EQ::bandParam (b, EQ::fGain)) + m.getParam (EQ::bandParam (b, EQ::fRange));
        const double gNow = m.dynamicGainDb (b);
        juce::Path now, area;
        bool inside = false;
        std::vector<juce::Point<float>> topPts, basePts;
        auto flush = [&]
        {
            if (topPts.size() > 1)
            {
                juce::Path a;
                a.startNewSubPath (topPts.front());
                for (size_t k = 1; k < topPts.size(); ++k) a.lineTo (topPts[k]);
                for (auto it = basePts.rbegin(); it != basePts.rend(); ++it) a.lineTo (*it);
                a.closeSubPath();
                area.addPath (a);
            }
            topPts.clear();
            basePts.clear();
        };
        for (int i = 0; i <= N; ++i)
        {
            const double f = fAt (i);
            const double own = m.bandResponseDb (b, f);
            const double ownFull = m.bandResponseDbWithGain (b, f, gFull);
            const bool active = std::abs (ownFull - own) > 0.03;
            const float yNow = yOf (total[(size_t) i] - own + m.bandResponseDbWithGain (b, f, gNow));
            if (active)
            {
                if (! inside) now.startNewSubPath (xAt (i), yNow); else now.lineTo (xAt (i), yNow);
                topPts.push_back ({ xAt (i), yOf (total[(size_t) i] - own + ownFull) });
                basePts.push_back ({ xAt (i), yOf (total[(size_t) i]) });
            }
            else if (inside)
                flush();
            inside = active;
        }
        flush();
        theme::hatch (g, area, theme::oxblood, 0.55f, 4.0f, false);
        g.setColour (theme::oxblood);
        g.strokePath (now, juce::PathStrokeType (1.6f));
    }

    // EQ match: what is wanted (dashed) and what the bands would do (thin)
    if (tab == 2 && ! lastFit.gridHz.empty())
    {
        auto pathOf = [&] (const std::vector<double>& v)
        {
            juce::Path p;
            for (size_t i = 0; i < v.size(); ++i)
            {
                const float x = plot::freqToX (lastFit.gridHz[i], pr), y = yOf (v[i]);
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            return p;
        };
        juce::Path d;
        const float dash[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.4f).createDashedStroke (d, pathOf (lastFit.wantedDb), dash, 2);
        g.setColour (theme::oxblood);
        g.fillPath (d);
        g.strokePath (pathOf (lastFit.fittedDb), juce::PathStrokeType (1.0f));
    }

    // composite curve
    juce::Path curve;
    for (int i = 0; i <= N; ++i)
    {
        const float y = yOf (total[(size_t) i]);
        if (i == 0) curve.startNewSubPath (pr.getX(), y); else curve.lineTo (xAt (i), y);
    }
    g.setColour (theme::ink);
    g.strokePath (curve, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // band handles
    for (int b = 0; b < EQ::kMaxBands; ++b)
    {
        if (! m.bandOn (b)) continue;
        const bool sel = std::find (multi.begin(), multi.end(), b) != multi.end();
        theme::numberedHandle (g, bandPoint (b), b + 1, sel, theme::ink, b == selected ? 12.0f : 10.5f);
    }

    // value readout next to the band(s) being dragged
    if (dragBand >= 0 && m.bandOn (dragBand))
    {
        const auto p = bandPoint (dragBand);
        const auto shape = (EQ::Shape) (int) m.getParam (EQ::bandParam (dragBand, EQ::fShape));
        const bool gainShape = m.isParamRelevant (EQ::bandParam (dragBand, EQ::fGain));
        juce::String t = formatValue (m.getParam (EQ::bandParam (dragBand, EQ::fFreq)), "Hz");
        if (gainShape) t << "   " << formatValue (m.getParam (EQ::bandParam (dragBand, EQ::fGain)), "dB");
        t << "   Q " << numText (m.getParam (EQ::bandParam (dragBand, EQ::fQ)), 2);
        juce::ignoreUnused (shape);
        const float w = (float) juce::GlyphArrangement::getStringWidthInt (theme::mono (11.0f), t) + 18.0f;
        auto box = juce::Rectangle<float> (p.x + 16.0f, p.y - 34.0f, w, 22.0f);
        if (box.getRight() > pr.getRight()) box.setX (p.x - 16.0f - w);
        if (box.getY() < pr.getY()) box.setY (p.y + 14.0f);
        g.setColour (theme::ink);
        g.fillRect (box);
        g.setColour (theme::card);
        g.setFont (theme::mono (11.0f));
        g.drawText (t, box, juce::Justification::centred);
    }
    // lasso
    if (lasso)
        dashedRect (g, juce::Rectangle<float> (dragStart, lassoEnd), theme::ink);
    g.restoreState();

    g.setColour (theme::ink);
    g.drawRect (pr.expanded (0.5f), 1.0f);

    // hover crosshair + reading
    if (pr.contains (hover) && dragBand < 0 && ! lasso)
    {
        const double f = plot::xToFreq (hover.x, pr);
        g.setColour (theme::oxblood.withAlpha (0.5f));
        g.drawVerticalLine ((int) hover.x, pr.getY(), pr.getBottom());
        const auto txt = formatValue (f, "Hz") + "  " + plot::noteName (f) + "   " + juce::String (plot::yToDb (hover.y, pr, -r, r), r < 4 ? 2 : 1) + " dB";
        auto box = juce::Rectangle<float> (hover.x + 12.0f, hover.y - 30.0f, 196.0f, 22.0f);
        if (box.getRight() > pr.getRight()) box.setX (hover.x - 208.0f);
        g.setColour (theme::card);
        g.fillRect (box);
        g.setColour (theme::ink);
        g.drawRect (box, 1.0f);
        g.setColour (theme::text);
        g.setFont (theme::mono (10.5f));
        g.drawText (txt, box, juce::Justification::centred);
    }

    // caption above the plot
    g.setColour (theme::textFaint);
    g.setFont (theme::mono (9.5f));
    const char* modes[] = { "ZERO LATENCY", "NATURAL PHASE", "LINEAR PHASE" };
    g.drawText (juce::String (juce::CharPointer_UTF8 ("CLICK TO ADD \xc2\xb7 DRAG TO MOVE (SHIFT FINE) \xc2\xb7 DRAG EMPTY SPACE TO SELECT \xc2\xb7 DEL REMOVES \xc2\xb7 WHEEL = Q   \xe2\x80\x94  "))
                    + modes[(int) m.getParam (0)],
                juce::Rectangle<int> ((int) pr.getX(), (int) pr.getY() - 26, (int) pr.getWidth() - 280, 20), juce::Justification::centredLeft, true);

    // ---- side column ----
    auto s = side();
    g.setColour (theme::ink);
    g.drawVerticalLine (s.getX() - 12, (float) s.getY(), (float) s.getBottom());
    if (tab == 0)
    {
        auto line = s.withTrimmedTop (38).withHeight (24);
        line.removeFromLeft (30);
        g.setColour (theme::ink);
        g.setFont (theme::heading (19.0f));
        const juce::String t = selected >= 0 ? "Band " + juce::String (selected + 1) : juce::String ("No band");
        g.drawText (t, line.withWidth (120), juce::Justification::centredLeft);
        g.setColour (theme::textFaint);
        g.setFont (theme::mono (9.0f));
        if (multi.size() > 1)
            g.drawText (juce::String ((int) multi.size()) + " SELECTED", line.withTrimmedLeft (72).withWidth (100), juce::Justification::centredLeft);
        else if (selected >= 0)
            g.drawText (kShapeShort[(int) m.getParam (EQ::bandParam (selected, EQ::fShape))], line.withTrimmedLeft (72).withWidth (100), juce::Justification::centredLeft);
        if (bandRows.isEmpty())
        {
            g.setColour (theme::textDim);
            g.setFont (theme::sans (12.5f));
            g.drawFittedText ("Click the curve to add a band. Drag across empty space to select several bands, then press Delete to remove them.",
                              s.withTrimmedTop (80).withHeight (80), juce::Justification::topLeft, 4);
        }
    }
    else if (tab == 2)
    {
        auto r = s.withTrimmedTop (38);
        g.setFont (theme::mono (9.5f));
        g.setColour (theme::textDim);
        auto lab = [&] (juce::Rectangle<int> area, const juce::String& t) { g.drawText (t, area.withWidth (88), juce::Justification::centredLeft); };
        lab (refFile.getBounds().withX (s.getX()), "REFERENCE");
        lab (learnButton.getBounds().withX (s.getX()), "YOURS");
        lab (amount.getBounds().withX (s.getX()), "AMOUNT");
        lab (maxBands.getBounds().withX (s.getX()), "MAX BANDS");
        g.setColour (theme::text);
        g.drawText (juce::String (juce::roundToInt (amount.getValue())) + " %", amount.getBounds().withX (amount.getRight() + 4).withWidth (48), juce::Justification::centredRight);
        g.drawText (juce::String (juce::roundToInt (maxBands.getValue())), maxBands.getBounds().withX (maxBands.getRight() + 4).withWidth (48), juce::Justification::centredRight);
        g.setColour (theme::textFaint);
        auto info = [&] (const sonic::eqmatch::Spectrum& sp, const juce::String& name)
        {
            return sp.empty() ? juce::String ("-") : (name.isNotEmpty() ? name.toUpperCase() + "  " : juce::String()) + juce::String (sp.frames) + " FRAMES";
        };
        g.drawText (info (reference, refName), refFile.getBounds().translated (0, 26).withX (s.getX() + 92).withWidth (s.getWidth() - 92).withHeight (12),
                    juce::Justification::centredLeft, true);
        g.drawText (info (yours, {}), learnButton.getBounds().translated (0, 26).withX (s.getX() + 92).withWidth (s.getWidth() - 92).withHeight (12),
                    juce::Justification::centredLeft, true);
        g.setColour (theme::textDim);
        g.setFont (theme::sans (12.0f));
        g.drawFittedText (matchStatus, juce::Rectangle<int> (s.getX(), applyButton.getBottom() + 10, s.getWidth(), s.getBottom() - applyButton.getBottom() - 10),
                          juce::Justification::topLeft, 4);
        juce::ignoreUnused (r);
    }
}

void EqView::mouseMove (const juce::MouseEvent& e)
{
    hover = e.position;
    setMouseCursor (bandAt (e.position) >= 0 ? juce::MouseCursor::DraggingHandCursor
                    : plot().contains (e.position) ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
    repaint (plot().toNearestInt().expanded (40, 30));
}

void EqView::mouseExit (const juce::MouseEvent&)
{
    hover = { -1, -1 };
    repaint (plot().toNearestInt().expanded (40, 30));
}

void EqView::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const int b = bandAt (e.position);
    lasso = false;
    pressOnEmpty = false;
    if (e.mods.isPopupMenu())
    {
        if (b >= 0)
        {
            if (std::find (multi.begin(), multi.end(), b) == multi.end()) selectBand (b);
            bandMenu (b);
        }
        else if (plot().contains (e.position))
            canvasMenu (e.position);
        return;
    }
    dragStart = e.position;
    lassoEnd = e.position;
    if (b >= 0)
    {
        const bool additive = e.mods.isCtrlDown() || e.mods.isCommandDown();
        if (additive) selectBand (b, true);
        else if (std::find (multi.begin(), multi.end(), b) == multi.end()) selectBand (b);
        else { selected = b; buildBandControls(); }
        dragBand = b;
        dragOrigin.clear();
        for (int k : multi)
            dragOrigin.push_back ({ eq().getParam (EQ::bandParam (k, EQ::fFreq)), eq().getParam (EQ::bandParam (k, EQ::fGain)) });
        return;
    }
    dragBand = -1;
    pressOnEmpty = plot().contains (e.position);
}

void EqView::mouseDrag (const juce::MouseEvent& e)
{
    if (pressOnEmpty)
    {
        if (! lasso && e.position.getDistanceFrom (dragStart) > 5.0f) lasso = true;
        if (lasso)
        {
            lassoEnd = e.position;
            const auto area = juce::Rectangle<float> (dragStart, lassoEnd);
            if (! (e.mods.isCtrlDown() || e.mods.isCommandDown())) multi.clear();
            for (int b = 0; b < EQ::kMaxBands; ++b)
                if (eq().bandOn (b) && area.contains (bandPoint (b)) && std::find (multi.begin(), multi.end(), b) == multi.end())
                    multi.push_back (b);
            selected = multi.empty() ? -1 : multi.front();
            repaint();
        }
        return;
    }
    if (dragBand < 0) return;
    const auto pr = plot();
    const double r = range();
    const bool fine = e.mods.isShiftDown();
    const float dx = (e.position.x - dragStart.x) * (fine ? 0.2f : 1.0f);
    const float dy = (e.position.y - dragStart.y) * (fine ? 0.2f : 1.0f);
    for (size_t i = 0; i < multi.size() && i < dragOrigin.size(); ++i)
    {
        const int b = multi[i];
        const double f = plot::xToFreq (plot::freqToX (dragOrigin[i].first, pr) + dx, pr);
        eq().setParam (EQ::bandParam (b, EQ::fFreq), (float) juce::jlimit (10.0, 22000.0, f));
        if (eq().isParamRelevant (EQ::bandParam (b, EQ::fGain)))
        {
            const auto shape = (EQ::Shape) (int) eq().getParam (EQ::bandParam (b, EQ::fShape));
            const double scale = (shape == EQ::Shape::bell ? 1.0 : 2.0) * 2.0 * r / pr.getHeight();
            eq().setParam (EQ::bandParam (b, EQ::fGain), (float) juce::jlimit (-30.0, 30.0, dragOrigin[i].second - dy * scale));
        }
    }
    repaint();
}

void EqView::mouseUp (const juce::MouseEvent& e)
{
    if (pressOnEmpty && ! lasso && plot().contains (e.position))
        addBandAt (plot::xToFreq (e.position.x, plot()), plot::yToDb (e.position.y, plot(), -range(), range()));
    else if (lasso)
    {
        selected = multi.empty() ? -1 : multi.front();
        buildBandControls();
    }
    else if (dragBand >= 0 && e.getDistanceFromDragStart() > 0)
        ui.proc.checkpoint();
    lasso = false;
    pressOnEmpty = false;
    dragBand = -1;
    repaint();
}

void EqView::mouseDoubleClick (const juce::MouseEvent& e)
{
    // double-click a band: back to 0 dB
    const int b = bandAt (e.position);
    if (b >= 0 && eq().isParamRelevant (EQ::bandParam (b, EQ::fGain)))
    {
        eq().setParam (EQ::bandParam (b, EQ::fGain), 0.0f);
        ui.proc.checkpoint();
        repaint();
    }
}

void EqView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    int b = bandAt (e.position);
    if (b < 0) b = selected;
    if (b < 0 || ! plot().contains (e.position)) return;
    float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? -w.deltaX : w.deltaY;
    if (w.isReversed) d = -d;
    const float step = e.mods.isShiftDown() ? 0.4f : 1.5f;    // ~4 % per notch (Shift: ~1 %)
    const float q = eq().getParam (EQ::bandParam (b, EQ::fQ));
    eq().setParam (EQ::bandParam (b, EQ::fQ), juce::jlimit (0.025f, 40.0f, q * std::pow (1.12f, d * step)));
    repaint();
}

bool EqView::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
    {
        if (multi.empty() && selected < 0) return false;
        deleteSelectedBands();
        return true;
    }
    if (k == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0)) { selectAllBands(); return true; }
    if (k == juce::KeyPress::escapeKey && multi.size() > 1) { selectBand (selected); return true; }
    return false;
}

void EqView::bandMenu (int b)
{
    juce::PopupMenu shapes, slopes, places, m;
    const int shape = (int) eq().getParam (EQ::bandParam (b, EQ::fShape));
    const char* longNames[] = { "Bell", "Low shelf", "High shelf", "Low cut", "High cut", "Notch", "Band pass", "Tilt" };
    for (int i = 0; i < 8; ++i) shapes.addItem (100 + i, longNames[i], true, i == shape);
    const int slope = (int) eq().getParam (EQ::bandParam (b, EQ::fSlope));
    for (int i = 0; i < 8; ++i) slopes.addItem (200 + i, EQ::slopeName (i), true, i == slope);
    const int place = (int) eq().getParam (EQ::bandParam (b, EQ::fPlace));
    const char* pl[] = { "Stereo", "Left", "Right", "Mid", "Side" };
    for (int i = 0; i < 5; ++i) places.addItem (300 + i, pl[i], true, i == place);
    m.addSectionHeader ("Band " + juce::String (b + 1));
    m.addSubMenu ("Shape", shapes);
    m.addSubMenu ("Slope", slopes, eq().isParamRelevant (EQ::bandParam (b, EQ::fSlope)));
    m.addSubMenu ("Placement", places);
    m.addItem (400, "Dynamic", eq().isParamRelevant (EQ::bandParam (b, EQ::fDyn)), eq().getParam (EQ::bandParam (b, EQ::fDyn)) > 0.5f);
    m.addItem (401, "Reset gain to 0 dB", eq().isParamRelevant (EQ::bandParam (b, EQ::fGain)));
    m.addSeparator();
    if (multi.size() > 1)
        m.addItem (juce::PopupMenu::Item ("Remove " + juce::String ((int) multi.size()) + " selected bands").setID (501).setColour (theme::danger));
    m.addItem (juce::PopupMenu::Item ("Remove band").setID (500).setColour (theme::danger));
    juce::Component::SafePointer<EqView> safe (this);
    m.showMenuAsync (ui.menuOptions(), [safe, b] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& q = safe->eq();
        if (r >= 100 && r < 108) q.setParam (EQ::bandParam (b, EQ::fShape), (float) (r - 100));
        else if (r >= 200 && r < 208) q.setParam (EQ::bandParam (b, EQ::fSlope), (float) (r - 200));
        else if (r >= 300 && r < 305) q.setParam (EQ::bandParam (b, EQ::fPlace), (float) (r - 300));
        else if (r == 400) q.setParam (EQ::bandParam (b, EQ::fDyn), q.getParam (EQ::bandParam (b, EQ::fDyn)) > 0.5f ? 0.0f : 1.0f);
        else if (r == 401) q.setParam (EQ::bandParam (b, EQ::fGain), 0.0f);
        else if (r == 500) { safe->removeBands ({ b }); return; }
        else if (r == 501) { safe->deleteSelectedBands(); return; }
        safe->ui.proc.checkpoint();
        safe->buildBandControls();
        safe->repaint();
    });
}

void EqView::canvasMenu (juce::Point<float> p)
{
    juce::PopupMenu m;
    m.addSectionHeader ("Equaliser");
    m.addItem (1, "Add a band here", eq().firstFreeBand() >= 0);
    m.addItem (2, "Select all bands", numBands() > 0);
    m.addItem (juce::PopupMenu::Item ("Remove all bands").setID (3).setEnabled (numBands() > 0).setColour (theme::danger));
    juce::Component::SafePointer<EqView> safe (this);
    m.showMenuAsync (ui.menuOptions(), [safe, p] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r == 1) safe->addBandAt (plot::xToFreq (p.x, safe->plot()), plot::yToDb (p.y, safe->plot(), -safe->range(), safe->range()));
        else if (r == 2) safe->selectAllBands();
        else if (r == 3) { safe->selectAllBands(); safe->deleteSelectedBands(); }
    });
}

void EqView::loadReference()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a reference recording", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg");
    juce::Component::SafePointer<EqView> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getResult() == juce::File()) return;
        std::unique_ptr<juce::AudioFormatReader> reader (safe->ui.proc.formats().createReaderFor (fc.getResult()));
        if (reader == nullptr) { safe->matchStatus = "Could not read that file."; safe->repaint(); return; }
        // same analysis as the live captures (FFT size and window), so both are comparable
        const int fft = safe->ui.res.fftSize > 0 ? safe->ui.res.fftSize : 4096;
        const auto len = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 180.0));
        juce::AudioBuffer<float> buf ((int) juce::jmin (2u, reader->numChannels), len);
        reader->read (&buf, 0, len, 0, true, buf.getNumChannels() > 1);
        std::vector<float> mono ((size_t) len);
        for (int i = 0; i < len; ++i)
            mono[(size_t) i] = buf.getSample (0, i);   // left channel, like the live analysis
        sonic::eqmatch::Spectrum s;
        s.binHz = reader->sampleRate / fft;
        s.power.assign ((size_t) fft / 2 + 1, 0.0);
        for (int at = 0; at + fft <= len; at += fft / 2)
        {
            const auto sp = sonic::computeSpectrum (mono.data() + at, fft, fft, reader->sampleRate, safe->ui.res.window);
            for (size_t k = 0; k < s.power.size(); ++k) s.power[k] += sp.amplitude[k] * sp.amplitude[k];
            ++s.frames;
        }
        if (s.frames == 0) { safe->matchStatus = "That reference is too short."; safe->repaint(); return; }
        safe->reference = s;
        safe->refName = fc.getResult().getFileNameWithoutExtension();
        safe->updateMatchStatus();
    });
}

// ================================================================== SATURATION VIEW

SaturationView::SaturationView (Ui& u, int id, PreviewEngine& e)
    : ui (u), nodeId (id),
      figure (u, id, e, sonic::preview::harmonics | sonic::preview::transfer),
      controls (u, id, [] (int i) { return i == 0 || (i >= 2 && i <= 10); })
{
    for (int i = 0; i < Sat::kNumRecipes; ++i)
    {
        auto* b = recipes.add (new juce::TextButton (juce::String (Sat::recipe (i).name).toUpperCase()));
        b->setTooltip (juce::String (Sat::recipe (i).name) + ": " + Sat::recipe (i).description);
        b->onClick = [this, i] { sat().applyRecipe (i); ui.proc.checkpoint(); repaint(); };
        addAndMakeVisible (b);
    }
    addAndMakeVisible (figure);
    addAndMakeVisible (controls);
}

SaturationView::Sat& SaturationView::sat() { return static_cast<Sat&> (*ui.proc.graph().node (nodeId)->module); }

juce::Rectangle<int> SaturationView::designerArea() const
{
    auto r = getLocalBounds().removeFromLeft ((int) (getWidth() * 0.33f));
    r.removeFromTop (2 * 26 + 8 + 30);
    return r.withTrimmedLeft (34).withTrimmedBottom (20);
}

juce::Rectangle<float> SaturationView::column (int k) const
{
    const auto d = designerArea().toFloat();
    const float bw = (d.getWidth() - 8 * 8.0f) / 9.0f;
    return { d.getX() + k * (bw + 8.0f), d.getY(), bw, d.getHeight() };
}

void SaturationView::resized()
{
    auto r = getLocalBounds();
    auto left = r.removeFromLeft ((int) (getWidth() * 0.33f));
    auto chips = left.removeFromTop (26 * 2 + 4);
    const int w = (chips.getWidth() - 12) / 4;
    for (int i = 0; i < recipes.size(); ++i)
        recipes[i]->setBounds (chips.getX() + (i % 4) * (w + 4), chips.getY() + (i / 4) * 30, w, 26);
    r.removeFromLeft (26);
    figure.setBounds (r.removeFromLeft ((int) (getWidth() * 0.21f)));
    r.removeFromLeft (26);
    controls.setBounds (r);
}

void SaturationView::sync()
{
    controls.sync();
    const int active = (int) sat().getParam (0);
    for (int i = 0; i < recipes.size(); ++i)
        recipes[i]->setToggleState (i == active, juce::dontSendNotification);
    if (sat().paramVersion() != lastVersion)
    {
        lastVersion = sat().paramVersion();
        repaint (getLocalBounds().removeFromLeft ((int) (getWidth() * 0.33f)));
    }
}

void SaturationView::paint (juce::Graphics& g)
{
    auto& m = sat();
    auto d = designerArea();
    g.setColour (theme::textDim);
    g.setFont (theme::mono (9.5f));
    g.drawText ("HARMONIC DESIGNER  -  DRAG THE BARS", juce::Rectangle<int> (d.getX() - 34, d.getY() - 24, d.getWidth() + 34, 14), juce::Justification::centredLeft);
    for (int db = 0; db >= -80; db -= 20)
    {
        const float y = d.getY() + (float) (-db) / 90.0f * d.getHeight();
        g.setColour (theme::textFaint);
        g.drawText (juce::String (db), juce::Rectangle<float> ((float) d.getX() - 34, y - 6, 28, 12), juce::Justification::centredRight);
    }
    for (int k = 0; k < 9; ++k)
    {
        auto col = column (k);
        g.setColour (theme::sheet);
        g.fillRect (col);
        g.setColour (theme::edge);
        g.drawRect (col, 1.0f);
        const float v = m.getParam (2 + k);
        const float h = juce::jlimit (0.0f, 1.0f, (v + 90.0f) / 90.0f) * col.getHeight();
        fig::harmonicBar (g, col.withTop (col.getBottom() - h).reduced (col.getWidth() * 0.26f, 0.0f), k);
        g.setColour (theme::accent);
        g.fillRect (col.getX() + 3.0f, col.getBottom() - h - 1.5f, col.getWidth() - 6.0f, 3.0f);
        g.setColour (theme::ink);
        g.setFont (theme::heading (15.0f));
        g.drawText ("H" + juce::String (k + 2), juce::Rectangle<float> (col.getX(), col.getBottom() + 2, col.getWidth(), 16), juce::Justification::centred);
        g.setFont (theme::mono (9.0f));
        g.setColour (theme::textDim);
        g.drawText (v <= -89.9f ? juce::String ("OFF") : juce::String (juce::roundToInt (v)), col.withHeight (14).translated (0, 3), juce::Justification::centred);
    }
}

void SaturationView::setHarmonicFromMouse (juce::Point<float> p)
{
    if (dragHarmonic < 0)
    {
        for (int k = 0; k < 9; ++k)
            if (column (k).expanded (4.0f, 0.0f).contains (p)) { dragHarmonic = k; break; }
        if (dragHarmonic < 0) return;
    }
    const auto col = column (dragHarmonic);
    const float v = juce::jlimit (-90.0f, 0.0f, -90.0f + (col.getBottom() - p.y) / col.getHeight() * 90.0f);
    sat().setParam (2 + dragHarmonic, v);
    repaint (designerArea().expanded (40, 30));
}

void SaturationView::mouseDown (const juce::MouseEvent& e)
{
    dragHarmonic = -1;
    if (designerArea().expanded (4, 0).contains (e.getPosition()))
        setHarmonicFromMouse (e.position);
}

void SaturationView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragHarmonic >= 0)
        setHarmonicFromMouse (e.position);
}

void SaturationView::mouseUp (const juce::MouseEvent&)
{
    if (dragHarmonic >= 0)
        ui.proc.checkpoint();
    dragHarmonic = -1;
}

void SaturationView::mouseDoubleClick (const juce::MouseEvent& e)
{
    for (int k = 0; k < 9; ++k)
        if (column (k).contains (e.position))
        {
            sat().setParam (2 + k, sat().params()[(size_t) (2 + k)].defaultValue);
            ui.proc.checkpoint();
            repaint();
            return;
        }
}
