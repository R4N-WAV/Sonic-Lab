#include "BlockEditors.h"

#include <sonic/Analysis.h>
#include <sonic/SignalGenerator.h>

namespace
{
    const juce::Rectangle<int> kDisplay { 24, 16, 1552, 600 };
    const juce::Rectangle<int> kLowerLeft { 24, 630, 1120, 272 };
    const juce::Rectangle<int> kLowerRight { 1160, 630, 416, 272 };
    const char* kShapeShort[] = { "BELL", "LO SHELF", "HI SHELF", "LO CUT", "HI CUT", "NOTCH", "BAND PASS", "TILT" };

    // even harmonics warm, odd harmonics blue (index 0 = H2)
    juce::Colour harmonicColour (int k) { return k % 2 == 0 ? theme::ink : theme::textDim; }

    // even harmonics: solid ink; odd harmonics: an ink outline on paper
    void harmBar (juce::Graphics& g, juce::Rectangle<float> bar, int k, float alpha = 1.0f)
    {
        if (bar.getHeight() <= 0.0f) return;
        const auto c = theme::ink.withMultipliedAlpha (alpha);
        if (k % 2 == 0) { g.setColour (c); g.fillRect (bar); }
        else { g.setColour (theme::card); g.fillRect (bar); g.setColour (c); g.drawRect (bar, 1.0f); }
    }



    void drawDashedH (juce::Graphics& g, float x0, float x1, float y, juce::Colour c)
    {
        g.setColour (c);
        for (float x = x0; x < x1; x += 7.0f)
            g.drawLine (x, y, juce::jmin (x + 3.0f, x1), y, 1.0f);
    }
}

// ================================================================== EQ EDITOR

EqEditor::EqEditor (Ui& u, int id) : BlockEditor (u, id)
{
    back.onClick = [this] { if (onClose) onClose(); };
    back.getProperties().set ("link", true);
    back.getProperties().set ("colour", (int) theme::accent.getARGB());
    addAndMakeVisible (back);

    const char* labels[] = { "30", "24", "12", "6", "3", "1", "AUTO" };
    const double vals[] = { 30, 24, 12, 6, 3, 1, 0 };
    for (int i = 0; i < 7; ++i)
    {
        auto* b = rangeButtons.add (new juce::TextButton (i < 6 ? juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + labels[i] : labels[i]));
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

    addBand.onClick = [this]
    {
        const int b = eq().firstFreeBand();
        if (b < 0) return;
        eq().setParam (EQ::bandParam (b, EQ::fShape), 0.0f);
        eq().setParam (EQ::bandParam (b, EQ::fGain), 0.0f);
        eq().setParam (EQ::bandParam (b, EQ::fQ), 1.0f);
        eq().setParam (EQ::bandParam (b, EQ::fOn), 1.0f);
        ui.proc.checkpoint();
        selectBand (b);
    };
    removeBand.onClick = [this]
    {
        if (selected < 0) return;
        eq().setParam (EQ::bandParam (selected, EQ::fOn), 0.0f);
        ui.proc.checkpoint();
        selectBand (-1);
    };
    addAndMakeVisible (addBand);
    addAndMakeVisible (removeBand);

    phaseMode = std::make_unique<ModuleChoice> (ui.proc, eq(), 0);
    linQuality = std::make_unique<ModuleChoice> (ui.proc, eq(), 1);
    outputRow = std::make_unique<ModuleParamRow> (ui.proc, eq(), 2);
    addAndMakeVisible (*phaseMode);
    addAndMakeVisible (*linQuality);
    addAndMakeVisible (*outputRow);

    refButton.onClick = [this] { loadReference(); };
    learnButton.setClickingTogglesState (true);
    learnButton.onClick = [this]
    {
        if (learnButton.getToggleState())
        {
            learnPow.clear();
            learnFrames = 0;
            matchStatus = "Learning: play the material you want to match...";
        }
        else
            matchStatus = "Learned " + juce::String (learnFrames / 15.0, 1) + " s of input.";
        learnButton.setButtonText (learnButton.getToggleState() ? "STOP LEARNING" : "LEARN INPUT");
        repaint();
    };
    applyButton.getProperties().set ("beige", true);
    applyButton.onClick = [this] { computeMatch(); };
    refButton.setTooltip ("Choose a reference recording whose tonal balance you want.");
    learnButton.setTooltip ("Play your own material through Atelier while learning; Atelier averages its spectrum.");
    applyButton.setTooltip ("Creates bell bands that move your learned input toward the reference.");
    for (auto* s : { &amount, &maxBands })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
        addAndMakeVisible (s);
    }
    for (auto* sl : { &amount, &maxBands })
    {
        sl->setColour (juce::Slider::textBoxTextColourId, theme::ink);
        sl->setColour (juce::Slider::textBoxOutlineColourId, theme::edge);
    }
    amount.setRange (0, 100, 1);
    amount.setValue (60);
    amount.setTextValueSuffix (" %");
    maxBands.setRange (1, 16, 1);
    maxBands.setValue (8);
    for (auto* b : { &refButton, &learnButton, &applyButton })
        addAndMakeVisible (b);

    for (int b = 0; b < EQ::kMaxBands; ++b)
        if (eq().bandOn (b)) { selected = b; break; }
    buildBandControls();
    startTimerHz (25);
}

EqEditor::EQ& EqEditor::eq() { return static_cast<EQ&> (*nodePtr()->module); }

juce::Rectangle<float> EqEditor::plot() const
{
    return kDisplay.toFloat().withTrimmedLeft (64).withTrimmedRight (64).withTrimmedTop (52).withTrimmedBottom (44);
}

double EqEditor::range() const
{
    if (! autoRange)
        return fixedRange;
    auto& m = const_cast<EqEditor*> (this)->eq();
    double mx = 0.25;
    for (int i = 0; i <= 120; ++i)
        mx = juce::jmax (mx, std::abs (m.responseDb (20.0 * std::pow (1000.0, i / 120.0)) - m.getParam (2)));
    return juce::jlimit (0.5, 30.0, mx * 1.3);
}

juce::Point<float> EqEditor::bandPoint (int b) const
{
    auto& m = const_cast<EqEditor*> (this)->eq();
    const auto shape = (EQ::Shape) (int) m.getParam (EQ::bandParam (b, EQ::fShape));
    const double f = m.getParam (EQ::bandParam (b, EQ::fFreq));
    const bool gainShape = shape == EQ::Shape::bell || shape == EQ::Shape::lowShelf || shape == EQ::Shape::highShelf || shape == EQ::Shape::tilt;
    double db = gainShape ? m.getParam (EQ::bandParam (b, EQ::fGain)) : 0.0;
    if (shape == EQ::Shape::lowShelf || shape == EQ::Shape::highShelf || shape == EQ::Shape::tilt) db *= 0.5;
    const double r = range();
    return { plot::freqToX (f, plot()), plot::dbToY (juce::jlimit (-r, r, db), plot(), -r, r) };
}

int EqEditor::bandAt (juce::Point<float> p) const
{
    auto& m = const_cast<EqEditor*> (this)->eq();
    for (int b = EQ::kMaxBands - 1; b >= 0; --b)
        if (m.bandOn (b) && bandPoint (b).getDistanceFrom (p) < 11.0f)
            return b;
    return -1;
}

void EqEditor::selectBand (int b)
{
    selected = b;
    buildBandControls();
    repaint();
}

void EqEditor::buildBandControls()
{
    bandChips.clear();
    bandRows.clear();
    for (int b = 0; b < EQ::kMaxBands; ++b)
    {
        if (! eq().bandOn (b)) continue;
        auto* c = bandChips.add (new juce::TextButton (juce::String (b + 1) + "  " + formatValue (eq().getParam (EQ::bandParam (b, EQ::fFreq)), "Hz")));
        c->setToggleState (b == selected, juce::dontSendNotification);
        c->onClick = [this, b] { selectBand (b); };
        addAndMakeVisible (c);
    }
    if (selected >= 0 && eq().bandOn (selected))
        for (int f = 1; f < EQ::kParamsPerBand; ++f)
        {
            auto* row = bandRows.add (new ModuleParamRow (ui.proc, eq(), EQ::bandParam (selected, f)));
            row->labelW = 96;
            addAndMakeVisible (row);
        }
    removeBand.setEnabled (selected >= 0);
    addBand.setEnabled (eq().firstFreeBand() >= 0);
    resized();
}

void EqEditor::resized()
{
    back.setBounds (kDisplay.getX() + 12, kDisplay.getY() + 8, 96, 24);
    int x = kDisplay.getRight() - 12;
    for (int i = rangeButtons.size() - 1; i >= 0; --i)
    {
        const int w = i == 6 ? 46 : 38;
        x -= w;
        rangeButtons[i]->setBounds (x, kDisplay.getY() + 9, w, 22);
        x -= 4;
    }

    // band area
    auto r = kLowerLeft.reduced (16, 0).withTrimmedTop (48);
    auto chips = r.removeFromTop (26);
    removeBand.setBounds (chips.removeFromRight (110));
    chips.removeFromRight (6);
    addBand.setBounds (chips.removeFromRight (96));
    chips.removeFromRight (10);
    for (auto* c : bandChips)
    {
        const int w = juce::jmin (100, juce::jmax (60, chips.getWidth() / juce::jmax (1, bandChips.size()) - 4));
        c->setBounds (chips.removeFromLeft (w));
        chips.removeFromLeft (4);
    }
    r.removeFromTop (12);
    const int colW = (r.getWidth() - 24) / 2;
    for (int i = 0; i < bandRows.size(); ++i)
    {
        const int col = i / 6, row = i % 6;
        bandRows[i]->setBounds (r.getX() + col * (colW + 24), r.getY() + row * 30, colW, 26);
    }

    // processing area
    auto p = kLowerRight.reduced (16, 0).withTrimmedTop (48);
    p.removeFromTop (14);
    phaseMode->setBounds (p.removeFromTop (26));
    p.removeFromTop (6);
    linQuality->setBounds (p.removeFromTop (26));
    p.removeFromTop (6);
    outputRow->setBounds (p.removeFromTop (26));
    p.removeFromTop (36);
    auto br = p.removeFromTop (26);
    const int bw = (br.getWidth() - 8) / 3;
    refButton.setBounds (br.removeFromLeft (bw));
    br.removeFromLeft (4);
    learnButton.setBounds (br.removeFromLeft (bw));
    br.removeFromLeft (4);
    applyButton.setBounds (br);
    p.removeFromTop (4);
    amount.setBounds (p.removeFromTop (22).withTrimmedLeft (90));
    maxBands.setBounds (p.removeFromTop (22).withTrimmedLeft (90));
}

void EqEditor::analysisUpdated() { repaint (kDisplay); }

void EqEditor::timerCallback()
{
    if (nodePtr() == nullptr) { if (onClose) onClose(); return; }
    for (auto* r : bandRows) r->sync();
    phaseMode->sync();
    linQuality->sync();
    outputRow->sync();
    if (eq().paramVersion() != lastVersion)
    {
        lastVersion = eq().paramVersion();
        for (int i = 0, b = 0; b < EQ::kMaxBands; ++b)
            if (eq().bandOn (b) && i < bandChips.size())
                bandChips[i++]->setButtonText (juce::String (b + 1) + "  " + formatValue (eq().getParam (EQ::bandParam (b, EQ::fFreq)), "Hz"));
    }
    if (learnButton.getToggleState() && ui.res.xHasSignal && ! ui.res.spectrumX.empty())
    {
        if (learnPow.size() != ui.res.spectrumX.size())
        {
            learnPow.assign (ui.res.spectrumX.size(), 0.0);
            learnFrames = 0;
        }
        learnBinHz = ui.res.sampleRate / ui.res.fftSize;
        for (size_t k = 0; k < learnPow.size(); ++k)
            learnPow[k] += std::pow (10.0, ui.res.spectrumX[k] / 10.0);
        ++learnFrames;
    }
    repaint (kDisplay);
}

void EqEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
    auto& m = eq();
    const auto pr = plot();
    const double r = range();

    // ---- display ----
    theme::drawPanel (g, kDisplay);
    {
        auto h = kDisplay.withHeight (44).withTrimmedLeft (124);
        const auto tf = theme::heading (27.0f);
        g.setColour (theme::ink);
        g.setFont (tf);
        g.drawText ("Equaliser", h, juce::Justification::centredLeft);
        const int w = juce::GlyphArrangement::getStringWidthInt (tf, "Equaliser");
        g.setColour (theme::textFaint);
        g.setFont (theme::mono (10.0f));
        g.drawText (juce::String (nodePtr()->name).toUpperCase(), h.withTrimmedLeft (w + 14).translated (0, 3), juce::Justification::centredLeft);
        g.setFont (theme::sans (12.0f));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("Double-click to add \xc2\xb7 drag (Shift: fine) \xc2\xb7 wheel for Q \xc2\xb7 right-click for shape \xc2\xb7 double-click a value to type it")),
                    kDisplay.withHeight (44).withTrimmedLeft (470).withTrimmedRight (330), juce::Justification::centredLeft, true);
    }

    // ---- dense grid: every 1-2-3...9 step, labels at the readable ones ----
    {
        g.setColour (theme::sheet);
        g.fillRect (pr);
        g.setFont (theme::mono (9.5f));
        for (double dec : { 10.0, 100.0, 1000.0, 10000.0 })
            for (int k = 1; k <= 9; ++k)
            {
                const double f = dec * k;
                if (f < 20.0 || f > 20000.0) continue;
                const float x = plot::freqToX (f, pr);
                const bool major = k == 1;
                g.setColour (major ? theme::gridMajor : (k == 2 || k == 5 ? theme::gridMajor.withAlpha (0.6f) : theme::grid));
                g.drawVerticalLine ((int) x, pr.getY(), pr.getBottom());
            }
        for (double f : { 20.0, 30.0, 40.0, 50.0, 60.0, 80.0, 100.0, 150.0, 200.0, 300.0, 400.0, 500.0, 600.0, 800.0, 1000.0,
                          1500.0, 2000.0, 3000.0, 4000.0, 5000.0, 6000.0, 8000.0, 10000.0, 15000.0, 20000.0 })
        {
            const float x = plot::freqToX (f, pr);
            const bool major = f == 100.0 || f == 1000.0 || f == 10000.0;
            g.setColour (major ? theme::text : theme::textFaint);
            const auto lab = f >= 1000.0 ? numText (f / 1000.0, f == 1500.0 ? 1 : 0) + "k" : juce::String ((int) f);
            g.drawText (lab, juce::Rectangle<float> (x - 18.0f, pr.getBottom() + 5.0f, 36.0f, 12.0f), juce::Justification::centred);
        }
        // dB grid: major/minor steps that suit the range
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
                            juce::Rectangle<float> (pr.getX() - 58, y - 6, 50, 12), juce::Justification::centredRight);
            }
        }
    }

    g.saveState();
    g.reduceClipRegion (pr.toNearestInt());
    // analyser (output) behind the curves, mapped to -96..0 over the plot height
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

    // per-band shapes, each in its own colour
    for (int b = 0; b < EQ::kMaxBands; ++b)
    {
        if (! m.bandOn (b)) continue;
        if (b != selected) continue;   // only the selected band is shaded (camel), like a pattern piece
        const auto c = theme::logo;
        juce::Path band;
        band.startNewSubPath (pr.getX(), y0);
        for (int i = 0; i <= N; ++i)
            band.lineTo (xAt (i), yOf (m.bandResponseDb (b, fAt (i))));
        band.lineTo (pr.getRight(), y0);
        band.closeSubPath();
        g.setColour (c.withAlpha (0.22f));
        g.fillPath (band);
    }

    // dynamic bands: the range as a shaded curve between "static" and "full range",
    // plus the live gain as a solid curve - like Pro-Q
    std::vector<double> total ((size_t) N + 1);
    for (int i = 0; i <= N; ++i) total[(size_t) i] = m.responseDb (fAt (i)) - outDb;
    for (int b = 0; b < EQ::kMaxBands; ++b)
    {
        if (! m.bandOn (b) || m.getParam (EQ::bandParam (b, EQ::fDyn)) < 0.5f) continue;
        const auto c = theme::oxblood;
        const double gStatic = m.getParam (EQ::bandParam (b, EQ::fGain));
        const double gFull = gStatic + m.getParam (EQ::bandParam (b, EQ::fRange));
        const double gNow = m.dynamicGainDb (b);
        // only where the band actually moves the curve
        juce::Path full, now, area;
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
            const float yFull = yOf (total[(size_t) i] - own + ownFull);
            const float yNow = yOf (total[(size_t) i] - own + m.bandResponseDbWithGain (b, f, gNow));
            if (active)
            {
                if (! inside) { full.startNewSubPath (xAt (i), yFull); now.startNewSubPath (xAt (i), yNow); }
                else { full.lineTo (xAt (i), yFull); now.lineTo (xAt (i), yNow); }
                topPts.push_back ({ xAt (i), yFull });
                basePts.push_back ({ xAt (i), yOf (total[(size_t) i]) });
            }
            else if (inside)
                flush();
            inside = active;
        }
        flush();
        theme::hatch (g, area, c, 0.6f, 4.0f, false);
        juce::Path dashedFull;
        const float dash[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.3f).createDashedStroke (dashedFull, full, dash, 2);
        g.setColour (c.withAlpha (0.85f));
        g.fillPath (dashedFull);
        g.setColour (c);
        g.strokePath (now, juce::PathStrokeType (1.6f));
    }

    // composite (static) curve, glowing
    juce::Path curve;
    for (int i = 0; i <= N; ++i)
    {
        const float y = yOf (total[(size_t) i]);
        if (i == 0) curve.startNewSubPath (pr.getX(), y); else curve.lineTo (xAt (i), y);
    }
    g.setColour (theme::ink);
    g.strokePath (curve, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // band handles (+ the dynamic target handle)
    for (int b = 0; b < EQ::kMaxBands; ++b)
    {
        if (! m.bandOn (b)) continue;
        const auto p = bandPoint (b);
        const bool sel = b == selected;
        if (m.getParam (EQ::bandParam (b, EQ::fDyn)) > 0.5f)
        {
            const double f = m.getParam (EQ::bandParam (b, EQ::fFreq));
            const double gFull = m.getParam (EQ::bandParam (b, EQ::fGain)) + m.getParam (EQ::bandParam (b, EQ::fRange));
            const double dbFull = m.responseDb (f) - outDb - m.bandResponseDb (b, f) + m.bandResponseDbWithGain (b, f, gFull);
            const float yf = yOf (dbFull);
            g.setColour (theme::oxblood);
            g.drawLine (p.x, p.y, p.x, yf, 0.8f);
            juce::Path d;
            d.addRectangle (-4.0f, -4.0f, 8.0f, 8.0f);
            d.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::pi * 0.25f).translated (p.x, yf));
            g.setColour (theme::card);
            g.fillPath (d);
            g.setColour (theme::oxblood);
            g.strokePath (d, juce::PathStrokeType (1.2f));
        }
        theme::numberedHandle (g, p, b + 1, sel, theme::ink, sel ? 12.0f : 10.5f);
    }
    g.restoreState();
    g.setColour (theme::ink);
    g.drawRect (pr.expanded (0.5f), 1.0f);
    for (int i = 0; i <= 300; ++i)
    {
        const float x = pr.getX() + pr.getWidth() * (float) i / 300.0f;
        g.setColour (theme::textFaint);
        g.drawLine (x, pr.getBottom(), x, pr.getBottom() + 3.0f, 0.6f);
    }

    // hover crosshair + reading
    if (pr.contains (hover) && dragBand < 0)
    {
        const double f = plot::xToFreq (hover.x, pr);
        g.setColour (theme::oxblood.withAlpha (0.6f));
        g.drawVerticalLine ((int) hover.x, pr.getY(), pr.getBottom());
        const auto txt = formatValue (f, "Hz") + "  " + plot::noteName (f) + "   " + juce::String (plot::yToDb (hover.y, pr, -r, r), r < 4 ? 2 : 1) + " dB";
        auto box = juce::Rectangle<float> (hover.x + 12.0f, hover.y - 30.0f, 200.0f, 22.0f);
        if (box.getRight() > pr.getRight()) box.setX (hover.x - 212.0f);
        g.setColour (theme::card);
        g.fillRect (box);
        g.setColour (theme::ink);
        g.drawRect (box, 1.0f);
        g.setColour (theme::text);
        g.setFont (theme::mono (10.5f));
        g.drawText (txt, box, juce::Justification::centred);
    }

    g.setColour (theme::textFaint);
    g.setFont (theme::mono (9.5f));
    const char* modes[] = { "ZERO LATENCY", "NATURAL PHASE", "LINEAR PHASE" };
    g.drawText (juce::String (modes[(int) m.getParam (0)]) + "  -  LATENCY " + juce::String (m.latencySamples()) + " SMP  -  ANALYSER: OUTPUT",
                kDisplay.withTrimmedLeft (64).removeFromBottom (22), juce::Justification::centredLeft);
    if ((int) m.getParam (0) == 2)
        g.drawText ("LINEAR MODE: DYNAMIC BANDS ACT STATICALLY", kDisplay.withTrimmedRight (64).removeFromBottom (22), juce::Justification::centredRight);

    // ---- band panel ----
    theme::drawPanel (g, kLowerLeft, "Band", selected >= 0 ? "band " + juce::String (selected + 1) + " - " + kShapeShort[(int) m.getParam (EQ::bandParam (selected, EQ::fShape))]
                                                          : "no band selected");
    if (bandRows.isEmpty())
    {
        g.setColour (theme::textDim);
        g.setFont (theme::sans (13.0f));
        g.drawText ("Double-click the curve to add a band, or click + ADD BAND. Up to 24 bands.",
                    kLowerLeft.withTrimmedTop (100), juce::Justification::centredTop);
    }

    // ---- processing panel ----
    theme::drawPanel (g, kLowerRight, "Processing");
    auto p = kLowerRight.reduced (16, 0).withTrimmedTop (48);
    g.setColour (theme::textDim);
    g.setFont (theme::mono (9.5f));
    g.drawText ("PHASE MODE / LINEAR QUALITY", p.withHeight (12), juce::Justification::centredLeft);
    auto mb = juce::Rectangle<int> (p.getX() - 6, refButton.getY() - 26, p.getWidth() + 12, kLowerRight.getBottom() - 8 - (refButton.getY() - 26));
    g.setColour (theme::card);
    g.fillRect (mb);
    g.setColour (theme::ink);
    g.drawRect (mb, 1);
    g.setColour (theme::beigeText);
    g.drawText (theme::q ("eq match") + (refName.isNotEmpty() ? "   REF: " + refName.toUpperCase() : juce::String()),
                mb.reduced (8, 0).withHeight (22), juce::Justification::centredLeft, true);
    g.setColour (theme::textDim);
    g.setFont (theme::sans (12.0f));
    g.drawText ("Amount", juce::Rectangle<int> (p.getX(), amount.getY(), 86, 22), juce::Justification::centredLeft);
    g.drawText ("Max bands", juce::Rectangle<int> (p.getX(), maxBands.getY(), 86, 22), juce::Justification::centredLeft);
    g.drawFittedText (matchStatus.isNotEmpty() ? matchStatus : "1. Load a reference  2. Learn your input  3. Apply",
                      juce::Rectangle<int> (p.getX(), maxBands.getBottom() + 2, p.getWidth(), mb.getBottom() - maxBands.getBottom() - 4), juce::Justification::topLeft, 2);
}

void EqEditor::mouseMove (const juce::MouseEvent& e)
{
    hover = e.position;
    setMouseCursor (bandAt (e.position) >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    repaint (kDisplay);
}

void EqEditor::mouseDown (const juce::MouseEvent& e)
{
    const int b = bandAt (e.position);
    if (e.mods.isPopupMenu())
    {
        if (b >= 0) { selectBand (b); bandMenu (b); }
        return;
    }
    dragBand = b;
    if (b >= 0)
    {
        if (b != selected) selectBand (b);
        dragStart = e.position;
        startFreq = eq().getParam (EQ::bandParam (b, EQ::fFreq));
        startGain = eq().getParam (EQ::bandParam (b, EQ::fGain));
    }
}

void EqEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBand < 0) return;
    const auto pr = plot();
    const double r = range();
    const bool fine = e.mods.isShiftDown();
    const float dx = (e.position.x - dragStart.x) * (fine ? 0.2f : 1.0f);
    const float dy = (e.position.y - dragStart.y) * (fine ? 0.2f : 1.0f);
    const double f = plot::xToFreq (plot::freqToX (startFreq, pr) + dx, pr);
    eq().setParam (EQ::bandParam (dragBand, EQ::fFreq), (float) juce::jlimit (10.0, 22000.0, f));
    const auto shape = (EQ::Shape) (int) eq().getParam (EQ::bandParam (dragBand, EQ::fShape));
    if (shape == EQ::Shape::bell || shape == EQ::Shape::lowShelf || shape == EQ::Shape::highShelf || shape == EQ::Shape::tilt)
    {
        const double scale = (shape == EQ::Shape::bell ? 1.0 : 2.0) * 2.0 * r / pr.getHeight();
        eq().setParam (EQ::bandParam (dragBand, EQ::fGain), (float) juce::jlimit (-30.0, 30.0, startGain - dy * scale));
    }
    repaint (kDisplay);
}

void EqEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragBand >= 0) ui.proc.checkpoint();
    dragBand = -1;
}

void EqEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int b = bandAt (e.position);
    if (b >= 0)
    {
        eq().setParam (EQ::bandParam (b, EQ::fOn), 0.0f);
        ui.proc.checkpoint();
        selectBand (-1);
        return;
    }
    const auto pr = plot();
    if (! pr.contains (e.position)) return;
    const int nb = eq().firstFreeBand();
    if (nb < 0) return;
    const double f = plot::xToFreq (e.position.x, pr);
    const double gdb = plot::yToDb (e.position.y, pr, -range(), range());
    // shape suggestion by position: far left = low cut, far right = high cut
    int shape = 0;
    if (f < 30.0) shape = (int) EQ::Shape::lowCut;
    else if (f > 16000.0) shape = (int) EQ::Shape::highCut;
    eq().setParam (EQ::bandParam (nb, EQ::fShape), (float) shape);
    eq().setParam (EQ::bandParam (nb, EQ::fFreq), (float) f);
    eq().setParam (EQ::bandParam (nb, EQ::fGain), shape == 0 ? (float) juce::jlimit (-30.0, 30.0, gdb) : 0.0f);
    eq().setParam (EQ::bandParam (nb, EQ::fQ), shape == 0 ? 1.0f : 0.7071f);
    eq().setParam (EQ::bandParam (nb, EQ::fOn), 1.0f);
    ui.proc.checkpoint();
    selectBand (nb);
}

void EqEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    int b = bandAt (e.position);
    if (b < 0) b = selected;
    if (b < 0) return;
    const float q = eq().getParam (EQ::bandParam (b, EQ::fQ));
    eq().setParam (EQ::bandParam (b, EQ::fQ), juce::jlimit (0.025f, 40.0f, q * std::pow (1.15f, w.deltaY * 8.0f)));
    ui.proc.checkpoint();
    repaint (kDisplay);
}

void EqEditor::bandMenu (int b)
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
    m.addSubMenu ("Shape", shapes);
    m.addSubMenu ("Slope (cuts)", slopes);
    m.addSubMenu ("Placement", places);
    m.addItem (400, "Dynamic", true, eq().getParam (EQ::bandParam (b, EQ::fDyn)) > 0.5f);
    m.addSeparator();
    m.addItem (500, "Remove band");
    juce::Component::SafePointer<EqEditor> safe (this);
    m.showMenuAsync ({}, [safe, b] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& q = safe->eq();
        if (r >= 100 && r < 108) q.setParam (EQ::bandParam (b, EQ::fShape), (float) (r - 100));
        else if (r >= 200 && r < 208) q.setParam (EQ::bandParam (b, EQ::fSlope), (float) (r - 200));
        else if (r >= 300 && r < 305) q.setParam (EQ::bandParam (b, EQ::fPlace), (float) (r - 300));
        else if (r == 400) q.setParam (EQ::bandParam (b, EQ::fDyn), q.getParam (EQ::bandParam (b, EQ::fDyn)) > 0.5f ? 0.0f : 1.0f);
        else if (r == 500) { q.setParam (EQ::bandParam (b, EQ::fOn), 0.0f); safe->selected = -1; }
        safe->ui.proc.checkpoint();
        safe->buildBandControls();
        safe->repaint();
    });
}

void EqEditor::loadReference()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a reference recording", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg");
    juce::Component::SafePointer<EqEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getResult() == juce::File()) return;
        std::unique_ptr<juce::AudioFormatReader> reader (safe->ui.proc.formats().createReaderFor (fc.getResult()));
        if (reader == nullptr) { safe->matchStatus = "Could not read that file."; safe->repaint(); return; }
        const int fft = 8192;
        const auto len = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 120.0));
        juce::AudioBuffer<float> buf ((int) juce::jmin (2u, reader->numChannels), len);
        reader->read (&buf, 0, len, 0, true, buf.getNumChannels() > 1);
        std::vector<float> mono ((size_t) len);
        for (int i = 0; i < len; ++i)
            mono[(size_t) i] = buf.getNumChannels() > 1 ? 0.5f * (buf.getSample (0, i) + buf.getSample (1, i)) : buf.getSample (0, i);
        std::vector<double> pow ((size_t) fft / 2 + 1, 0.0);
        int frames = 0;
        for (int s = 0; s + fft <= len; s += fft / 2)
        {
            auto sp = sonic::computeSpectrum (mono.data() + s, fft, fft, reader->sampleRate, sonic::WindowType::hann);
            for (size_t k = 0; k < pow.size(); ++k) pow[k] += sp.amplitude[k] * sp.amplitude[k];
            ++frames;
        }
        if (frames == 0) { safe->matchStatus = "Reference is too short."; safe->repaint(); return; }
        safe->refFreq.clear();
        safe->refDb.clear();
        for (size_t k = 1; k < pow.size(); ++k)
        {
            safe->refFreq.push_back (k * reader->sampleRate / fft);
            safe->refDb.push_back (10.0 * std::log10 (pow[k] / frames + 1e-20));
        }
        safe->refName = fc.getResult().getFileNameWithoutExtension();
        safe->matchStatus = "Reference analysed (" + juce::String (len / reader->sampleRate, 1) + " s). Now learn your input.";
        safe->repaint();
    });
}

void EqEditor::computeMatch()
{
    if (refDb.empty()) { matchStatus = "Load a reference first."; repaint(); return; }
    if (learnFrames < 10 || learnPow.empty()) { matchStatus = "Learn your input first (play audio while LEARN is on)."; repaint(); return; }

    // both spectra on a log grid with 1/3-octave power smoothing
    const int G = 240;
    std::vector<double> fGrid ((size_t) G), diff ((size_t) G);
    auto smoothAt = [] (double f, auto&& powAtFreq) { return powAtFreq (f); };
    juce::ignoreUnused (smoothAt);
    auto refPow = [this] (double f0, double f1)
    {
        double s = 0; int n = 0;
        for (size_t k = 0; k < refFreq.size(); ++k)
            if (refFreq[k] >= f0 && refFreq[k] <= f1) { s += std::pow (10.0, refDb[k] / 10.0); ++n; }
        return n > 0 ? 10.0 * std::log10 (s / n + 1e-20) : -200.0;
    };
    auto curPow = [this] (double f0, double f1)
    {
        double s = 0; int n = 0;
        for (size_t k = 1; k < learnPow.size(); ++k)
        {
            const double f = k * learnBinHz;
            if (f >= f0 && f <= f1) { s += learnPow[k] / learnFrames; ++n; }
        }
        return n > 0 ? 10.0 * std::log10 (s / n + 1e-20) : -200.0;
    };
    const double third = std::pow (2.0, 1.0 / 6.0);
    double meanDiff = 0.0; int meanN = 0;
    for (int i = 0; i < G; ++i)
    {
        const double f = 25.0 * std::pow (18000.0 / 25.0, (double) i / (G - 1));
        fGrid[(size_t) i] = f;
        const double d = refPow (f / third, f * third) - curPow (f / third, f * third);
        diff[(size_t) i] = d;
        if (f > 100.0 && f < 10000.0 && d > -150.0 && d < 150.0) { meanDiff += d; ++meanN; }
    }
    if (meanN == 0) { matchStatus = "Not enough overlapping spectrum to compare."; repaint(); return; }
    meanDiff /= meanN;
    const double amt = amount.getValue() * 0.01;
    for (auto& d : diff) d = juce::jlimit (-12.0, 12.0, (d - meanDiff)) * amt;

    // greedy bell fitting
    auto& m = eq();
    int placed = 0;
    for (int it = 0; it < (int) maxBands.getValue(); ++it)
    {
        int peak = 0;
        for (int i = 0; i < G; ++i)
            if (std::abs (diff[(size_t) i]) > std::abs (diff[(size_t) peak])) peak = i;
        const double gain = diff[(size_t) peak];
        if (std::abs (gain) < 0.5) break;
        int lo = peak, hi = peak;
        while (lo > 0 && std::abs (diff[(size_t) lo]) > std::abs (gain) * 0.5 && diff[(size_t) lo] * gain > 0) --lo;
        while (hi < G - 1 && std::abs (diff[(size_t) hi]) > std::abs (gain) * 0.5 && diff[(size_t) hi] * gain > 0) ++hi;
        const double bwOct = juce::jmax (0.2, std::log2 (fGrid[(size_t) hi] / fGrid[(size_t) lo]));
        const double q = juce::jlimit (0.3, 6.0, std::sqrt (std::pow (2.0, bwOct)) / (std::pow (2.0, bwOct) - 1.0));
        const int b = m.firstFreeBand();
        if (b < 0) break;
        m.setParam (EQ::bandParam (b, EQ::fShape), 0.0f);
        m.setParam (EQ::bandParam (b, EQ::fFreq), (float) fGrid[(size_t) peak]);
        m.setParam (EQ::bandParam (b, EQ::fGain), (float) gain);
        m.setParam (EQ::bandParam (b, EQ::fQ), (float) q);
        m.setParam (EQ::bandParam (b, EQ::fPlace), 0.0f);
        m.setParam (EQ::bandParam (b, EQ::fDyn), 0.0f);
        m.setParam (EQ::bandParam (b, EQ::fOn), 1.0f);
        for (int i = 0; i < G; ++i)
            diff[(size_t) i] -= m.bandResponseDb (b, fGrid[(size_t) i]);
        ++placed;
    }
    double rest = 0.0;
    for (auto d : diff) rest = juce::jmax (rest, std::abs (d));
    matchStatus = "Added " + juce::String (placed) + " bands. Largest remaining difference " + juce::String (rest, 1) + " dB.";
    ui.proc.checkpoint();
    buildBandControls();
    repaint();
}

// ================================================================== SATURATION EDITOR

namespace
{
    const juce::Rectangle<int> kCards { 24, 16, 1000, 404 };
    const juce::Rectangle<int> kPreview { 1040, 16, 536, 404 };
    const juce::Rectangle<int> kDesigner { 24, 436, 1000, 476 };
    const juce::Rectangle<int> kStage { 1040, 436, 536, 476 };
}

SaturationEditor::SaturationEditor (Ui& u, int id) : BlockEditor (u, id)
{
    back.onClick = [this] { if (onClose) onClose(); };
    back.getProperties().set ("link", true);
    back.getProperties().set ("colour", (int) theme::accent.getARGB());
    addAndMakeVisible (back);
    auto& m = sat();
    for (int i = 0; i < m.numParams(); ++i)
    {
        const auto& s = m.params()[(size_t) i];
        const juce::String grp (s.group);
        if (grp == "Shape" || grp == "Focus")
            addAndMakeVisible (shapeRows.add (new ModuleParamRow (ui.proc, m, i)));
        else if (s.id == "drive_db" || s.id == "output_db")
            addAndMakeVisible (knobs.add (new ModuleKnob (ui.proc, m, i, s.id == "drive_db")));
        else if (grp == "Stage" || grp == "Tone")
            addAndMakeVisible (stageRows.add (new ModuleParamRow (ui.proc, m, i)));
    }
    struct MixKnob : public juce::Component
    {
        MixKnob (Ui& u, int id) : ui (u), nid (id)
        {
            s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
            s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
            s.setRange (0, 100, 0.1);
            s.setDefault (100.0);
            s.setTextValueSuffix (" %");
            s.setNumDecimalPlacesToDisplay (0);
            s.valueArea = [this] { return getLocalBounds().removeFromBottom (18).reduced (8, 0) - s.getPosition(); };
            s.getProperties().set ("beige", true);
            if (auto* n = ui.proc.graph().node (nid)) s.setValue (n->mix.load() * 100.0, juce::dontSendNotification);
            s.onValueChange = [this] { if (auto* n = ui.proc.graph().node (nid)) n->mix = (float) s.getValue() * 0.01f; repaint(); };
            s.onDragEnd = [this] { ui.proc.checkpoint(); };
            addAndMakeVisible (s);
        }
        void resized() override { s.setBounds (getLocalBounds().withTrimmedBottom (34)); }
        void mouseDoubleClick (const juce::MouseEvent&) override { s.beginTextEntry(); }
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().removeFromBottom (32);
            g.setColour (theme::ink); g.setFont (theme::heading (17.0f));
            g.drawText ("Mix", r.removeFromTop (17), juce::Justification::centred);
            g.setColour (theme::textDim); g.setFont (theme::mono (9.5f));
            g.drawText (juce::String (juce::roundToInt (s.getValue())) + " %", r, juce::Justification::centred);
        }
        Ui& ui; int nid; AtelierSlider s;
    };
    mixKnob = std::make_unique<MixKnob> (ui, nodeId);
    addAndMakeVisible (*mixKnob);
    renderPreview();
    startTimerHz (15);
}

SaturationEditor::Sat& SaturationEditor::sat() { return static_cast<Sat&> (*nodePtr()->module); }

juce::Rectangle<int> SaturationEditor::cardRect (int i) const
{
    auto r = kCards.reduced (16, 0).withTrimmedTop (54);
    const int w = (r.getWidth() - 30) / 4, h = 150;
    return { r.getX() + (i % 4) * (w + 10), r.getY() + (i / 4) * (h + 10), w, h };
}

juce::Rectangle<int> SaturationEditor::designerArea() const
{
    return kDesigner.reduced (16, 0).withTrimmedTop (56).withHeight (210).withTrimmedLeft (44);
}

void SaturationEditor::resized()
{
    back.setBounds (kCards.getRight() - 108, kCards.getY() + 8, 96, 24);
    auto r = kDesigner.reduced (16, 0).withTrimmedTop (290);
    const int colW = (r.getWidth() - 24) / 2;
    for (int i = 0; i < shapeRows.size(); ++i)
        shapeRows[i]->setBounds (r.getX() + (i / 4) * (colW + 24), r.getY() + (i % 4) * 30, colW, 26);

    auto s = kStage.reduced (16, 0).withTrimmedTop (52);
    auto kr = s.removeFromTop (118);
    const int kw = kr.getWidth() / 3;
    if (knobs.size() > 0) knobs[0]->setBounds (kr.removeFromLeft (kw));
    if (knobs.size() > 1) knobs[1]->setBounds (kr.removeFromLeft (kw));
    mixKnob->setBounds (kr);
    s.removeFromTop (14);
    for (auto* row : stageRows)
    {
        row->setBounds (s.removeFromTop (26));
        s.removeFromTop (4);
    }
}

void SaturationEditor::timerCallback()
{
    if (nodePtr() == nullptr) { if (onClose) onClose(); return; }
    for (auto* r : shapeRows) r->sync();
    for (auto* r : stageRows) r->sync();
    for (auto* k : knobs) k->sync();
    if (sat().paramVersion() != lastVersion)
    {
        lastVersion = sat().paramVersion();
        renderPreview();
        repaint();
    }
}

void SaturationEditor::renderPreview()
{
    // Offline render of an identical copy of this block: exact, independent of what is playing.
    const double fs = 48000.0;
    auto measure = [&] (double f, double db, sonic::HarmonicResult& hr, double& aliasDb)
    {
        sonic::SaturationModule copy;
        for (int i = 0; i < copy.numParams(); ++i)
            copy.setParam (i, sat().getParam (i));
        copy.prepare (fs, 512, 2);
        sonic::SignalGenerator gen;
        gen.prepare (fs);
        gen.frequencyHz = (float) f;
        gen.levelDb = (float) db;
        const int n = 32768;
        std::vector<float> l ((size_t) n), r ((size_t) n);
        for (int i = 0; i < n; i += 512)
        {
            gen.render (l.data() + i, r.data() + i, 512);
            float* ch[2] = { l.data() + i, r.data() + i };
            copy.process (ch, 2, 512);
        }
        auto spec = sonic::computeSpectrum (l.data(), n, 16384, fs, sonic::WindowType::blackmanHarris4);
        hr = sonic::analyseHarmonics (spec, f, 10, 20000.0);
        double sum = 0.0;
        for (int k = (int) (100 / spec.binHz()); k < (int) ((f - 500) / spec.binHz()); ++k)
            sum += spec.power[(size_t) k];
        aliasDb = sonic::gainToDb (std::sqrt (4.0 * sum / (spec.fftSize * spec.windowSumSq))) - (hr.valid ? hr.levelDbfs[0] : 0.0);
    };
    sonic::HarmonicResult h, h7;
    double dummy = 0.0, alias = 0.0;
    measure (1000.0, -18.0, h, dummy);
    measure (7000.0, -6.0, h7, alias);
    previewValid = h.valid;
    previewDbc.clear();
    double even = 0.0, odd = 0.0;
    if (h.valid)
        for (int k = 1; k < h.numHarmonics; ++k)
        {
            previewDbc.push_back (h.levelDbc[(size_t) k]);
            const double p = std::pow (10.0, h.levelDbc[(size_t) k] / 10.0);
            ((k + 1) % 2 == 0 ? even : odd) += p;
        }
    previewThd = h.thdPercent;
    previewEven = even + odd > 0 ? even / (even + odd) * 100.0 : 50.0;
    previewAlias = alias;
}

void SaturationEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
    auto& m = sat();
    const int active = (int) m.getParam (0);

    // ---- character cards ----
    theme::drawPanel (g, kCards, "Harmonic character", "Fig. 04 - pick a recipe, then shape it below");
    for (int i = 0; i < Sat::kNumRecipes; ++i)
    {
        const auto& rc = Sat::recipe (i);
        auto r = cardRect (i);
        const bool on = i == active;
        theme::drawCard (g, r.toFloat(), on ? theme::accent : theme::edge, on, juce::Colours::transparentBlack, i == 7 && ! on);
        auto t = r.reduced (12);
        g.setColour (theme::ink);
        g.setFont (theme::heading (27.0f));
        g.drawText (rc.name, t.removeFromTop (28), juce::Justification::centredLeft);
        auto bars = t.removeFromTop (44).withTrimmedTop (4);
        if (i < 7)
            for (int k = 0; k < 6; ++k)
            {
                const float hgt = juce::jlimit (0.06f, 1.0f, (rc.levelsDbc[k] + 60.0f) / 60.0f) * (float) bars.getHeight();
                harmBar (g, juce::Rectangle<float> ((float) bars.getX() + k * 16.0f, (float) bars.getBottom() - hgt, 10.0f, hgt), k, on ? 1.0f : 0.8f);
            }
        else
        {
            g.setColour (theme::textDim);
            g.setFont (theme::mono (10.0f));
            g.drawText ("+ DRAW YOUR OWN", bars, juce::Justification::centredLeft);
        }
        t.removeFromTop (6);
        g.setColour (on ? theme::text : theme::textDim);
        g.setFont (theme::sans (12.0f));
        g.drawFittedText (rc.description, t, juce::Justification::topLeft, 3);
        if (on)
            theme::drawTag (g, juce::Rectangle<int> (r.getRight() - 66, r.getY() + 10, 54, 15), "ACTIVE", theme::accent, theme::edge, theme::card);
    }
    g.setColour (theme::textDim);
    g.setFont (theme::mono (9.5f));
    auto legend = kCards.reduced (16, 0).removeFromBottom (26);
    harmBar (g, juce::Rectangle<float> ((float) legend.getX(), (float) legend.getCentreY() - 5, 10.0f, 10.0f), 0);
    g.setColour (theme::textDim);
    g.drawText ("EVEN  H2 H4 H6", legend.withTrimmedLeft (16), juce::Justification::centredLeft);
    harmBar (g, juce::Rectangle<float> ((float) legend.getX() + 140, (float) legend.getCentreY() - 5, 10.0f, 10.0f), 1);
    g.setColour (theme::textDim);
    g.drawText ("ODD  H3 H5 H7", legend.withTrimmedLeft (156), juce::Justification::centredLeft);

    // ---- preview ----
    theme::drawPanel (g, kPreview, "Preview");
    theme::drawTag (g, juce::Rectangle<int> (kPreview.getRight() - 216, kPreview.getY() + 14, 200, 16), juce::String (juce::CharPointer_UTF8 ("MEASURED \xc2\xb7 1 KHZ \xc2\xb7 -18 DBFS")),
                    theme::accent, theme::edge, theme::bg);
    auto pv = kPreview.reduced (16, 0).withTrimmedTop (56);
    auto tc = pv.removeFromLeft (200).withHeight (200);
    g.setColour (theme::textDim);
    g.setFont (theme::mono (9.5f));
    g.drawText ("TRANSFER CURVE", tc.translated (0, -16).withHeight (12), juce::Justification::centredLeft);
    g.setColour (theme::sheet);
    g.fillRect (tc);
    g.setColour (theme::edge);
    g.drawHorizontalLine (tc.getCentreY(), (float) tc.getX(), (float) tc.getRight());
    g.drawVerticalLine (tc.getCentreX(), (float) tc.getY(), (float) tc.getBottom());
    {
        juce::Path p;
        double ymax = 0.05;
        std::vector<double> ys (201);
        for (int i = 0; i <= 200; ++i) { ys[(size_t) i] = m.staticCurve (-1.0 + i / 100.0); ymax = juce::jmax (ymax, std::abs (ys[(size_t) i])); }
        for (int i = 0; i <= 200; ++i)
        {
            const float x = (float) tc.getX() + tc.getWidth() * i / 200.0f;
            const float y = (float) tc.getCentreY() - (float) (ys[(size_t) i] / ymax) * tc.getHeight() * 0.48f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (theme::beige);
        g.strokePath (p, juce::PathStrokeType (1.8f));
    }
    pv.removeFromLeft (16);
    auto hb = pv.withHeight (200);
    g.setColour (theme::textDim);
    g.drawText ("HARMONICS (dBc)", hb.translated (0, -16).withHeight (12), juce::Justification::centredLeft);
    g.setColour (theme::sheet);
    g.fillRect (hb);
    if (previewValid)
    {
        const int n = (int) previewDbc.size();
        const float bw = (hb.getWidth() - 16.0f) / juce::jmax (1, n);
        for (int k = 0; k < n; ++k)
        {
            const float h = juce::jlimit (0.0f, 1.0f, (float) (previewDbc[(size_t) k] + 100.0) / 100.0f) * (hb.getHeight() - 20.0f);
            harmBar (g, juce::Rectangle<float> (hb.getX() + 8.0f + k * bw + bw * 0.25f, hb.getBottom() - 16.0f - h, bw * 0.5f, h), k);
            g.setColour (theme::textDim);
            g.drawText (juce::String (k + 2), juce::Rectangle<float> (hb.getX() + 8.0f + k * bw, (float) hb.getBottom() - 14.0f, bw, 12.0f), juce::Justification::centred);
        }
    }
    auto stats = kPreview.reduced (16, 0).withTrimmedTop (290).withHeight (60);
    auto stat = [&] (juce::Rectangle<int> r, const juce::String& l, const juce::String& v)
    {
        g.setColour (theme::textDim); g.setFont (theme::mono (9.5f));
        g.drawText (l, r.removeFromTop (14), juce::Justification::centredLeft);
        g.setColour (theme::text); g.setFont (theme::sansMedium (19.0f));
        g.drawText (v, r.removeFromTop (26), juce::Justification::centredLeft);
    };
    const int sw = stats.getWidth() / 3;
    stat (stats.removeFromLeft (sw), "THD", juce::String (previewThd, previewThd < 1 ? 3 : 2) + " %");
    stat (stats.removeFromLeft (sw), "EVEN : ODD", juce::String (juce::roundToInt (previewEven)) + " : " + juce::String (100 - juce::roundToInt (previewEven)));
    stat (stats, "ALIASING (7 KHZ)", previewAlias < -150 ? juce::String ("< -150 dB") : juce::String (juce::roundToInt (previewAlias)) + " dB");

    // ---- designer ----
    theme::drawPanel (g, kDesigner, "Harmonic designer", "Drag bars - level per harmonic for a full-scale sine at the shaper");
    auto d = designerArea();
    g.setFont (theme::mono (9.5f));
    for (int db = 0; db >= -80; db -= 20)
    {
        const float y = d.getY() + (float) (-db) / 90.0f * (d.getHeight() - 18);
        g.setColour (theme::textDim);
        g.drawText (juce::String (db), juce::Rectangle<float> ((float) d.getX() - 44, y - 6, 38, 12), juce::Justification::centredRight);
    }
    const int nb = 9;
    const float bw = (d.getWidth() - (nb - 1) * 12.0f) / nb;
    for (int k = 0; k < nb; ++k)
    {
        auto col = juce::Rectangle<float> (d.getX() + k * (bw + 12.0f), (float) d.getY(), bw, (float) d.getHeight() - 18.0f);
        g.setColour (theme::sheet);
        g.fillRect (col);
        g.setColour (theme::edge);
        g.drawRect (col, 1.0f);
        const float v = m.getParam (2 + k);
        const float h = juce::jlimit (0.0f, 1.0f, (v + 90.0f) / 90.0f) * col.getHeight();
        harmBar (g, col.withTop (col.getBottom() - h).reduced (col.getWidth() * 0.28f, 0.0f), k);
        // the handle you drag: a camel rule across the column
        g.setColour (theme::accent);
        g.fillRect (col.getX() + 6.0f, col.getBottom() - h - 1.5f, col.getWidth() - 12.0f, 3.0f);
        g.setColour (theme::ink);
        g.setFont (theme::heading (16.0f));
        g.drawText ("H" + juce::String (k + 2), juce::Rectangle<float> (col.getX(), col.getBottom() + 2, col.getWidth(), 16), juce::Justification::centred);
        g.setFont (theme::mono (9.5f));
        g.setColour (theme::textDim);
        g.drawText (v <= -89.9f ? juce::String ("OFF") : juce::String (juce::roundToInt (v)), col.withHeight (14).translated (0, 4), juce::Justification::centred);
    }
    drawDashedH (g, (float) kDesigner.getX() + 1, (float) kDesigner.getRight() - 1, (float) kDesigner.getY() + 280.0f, theme::dashed);

    // ---- stage ----
    theme::drawPanel (g, kStage, "Stage");
    g.setColour (theme::textDim);
    g.setFont (theme::sans (12.5f));
    g.drawFittedText ("Drive sets how hard you push; the character sets which harmonics appear. Auto gain keeps the level steady "
                      "so you judge tone, not volume.", kStage.reduced (16, 0).withTrimmedTop (176 + stageRows.size() * 30).withHeight (50),
                      juce::Justification::topLeft, 3);
}

void SaturationEditor::setHarmonicFromMouse (juce::Point<float> p)
{
    auto d = designerArea();
    const int nb = 9;
    const float bw = (d.getWidth() - (nb - 1) * 12.0f) / nb;
    if (dragHarmonic < 0)
    {
        const int k = (int) ((p.x - d.getX()) / (bw + 12.0f));
        if (k < 0 || k >= nb || ! d.toFloat().contains (p)) return;
        dragHarmonic = k;
    }
    const float h = (float) d.getHeight() - 18.0f;
    const float v = juce::jlimit (-90.0f, 0.0f, -90.0f + (d.getY() + h - p.y) / h * 90.0f);
    sat().setParam (2 + dragHarmonic, v);
    sat().setParam (0, 7.0f); // Custom
    repaint (kDesigner);
}

void SaturationEditor::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < Sat::kNumRecipes; ++i)
        if (cardRect (i).contains (e.getPosition()))
        {
            sat().applyRecipe (i);
            ui.proc.checkpoint();
            repaint();
            return;
        }
    dragHarmonic = -1;
    if (designerArea().contains (e.getPosition()))
        setHarmonicFromMouse (e.position);
}

void SaturationEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragHarmonic >= 0)
        setHarmonicFromMouse (e.position);
}

void SaturationEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragHarmonic >= 0)
        ui.proc.checkpoint();
    dragHarmonic = -1;
}
