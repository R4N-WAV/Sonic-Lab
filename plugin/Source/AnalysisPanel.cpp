#include "AnalysisPanel.h"

using Mode = AnalysisWorker::Mode;

AnalysisPanel::AnalysisPanel (AtelierProcessor& p) : proc (p)
{
    modeCombo.addItemList ({ "ATELIER chain: input -> output", "Reference plugin: sidechain -> main input" }, 1);
    modeCombo.setSelectedItemIndex (0, juce::dontSendNotification);
    modeCombo.setTooltip ("What is measured. Reference mode needs the unprocessed signal on ATELIER's sidechain input.");
    modeCombo.onChange = [this] { worker.setMode ((Mode) modeCombo.getSelectedItemIndex()); };

    viewCombo.addItemList ({ "Spectrum", "Transfer function" }, 1);
    viewCombo.setSelectedItemIndex (0, juce::dontSendNotification);
    viewCombo.onChange = [this] { repaint(); };

    fftCombo.addItemList ({ "FFT 4096", "FFT 8192", "FFT 16384", "FFT 32768" }, 1);
    fftCombo.setSelectedItemIndex (1, juce::dontSendNotification);
    fftCombo.setTooltip ("Spectrum/harmonics FFT size. Larger = finer frequency resolution, slower response.");
    fftCombo.onChange = [this] { worker.setFftSize (4096 << fftCombo.getSelectedItemIndex()); };

    windowCombo.addItemList ({ "Hann", "Blackman-Harris" }, 1);
    windowCombo.setSelectedItemIndex (1, juce::dontSendNotification);
    windowCombo.setTooltip ("Window function. Blackman-Harris has lower leakage (better for THD).");
    windowCombo.onChange = [this] { worker.setWindow ((sonic::WindowType) windowCombo.getSelectedItemIndex()); };

    resetButton.setTooltip ("Restart spectrum and transfer-function averaging.");
    resetButton.onClick = [this] { worker.resetAveraging(); };

    for (auto* c : { (juce::Component*) &modeCombo, (juce::Component*) &viewCombo, (juce::Component*) &fftCombo,
                     (juce::Component*) &windowCombo, (juce::Component*) &resetButton })
        addAndMakeVisible (c);

    startTimerHz (20);
}

void AnalysisPanel::timerCallback()
{
    if (worker.getResults (res, res.serial))
        repaint();
}

void AnalysisPanel::resized()
{
    auto r = getLocalBounds().reduced (12);
    r.removeFromTop (22);
    auto bar = r.removeFromTop (28);
    modeCombo.setBounds (bar.removeFromLeft (300).reduced (0, 2));
    bar.removeFromLeft (8);
    viewCombo.setBounds (bar.removeFromLeft (150).reduced (0, 2));
    bar.removeFromLeft (8);
    fftCombo.setBounds (bar.removeFromLeft (110).reduced (0, 2));
    bar.removeFromLeft (8);
    windowCombo.setBounds (bar.removeFromLeft (140).reduced (0, 2));
    bar.removeFromLeft (8);
    resetButton.setBounds (bar.removeFromLeft (90).reduced (0, 2));
    r.removeFromTop (8);

    harmArea = r.removeFromRight (270);
    r.removeFromRight (12);
    graphArea = r;
}

void AnalysisPanel::drawTag (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text, juce::Colour c)
{
    g.setColour (c.withAlpha (0.15f));
    g.fillRoundedRectangle (r.toFloat(), 2.0f);
    g.setColour (c);
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 2.0f, 1.0f);
    g.setFont (theme::bold (9.5f));
    g.drawText (text, r, juce::Justification::centred);
}

void AnalysisPanel::paint (juce::Graphics& g)
{
    theme::drawPanel (g, getLocalBounds(), "Analysis");

    g.setColour (theme::bg);
    g.fillRoundedRectangle (graphArea.toFloat(), 2.0f);
    const auto plotArea = graphArea.toFloat().reduced (50.0f, 22.0f).withTrimmedBottom (4.0f);

    if (viewCombo.getSelectedItemIndex() == 0)
        drawSpectrum (g, plotArea);
    else
        drawTransfer (g, plotArea);

    drawHarmonics (g, harmArea);
}

void AnalysisPanel::drawSpectrum (juce::Graphics& g, juce::Rectangle<float> r)
{
    const double dbMin = -132.0, dbMax = 0.0;
    plot::drawFreqGrid (g, r, dbMin, dbMax, 12.0, "dBFS");

    const bool chainMode = res.mode == Mode::chain;
    const juce::String xName = chainMode ? "Chain input" : "Sidechain (unprocessed)";
    const juce::String yName = chainMode ? "Chain output" : "Main input (processed by reference)";

    auto drawCurve = [&] (const std::vector<float>& s, juce::Colour c, float w)
    {
        if (s.size() < 4) return;
        const double binHz = res.sampleRate / res.fftSize;
        juce::Path p;
        bool started = false;
        // Plot one point per pixel column (max over bins in that column) for clarity at high FFT sizes.
        const int cols = (int) r.getWidth();
        const double fMax = juce::jmin (20000.0, res.sampleRate * 0.5);
        for (int x = 0; x < cols; ++x)
        {
            const double f0 = 20.0 * std::pow (1000.0, (double) x / cols);
            const double f1 = 20.0 * std::pow (1000.0, (double) (x + 1) / cols);
            if (f0 > fMax) break;
            int k0 = juce::jmax (1, (int) (f0 / binHz)), k1 = juce::jmax (k0, (int) (f1 / binHz));
            k1 = juce::jmin (k1, (int) s.size() - 1);
            float v = -200.0f;
            for (int k = k0; k <= k1; ++k)
                v = juce::jmax (v, s[(size_t) k]);
            const float y = plot::dbToY (juce::jlimit (dbMin, dbMax, (double) v), r, dbMin, dbMax);
            if (! started) { p.startNewSubPath (r.getX() + (float) x, y); started = true; }
            else p.lineTo (r.getX() + (float) x, y);
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (w));
    };

    drawCurve (res.spectrumX, theme::reference.withAlpha (0.8f), 1.0f);
    drawCurve (res.spectrumY, theme::accent, 1.4f);

    g.setFont (theme::label (11.0f));
    auto legend = graphArea.reduced (52, 4).withHeight (14);
    g.setColour (theme::reference);
    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94 ")) + xName, legend, juce::Justification::centredLeft);
    g.setColour (theme::accent);
    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94 ")) + yName, legend.withTrimmedLeft (230), juce::Justification::centredLeft);
    g.setColour (theme::textFaint);
    g.drawText ("display-averaged, peak per pixel", legend, juce::Justification::centredRight);

    if (! chainMode && proc.sidechainState() == AtelierProcessor::SidechainState::notConnected)
    {
        g.setColour (theme::warn);
        g.drawText ("Sidechain not connected: only the processed signal is shown.", r.withTrimmedTop (r.getHeight() - 18.0f),
                    juce::Justification::centredLeft);
    }
}

void AnalysisPanel::drawTransfer (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& t = res.transfer;
    const double dbMin = -24.0, dbMax = 24.0;
    plot::drawFreqGrid (g, r, dbMin, dbMax, 6.0);

    g.setFont (theme::label (11.0f));
    auto legend = graphArea.reduced (52, 4).withHeight (14);

    if (! res.transferAvailable)
    {
        g.setColour (theme::warn);
        g.drawFittedText (res.transferNote.isNotEmpty() ? res.transferNote : "Waiting for data...",
                          r.reduced (20.0f).toNearestInt(), juce::Justification::centred, 3);
        return;
    }

    // 1/12-octave smoothing of magnitude (display only), coherence-weighted
    const size_t bins = t.magnitudeDb.size();
    juce::Path mag, ph;
    bool mStarted = false, pStarted = false;
    const int cols = (int) r.getWidth();
    const double fMax = juce::jmin (20000.0, t.sampleRate * 0.5);
    const double frac = std::pow (2.0, 1.0 / 24.0);
    for (int x = 0; x < cols; x += 2)
    {
        const double f = 20.0 * std::pow (1000.0, (double) x / cols);
        if (f > fMax) break;
        const int k0 = juce::jmax (1, (int) (f / frac / t.binHz));
        const int k1 = juce::jmin ((int) bins - 1, juce::jmax (k0, (int) (f * frac / t.binHz)));
        double sumP = 0.0, sumW = 0.0, sumPh = 0.0, coh = 0.0;
        for (int k = k0; k <= k1; ++k)
        {
            if (! t.usable[(size_t) k]) continue;
            const double w = t.coherence[(size_t) k];
            sumP += w * std::pow (10.0, t.magnitudeDb[(size_t) k] / 10.0);
            sumPh += w * t.phaseRad[(size_t) k];
            sumW += w;
            coh = juce::jmax (coh, w);
        }
        const float px = r.getX() + (float) x;
        // coherence strip at the bottom
        g.setColour (theme::good.withAlpha ((float) (0.15 + 0.6 * coh)));
        g.fillRect (px, r.getBottom() - (float) coh * 24.0f, 2.0f, (float) coh * 24.0f);

        if (sumW < 1e-6) { mStarted = pStarted = false; continue; }
        const double db = 10.0 * std::log10 (sumP / sumW);
        const float ym = plot::dbToY (juce::jlimit (dbMin, dbMax, db), r, dbMin, dbMax);
        const float yp = plot::dbToY (juce::radiansToDegrees (sumPh / sumW) / 7.5, r, dbMin, dbMax); // 7.5 deg per grid dB
        if (! mStarted) { mag.startNewSubPath (px, ym); mStarted = true; } else mag.lineTo (px, ym);
        if (! pStarted) { ph.startNewSubPath (px, yp); pStarted = true; } else ph.lineTo (px, yp);
    }
    g.setColour (theme::reference.withAlpha (0.6f));
    g.strokePath (ph, juce::PathStrokeType (1.0f));
    g.setColour (theme::accent);
    g.strokePath (mag, juce::PathStrokeType (1.8f));

    g.setColour (theme::textDim);
    const double delayMs = t.estimatedDelaySamples / t.sampleRate * 1000.0;
    g.drawText ("Magnitude (violet)  Phase (grey, 7.5 deg/grid dB, delay removed)  Coherence (green strip)",
                legend, juce::Justification::centredLeft);
    auto info = graphArea.reduced (52, 4).withTop (graphArea.getBottom() - 18).withHeight (14);
    g.drawText ("Mean coherence " + juce::String (t.meanCoherence, 3)
                    + "   |   path delay " + (t.broadband ? juce::String (t.estimatedDelaySamples) + " smp (" + juce::String (delayMs, 2) + " ms)"
                                                          : juce::String ("unreliable (narrowband)"))
                    + "   |   " + juce::String (t.segmentsAveraged) + " segments x " + juce::String (t.segmentSize),
                info, juce::Justification::centredLeft);
    drawTag (g, info.removeFromRight (150).withSizeKeepingCentre (150, 14), "ESTIMATED  -  LINEAR H1", theme::warn);
    if (! t.broadband && res.transferNote.isNotEmpty())
    {
        g.setColour (theme::warn);
        g.drawFittedText (res.transferNote, r.withHeight (40.0f).translated (0.0f, 6.0f).toNearestInt(), juce::Justification::topLeft, 2);
    }
}

void AnalysisPanel::drawHarmonics (juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour (theme::bg);
    g.fillRoundedRectangle (r.toFloat(), 2.0f);
    r = r.reduced (10);

    g.setColour (theme::textDim);
    g.setFont (theme::bold (10.5f));
    auto head = r.removeFromTop (16);
    g.drawText ("HARMONICS", head, juce::Justification::centredLeft);

    const auto& h = res.harmonics;
    if (! res.harmonicsAvailable)
    {
        g.setColour (theme::textFaint);
        g.setFont (theme::label (11.5f));
        g.drawFittedText (res.harmonicsNote.isNotEmpty() ? res.harmonicsNote
                                                         : "Play a sine (Generator) for an exact harmonic measurement.",
                          r.removeFromTop (60), juce::Justification::topLeft, 4);
    }
    else
    {
        drawTag (g, head.removeFromRight (96), res.f0Estimated ? "f0 ESTIMATED" : "MEASURED",
                 res.f0Estimated ? theme::warn : theme::good);
        r.removeFromTop (6);

        g.setFont (theme::mono (11.0f));
        g.setColour (theme::text);
        g.drawText ("f0  " + juce::String (h.fundamentalHz, 1) + " Hz   H1 " + juce::String (h.levelDbfs[0], 2) + " dBFS",
                    r.removeFromTop (16), juce::Justification::centredLeft);
        r.removeFromTop (6);

        // Bars H2..H10 in dBc, -140..0
        auto bars = r.removeFromTop (110);
        const int n = juce::jmax (1, h.numHarmonics - 1);
        const float bw = (float) bars.getWidth() / 9.0f;
        for (int i = 1; i < h.numHarmonics; ++i)
        {
            const double dbc = juce::jlimit (-140.0, 0.0, h.levelDbc[(size_t) i]);
            const float height = (float) ((dbc + 140.0) / 140.0) * (float) (bars.getHeight() - 14);
            auto b = juce::Rectangle<float> ((float) bars.getX() + (float) (i - 1) * bw + 2.0f,
                                             (float) bars.getBottom() - 14.0f - height, bw - 4.0f, height);
            g.setColour (i % 2 == 1 ? theme::accent : theme::accent.withMultipliedBrightness (0.7f)); // even (H2,H4..) brighter
            g.fillRect (b);
            g.setColour (theme::textFaint);
            g.setFont (theme::mono (9.0f));
            g.drawText (juce::String (i + 1), juce::Rectangle<float> (b.getX(), (float) bars.getBottom() - 13.0f, b.getWidth(), 12.0f),
                        juce::Justification::centred);
        }
        juce::ignoreUnused (n);
        r.removeFromTop (6);

        g.setFont (theme::mono (10.5f));
        auto line = [&] (const juce::String& a, const juce::String& b)
        {
            auto row = r.removeFromTop (15);
            g.setColour (theme::textDim);
            g.drawText (a, row, juce::Justification::centredLeft);
            g.setColour (theme::text);
            g.drawText (b, row, juce::Justification::centredRight);
        };
        line ("H2 / H3", juce::String (h.levelDbc.size() > 1 ? h.levelDbc[1] : -200.0, 1) + " / "
                             + juce::String (h.levelDbc.size() > 2 ? h.levelDbc[2] : -200.0, 1) + " dBc");
        line ("THD", juce::String (h.thdPercent, h.thdPercent < 0.1 ? 4 : 2) + " %  (" + juce::String (h.thdDb, 1) + " dB)");
        line ("THD+N", juce::String (h.thdNDb, 1) + " dB");
        line ("Noise floor (median bin)", juce::String (h.noiseFloorDbfs, 1) + " dBFS");
    }

    r.removeFromTop (6);
    g.setFont (theme::mono (10.5f));
    auto line2 = [&] (const juce::String& a, const juce::String& b)
    {
        auto row = r.removeFromTop (15);
        g.setColour (theme::textDim);
        g.drawText (a, row, juce::Justification::centredLeft);
        g.setColour (theme::text);
        g.drawText (b, row, juce::Justification::centredRight);
    };
    line2 ("RMS / Peak", juce::String (res.rmsYdB, 1) + " / " + juce::String (res.peakYdB, 1) + " dBFS");
    line2 ("Crest factor", juce::String (res.crestDb, 1) + " dB");

    // Test conditions (always shown)
    g.setColour (theme::textFaint);
    g.setFont (theme::label (10.5f));
    const juce::String point = res.mode == Mode::chain ? "chain output" : "main input (processed)";
    g.drawFittedText ("Conditions: " + point + ", left ch, FFT " + juce::String (res.fftSize) + ", "
                          + sonic::windowName (res.window) + ", " + juce::String (res.sampleRate / 1000.0, 1)
                          + " kHz, unaveraged. Band 20 Hz - 20 kHz.",
                      r.removeFromBottom (44), juce::Justification::bottomLeft, 3);
}
