#include "Figures.h"

#include <sonic/modules/CompressorModule.h>

namespace fig
{
juce::Rectangle<float> sheet (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& caption, const juce::String& note)
{
    auto head = r.removeFromTop (16.0f);
    g.setFont (theme::mono (9.5f));
    g.setColour (theme::textDim);
    g.drawText (caption.toUpperCase(), head, juce::Justification::centredLeft, true);
    if (note.isNotEmpty())
    {
        g.setColour (theme::textFaint);
        g.drawText (note.toUpperCase(), head, juce::Justification::centredRight, true);
    }
    r.removeFromTop (3.0f);
    g.setColour (theme::sheet);
    g.fillRect (r);
    g.setColour (theme::ink);
    g.drawRect (r, 1.0f);
    return r.reduced (1.0f);
}

void logFreqAxis (juce::Graphics& g, juce::Rectangle<float> p, bool labels, double fMin, double fMax)
{
    auto x = [&] (double f) { return p.getX() + (float) (std::log (f / fMin) / std::log (fMax / fMin)) * p.getWidth(); };
    for (double dec : { 10.0, 100.0, 1000.0, 10000.0 })
        for (int k = 1; k <= 9; ++k)
        {
            const double f = dec * k;
            if (f < fMin || f > fMax) continue;
            g.setColour (k == 1 ? theme::gridMajor : theme::grid);
            g.drawVerticalLine ((int) x (f), p.getY(), p.getBottom());
        }
    if (! labels) return;
    g.setFont (theme::mono (9.0f));
    g.setColour (theme::textFaint);
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        if (f > fMin && f < fMax)
            g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f),
                        juce::Rectangle<float> (x (f) - 16.0f, p.getBottom() - 13.0f, 32.0f, 12.0f), juce::Justification::centred);
}

void dbAxis (juce::Graphics& g, juce::Rectangle<float> p, double dbMin, double dbMax, double step, const juce::String& unit)
{
    g.setFont (theme::mono (9.0f));
    for (double d = std::ceil (dbMin / step) * step; d <= dbMax + 1e-9; d += step)
    {
        const float y = plot::dbToY (d, p, dbMin, dbMax);
        g.setColour (std::abs (d) < 1e-9 ? theme::edgeHi : theme::grid);
        g.drawHorizontalLine ((int) y, p.getX(), p.getRight());
        if (y > p.getY() + 8.0f && y < p.getBottom() - 4.0f)
        {
            g.setColour (theme::textFaint);
            g.drawText ((d > 0 ? "+" : "") + numText (d, 0) + unit, juce::Rectangle<float> (p.getX() + 3.0f, y - 11.0f, 60.0f, 10.0f),
                        juce::Justification::centredLeft);
        }
    }
}

// even harmonics: solid ink; odd harmonics: an ink outline on paper
void harmonicBar (juce::Graphics& g, juce::Rectangle<float> bar, int k, float alpha)
{
    if (bar.getHeight() <= 0.0f) return;
    const auto c = theme::ink.withMultipliedAlpha (alpha);
    if (k % 2 == 0) { g.setColour (c); g.fillRect (bar); }
    else { g.setColour (theme::card); g.fillRect (bar); g.setColour (c); g.drawRect (bar, 1.0f); }
}
}

namespace
{
    juce::Path curvePath (const std::vector<float>& xs, const std::vector<float>& ys, std::function<float (float)> mx, std::function<float (float)> my)
    {
        juce::Path p;
        for (size_t i = 0; i < xs.size() && i < ys.size(); ++i)
        {
            const float x = mx (xs[i]), y = my (ys[i]);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        return p;
    }

    void bigNumber (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& label, const juce::String& value, juce::Colour c = theme::ink)
    {
        g.setColour (theme::textFaint);
        g.setFont (theme::mono (9.0f));
        g.drawText (label.toUpperCase(), r.removeFromTop (12.0f), juce::Justification::centredLeft);
        g.setColour (c);
        g.setFont (theme::numeral (24.0f));
        g.drawText (value, r.removeFromTop (28.0f), juce::Justification::centredLeft);
    }

    const char* kIntervals[] = { "unison", "minor second", "major second", "minor third", "major third", "fourth", "tritone",
                                 "fifth", "minor sixth", "major sixth", "minor seventh", "major seventh", "octave" };
}

// ================================================================== BlockFigure

BlockFigure::BlockFigure (Ui& u, int id, PreviewEngine& e, unsigned only) : ui (u), nodeId (id), engine (e)
{
    if (auto* n = ui.proc.graph().node (nodeId))
        type = n->type;
    kinds = sonic::preview::kindsFor (type) & only;
    setInterceptsMouseClicks (false, false);
    startTimerHz (20);
    timerCallback();
}

juce::uint64 BlockFigure::stampNow() const
{
    auto* n = ui.proc.graph().node (nodeId);
    if (n == nullptr) return 0;
    juce::uint64 h = n->module->paramVersion();
    h = h * 1000003ull + (juce::uint64) juce::roundToInt (n->mix.load() * 10000.0f);
    h = h * 1000003ull + (juce::uint64) juce::roundToInt ((n->outGainDb.load() + 100.0f) * 100.0f);
    h = h * 1000003ull + (juce::uint64) ui.proc.currentSampleRate();
    return h + 1;
}

void BlockFigure::timerCallback()
{
    auto* n = ui.proc.graph().node (nodeId);
    if (n == nullptr) return;
    if (kinds != 0)
    {
        const auto s = stampNow();
        if (s != requested)
        {
            sonic::preview::Settings st;
            st.type = n->type;
            for (int i = 0; i < n->module->numParams(); ++i)
                st.params.push_back (n->module->getParam (i));
            st.mix = n->mix.load();
            st.outDb = n->outGainDb.load();
            st.sampleRate = juce::jlimit (22050.0, 192000.0, ui.proc.currentSampleRate());
            engine.request (nodeId, std::move (st), kinds, s);
            requested = s;
        }
        if (engine.fetch (nodeId, received, res))
        {
            have = true;
            repaint();
        }
    }
    if (type == "dynamics.compressor")
    {
        if (grHistory.empty()) grHistory.assign (200, 0.0f);
        grHistory.erase (grHistory.begin());
        grHistory.push_back (n->enabled.load() ? n->module->meter (0) : 0.0f);
        repaint();
    }
}

void BlockFigure::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    if (kinds == 0)
        return;
    if (! have)
    {
        g.setColour (theme::textFaint);
        g.setFont (theme::mono (9.5f));
        g.drawText ("MEASURING...", r, juce::Justification::centred);
        return;
    }
    using namespace sonic::preview;
    const float gap = 14.0f;
    if ((kinds & (response | harmonics | transfer)) == (response | harmonics | transfer))
    {
        auto top = r.removeFromTop (r.getHeight() * 0.46f);
        drawResponse (g, top, false);
        r.removeFromTop (gap);
        auto left = r.removeFromLeft (r.getHeight() * 1.05f);
        drawTransfer (g, left);
        r.removeFromLeft (gap);
        drawHarmonics (g, r);
    }
    else if ((kinds & (harmonics | transfer)) == (harmonics | transfer) && r.getHeight() > r.getWidth() * 0.9f)
    {
        drawTransfer (g, r.removeFromTop (juce::jmin (r.getWidth() * 0.8f, r.getHeight() * 0.5f)));
        r.removeFromTop (gap);
        drawHarmonics (g, r);
    }
    else if ((kinds & (harmonics | transfer)) == (harmonics | transfer))
    {
        auto left = r.removeFromLeft (juce::jmin (r.getHeight(), r.getWidth() * 0.45f));
        drawTransfer (g, left);
        r.removeFromLeft (gap);
        drawHarmonics (g, r);
    }
    else if ((kinds & (response | harmonics)) == (response | harmonics))
    {
        drawResponse (g, r.removeFromTop (r.getHeight() * 0.55f), false);
        r.removeFromTop (gap);
        drawHarmonics (g, r);
    }
    else if ((kinds & (response | transfer)) == (response | transfer))
    {
        auto left = r.removeFromLeft (juce::jmin (r.getHeight(), r.getWidth() * 0.45f));
        drawTransfer (g, left);
        r.removeFromLeft (gap);
        drawResponse (g, r, false);
    }
    else if (kinds & response) drawResponse (g, r, true);
    else if (kinds & dynamics) drawDynamics (g, r);
    else if (kinds & envelope) drawEnvelope (g, r);
    else if (kinds & impulse) drawImpulse (g, r);
    else if (kinds & modmap) drawModMap (g, r);
    else if (kinds & pitch) drawPitch (g, r);
    else if (kinds & stereo) drawStereo (g, r);
}

// ------------------------------------------------------------------ response

void BlockFigure::drawResponse (juce::Graphics& g, juce::Rectangle<float> r, bool big)
{
    if (res.respDb.empty()) return;
    // range: fit the curve, at least +-3 dB
    double mx = 3.0;
    for (auto v : res.respDb) mx = juce::jmax (mx, (double) std::abs (v));
    for (auto v : res.respLoDb) mx = juce::jmax (mx, (double) std::abs (v));
    for (auto v : res.respHiDb) mx = juce::jmax (mx, (double) std::abs (v));
    const double span = mx <= 3.0 ? 3.0 : mx <= 6.0 ? 6.0 : mx <= 12.0 ? 12.0 : mx <= 24.0 ? 24.0 : 48.0;
    const double lo = big || span > 12.0 ? -span : -span, hi = span;
    const auto p = fig::sheet (g, r, "Frequency response", juce::String (juce::CharPointer_UTF8 ("small signal \xc2\xb7 measured")));
    fig::logFreqAxis (g, p);
    fig::dbAxis (g, p, lo, hi, span >= 24 ? 12.0 : span >= 12 ? 6.0 : span >= 6 ? 3.0 : 1.0, " dB");
    auto mxF = [&] (float f) { return plot::freqToX (f, p); };
    auto myF = [&] (float d) { return plot::dbToY (juce::jlimit (lo - 1.0, hi + 1.0, (double) d), p, lo, hi); };
    g.saveState();
    g.reduceClipRegion (p.toNearestInt());
    if (! res.respLoDb.empty() && ! res.respHiDb.empty())
    {
        juce::Path band = curvePath (res.respHz, res.respHiDb, mxF, myF);
        for (size_t i = res.respHz.size(); i-- > 0;)
            band.lineTo (mxF (res.respHz[i]), myF (res.respLoDb[i]));
        band.closeSubPath();
        theme::hatch (g, band, theme::accent, 0.45f, 4.0f);
        g.setColour (theme::textFaint);
        g.setFont (theme::mono (9.0f));
        g.drawText ("HATCHED: RANGE OF THE LFO SWEEP", p.reduced (6.0f, 4.0f), juce::Justification::topRight);
    }
    auto path = curvePath (res.respHz, res.respDb, mxF, myF);
    juce::Path area (path);
    const float y0 = myF (0.0f);
    area.lineTo (p.getRight(), y0);
    area.lineTo (p.getX(), y0);
    area.closeSubPath();
    theme::hatch (g, area, theme::ink, 0.18f, 5.0f);
    g.setColour (theme::ink);
    g.strokePath (path, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.restoreState();
}

// ------------------------------------------------------------------ harmonics

void BlockFigure::drawHarmonics (juce::Graphics& g, juce::Rectangle<float> r)
{
    if (res.harmDbc.empty()) return;
    auto numbers = r.removeFromRight (juce::jmin (120.0f, r.getWidth() * 0.32f));
    const auto p = fig::sheet (g, r, "Harmonics",
                               juce::String (juce::roundToInt (res.testHz)) + " Hz at " + juce::String (juce::roundToInt (res.testDbfs)) + " dBFS");
    const double floorDb = -100.0;
    fig::dbAxis (g, p.withTrimmedBottom (14.0f), floorDb, 0.0, 20.0);
    const int n = (int) res.harmDbc.size();
    const float bw = (p.getWidth() - 16.0f) / (float) n;
    auto bars = p.withTrimmedBottom (14.0f);
    for (int k = 0; k < n; ++k)
    {
        const float v = res.harmDbc[(size_t) k];
        const float h = juce::jlimit (0.0f, 1.0f, (float) ((v - floorDb) / -floorDb)) * bars.getHeight();
        const float x = p.getX() + 8.0f + k * bw;
        fig::harmonicBar (g, juce::Rectangle<float> (x + bw * 0.25f, bars.getBottom() - h, bw * 0.5f, h), k);
        g.setColour (theme::textDim);
        g.setFont (theme::mono (9.0f));
        g.drawText ("H" + juce::String (k + 2), juce::Rectangle<float> (x, p.getBottom() - 13.0f, bw, 12.0f), juce::Justification::centred);
    }
    numbers.removeFromLeft (14.0f);
    numbers.removeFromTop (22.0f);
    bigNumber (g, numbers.removeFromTop (46.0f), "THD", numText (res.thdPct, res.thdPct < 1.0 ? 2 : 1) + " %");
    const int even = juce::roundToInt (res.evenPct);
    bigNumber (g, numbers.removeFromTop (46.0f), "Even : odd", juce::String (even) + " : " + juce::String (100 - even));
    g.setColour (theme::textFaint);
    g.setFont (theme::mono (9.0f));
    g.drawFittedText ("SOLID = EVEN (WARM)\nOUTLINE = ODD (EDGY)", numbers.removeFromTop (30.0f).toNearestInt(), juce::Justification::topLeft, 2);
}

// ------------------------------------------------------------------ transfer

void BlockFigure::drawTransfer (juce::Graphics& g, juce::Rectangle<float> r)
{
    if (res.xyIn.empty()) return;
    const auto p = fig::sheet (g, r, "Transfer", "100 Hz, full scale");
    float mx = 1.0f;
    for (auto v : res.xyOut) mx = juce::jmax (mx, std::abs (v));
    mx *= 1.08f;
    auto sx = [&] (float v) { return p.getCentreX() + v / mx * p.getWidth() * 0.5f; };
    auto sy = [&] (float v) { return p.getCentreY() - v / mx * p.getHeight() * 0.5f; };
    g.setColour (theme::grid);
    g.drawHorizontalLine ((int) p.getCentreY(), p.getX(), p.getRight());
    g.drawVerticalLine ((int) p.getCentreX(), p.getY(), p.getBottom());
    // the straight line = no colour at all
    {
        juce::Path d, line;
        line.startNewSubPath (sx (-1.0f), sy (-1.0f));
        line.lineTo (sx (1.0f), sy (1.0f));
        const float dash[] = { 3.0f, 3.0f };
        juce::PathStrokeType (0.8f).createDashedStroke (d, line, dash, 2);
        g.setColour (theme::textFaint);
        g.fillPath (d);
    }
    juce::Path path;
    for (size_t i = 0; i < res.xyIn.size(); ++i)
    {
        const float x = sx (res.xyIn[i]), y = sy (res.xyOut[i]);
        if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
    }
    path.closeSubPath();
    g.setColour (theme::ink);
    g.strokePath (path, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (theme::textFaint);
    g.setFont (theme::mono (9.0f));
    g.drawText ("IN", p.reduced (4.0f), juce::Justification::bottomRight);
    g.drawText ("OUT", p.reduced (4.0f), juce::Justification::topLeft);
}

// ------------------------------------------------------------------ dynamics

void BlockFigure::drawDynamics (juce::Graphics& g, juce::Rectangle<float> r)
{
    const float side = juce::jmin (r.getHeight(), r.getWidth() * 0.44f);
    auto left = r.removeFromLeft (side);
    r.removeFromLeft (16.0f);

    // static curve
    {
        const auto p = fig::sheet (g, left, "Static curve", "in / out dB");
        const double lo = -60.0, hi = 0.0;
        for (double d = lo; d <= hi; d += 12.0)
        {
            g.setColour (theme::grid);
            g.drawHorizontalLine ((int) plot::dbToY (d, p, lo, hi), p.getX(), p.getRight());
            const float x = p.getX() + (float) ((d - lo) / (hi - lo)) * p.getWidth();
            g.drawVerticalLine ((int) x, p.getY(), p.getBottom());
        }
        auto sx = [&] (float d) { return p.getX() + (float) ((d - lo) / (hi - lo)) * p.getWidth(); };
        auto sy = [&] (float d) { return plot::dbToY (juce::jlimit (lo - 6.0, hi + 12.0, (double) d), p, lo, hi); };
        juce::Path unity, d;
        unity.startNewSubPath (sx ((float) lo), sy ((float) lo));
        unity.lineTo (sx ((float) hi), sy ((float) hi));
        const float dash[] = { 3.0f, 3.0f };
        juce::PathStrokeType (0.8f).createDashedStroke (d, unity, dash, 2);
        g.setColour (theme::textFaint);
        g.fillPath (d);
        // threshold
        const float tx = sx ((float) res.thresholdDb);
        g.setColour (theme::accent);
        g.drawVerticalLine ((int) tx, p.getY(), p.getBottom());
        g.setFont (theme::mono (9.0f));
        g.drawText ("THR", juce::Rectangle<float> (tx + 3.0f, p.getBottom() - 14.0f, 40.0f, 12.0f), juce::Justification::centredLeft);
        g.saveState();
        g.reduceClipRegion (p.toNearestInt());
        auto path = curvePath (res.staticIn, res.staticOut, sx, sy);
        g.setColour (theme::ink);
        g.strokePath (path, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // live operating point: where the curve gives the reduction the block applies right now
        const float gr = grHistory.empty() ? 0.0f : grHistory.back();
        if (gr < -0.2f && ! res.staticIn.empty())
        {
            const float slope0 = res.staticOut.front() - res.staticIn.front();   // makeup at low level
            for (size_t i = 0; i < res.staticIn.size(); ++i)
                if (res.staticOut[i] - res.staticIn[i] - slope0 <= gr)
                {
                    g.setColour (theme::oxblood);
                    g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ sx (res.staticIn[i]), sy (res.staticOut[i]) }));
                    break;
                }
        }
        g.restoreState();
        g.setColour (theme::textFaint);
        g.drawText ("IN", p.reduced (4.0f), juce::Justification::bottomRight);
        g.drawText ("OUT", p.reduced (4.0f), juce::Justification::topLeft);
    }

    // step response + live gain reduction
    auto top = r.removeFromTop (r.getHeight() * 0.62f);
    r.removeFromTop (12.0f);
    {
        const auto p = fig::sheet (g, top, "Attack and release",
                                   "1 kHz steps " + juce::String (juce::roundToInt (res.stepLowDb)) + " / " + juce::String (juce::roundToInt (res.stepHighDb)) + " dBFS");
        if (! res.stepMs.empty())
        {
            const double t0 = res.stepMs.front(), t1 = res.stepMs.back();
            double grMin = -3.0;
            for (auto v : res.stepGrDb) grMin = juce::jmin (grMin, (double) v);
            const double lo = std::floor (grMin / 3.0) * 3.0 - 1.0, hi = 3.0;
            fig::dbAxis (g, p, lo, hi, lo < -18 ? 6.0 : 3.0, " dB");
            auto sx = [&] (float t) { return p.getX() + (float) ((t - t0) / (t1 - t0)) * p.getWidth(); };
            auto sy = [&] (float d) { return plot::dbToY (juce::jlimit (lo, hi, (double) d), p, lo, hi); };
            // the loud part
            g.setColour (theme::logo.withAlpha (0.18f));
            g.fillRect (juce::Rectangle<float> (sx (0.0f), p.getY(), sx (700.0f) - sx (0.0f), p.getHeight()));
            g.setColour (theme::textFaint);
            g.setFont (theme::mono (9.0f));
            g.drawText ("LOUD", juce::Rectangle<float> (sx (0.0f) + 4.0f, p.getY() + 3.0f, 60.0f, 12.0f), juce::Justification::centredLeft);
            auto path = curvePath (res.stepMs, res.stepGrDb, sx, sy);
            juce::Path area (path);
            area.lineTo (p.getRight(), sy (0.0f));
            area.lineTo (p.getX(), sy (0.0f));
            area.closeSubPath();
            theme::hatch (g, area, theme::oxblood, 0.4f, 4.0f, false);
            g.setColour (theme::oxblood);
            g.strokePath (path, juce::PathStrokeType (1.6f));
            for (int ms = 0; ms <= (int) t1; ms += 250)
            {
                g.setColour (theme::textFaint);
                g.drawText (juce::String (ms) + (ms == 0 ? " ms" : ""), juce::Rectangle<float> (sx ((float) ms) - 2.0f, p.getBottom() - 13.0f, 50.0f, 12.0f),
                            juce::Justification::centredLeft);
            }
        }
    }
    {
        const auto p = fig::sheet (g, r, "Gain reduction now", grHistory.empty() ? juce::String() : numText (grHistory.back(), 1) + " dB");
        const double lo = -24.0;
        g.setColour (theme::grid);
        for (double d = -6.0; d >= lo; d -= 6.0)
            g.drawHorizontalLine ((int) plot::dbToY (d, p, lo, 0.0), p.getX(), p.getRight());
        juce::Path path;
        for (size_t i = 0; i < grHistory.size(); ++i)
        {
            const float x = p.getX() + p.getWidth() * (float) i / (float) (grHistory.size() - 1);
            const float y = plot::dbToY (juce::jlimit (lo, 0.0, (double) grHistory[i]), p, lo, 0.0);
            if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }
        juce::Path area (path);
        area.lineTo (p.getRight(), p.getY());
        area.lineTo (p.getX(), p.getY());
        area.closeSubPath();
        theme::hatch (g, area, theme::oxblood, 0.35f, 4.0f, false);
        g.setColour (theme::oxblood);
        g.strokePath (path, juce::PathStrokeType (1.4f));
    }
}

// ------------------------------------------------------------------ envelope (transient shaper)

void BlockFigure::drawEnvelope (juce::Graphics& g, juce::Rectangle<float> r)
{
    if (res.envMs.empty()) return;
    const auto p = fig::sheet (g, r, "One drum hit, before and after", juce::String (juce::CharPointer_UTF8 ("dotted = in \xc2\xb7 solid = out")));
    const double lo = -60.0, hi = 6.0;
    fig::dbAxis (g, p, lo, hi, 12.0, " dB");
    const double t0 = res.envMs.front(), t1 = res.envMs.back();
    auto sx = [&] (float t) { return p.getX() + (float) ((t - t0) / (t1 - t0)) * p.getWidth(); };
    auto sy = [&] (float d) { return plot::dbToY (juce::jlimit (lo, hi, (double) d), p, lo, hi); };
    g.saveState();
    g.reduceClipRegion (p.toNearestInt());
    auto in = curvePath (res.envMs, res.envInDb, sx, sy);
    auto out = curvePath (res.envMs, res.envOutDb, sx, sy);
    // hatch the difference: where the block adds (camel) or removes (oxblood)
    for (size_t i = 1; i < res.envMs.size(); ++i)
    {
        const float d = res.envOutDb[i] - res.envInDb[i];
        if (std::abs (d) < 0.5f) continue;
        juce::Path q;
        q.startNewSubPath (sx (res.envMs[i - 1]), sy (res.envInDb[i - 1]));
        q.lineTo (sx (res.envMs[i]), sy (res.envInDb[i]));
        q.lineTo (sx (res.envMs[i]), sy (res.envOutDb[i]));
        q.lineTo (sx (res.envMs[i - 1]), sy (res.envOutDb[i - 1]));
        q.closeSubPath();
        g.setColour ((d > 0 ? theme::logo : theme::oxblood).withAlpha (0.35f));
        g.fillPath (q);
    }
    juce::Path dashed;
    const float dash[] = { 2.0f, 2.5f };
    juce::PathStrokeType (1.2f).createDashedStroke (dashed, in, dash, 2);
    g.setColour (theme::textDim);
    g.fillPath (dashed);
    g.setColour (theme::ink);
    g.strokePath (out, juce::PathStrokeType (1.8f));
    g.restoreState();
    g.setColour (theme::textFaint);
    g.setFont (theme::mono (9.0f));
    for (int ms = 0; ms <= (int) t1; ms += 100)
        g.drawText (juce::String (ms) + " ms", juce::Rectangle<float> (sx ((float) ms) + 2.0f, p.getBottom() - 13.0f, 60.0f, 12.0f), juce::Justification::centredLeft);
    g.drawText ("CAMEL = ADDED   OXBLOOD = REMOVED", p.reduced (6.0f, 4.0f), juce::Justification::topRight);
}

// ------------------------------------------------------------------ impulse (delay / reverb)

void BlockFigure::drawImpulse (juce::Graphics& g, juce::Rectangle<float> r)
{
    if (res.irMs.empty()) return;
    const bool reverb = type == "space.reverb";
    juce::Rectangle<float> numbers;
    if (reverb) numbers = r.removeFromRight (128.0f);
    const double lo = reverb ? -84.0 : -60.0;
    const auto p = fig::sheet (g, r, reverb ? "Reverb tail" : "Echoes",
                               reverb ? juce::String (juce::CharPointer_UTF8 ("left above \xc2\xb7 right below \xc2\xb7 wet only"))
                                      : juce::String (juce::CharPointer_UTF8 ("left above \xc2\xb7 right below \xc2\xb7 wet only")));
    const double t1 = res.irMs.back();
    auto sx = [&] (float t) { return p.getX() + (float) (t / t1) * p.getWidth(); };
    const float mid = p.getCentreY();
    auto h = [&] (float db) { return juce::jlimit (0.0f, 1.0f, (float) ((db - lo) / -lo)) * (p.getHeight() * 0.5f - 4.0f); };
    g.setColour (theme::edgeHi);
    g.drawHorizontalLine ((int) mid, p.getX(), p.getRight());
    for (double db = -12.0; db >= lo; db -= 12.0)
    {
        g.setColour (theme::grid);
        g.drawHorizontalLine ((int) (mid - h ((float) db)), p.getX(), p.getRight());
        g.drawHorizontalLine ((int) (mid + h ((float) db)), p.getX(), p.getRight());
    }
    g.saveState();
    g.reduceClipRegion (p.toNearestInt());
    if (reverb)
    {
        juce::Path up, down;
        up.startNewSubPath (p.getX(), mid);
        down.startNewSubPath (p.getX(), mid);
        for (size_t i = 0; i < res.irMs.size(); ++i)
        {
            up.lineTo (sx (res.irMs[i]), mid - h (res.irL[i]));
            down.lineTo (sx (res.irMs[i]), mid + h (res.irR[i]));
        }
        up.lineTo (p.getRight(), mid); up.closeSubPath();
        down.lineTo (p.getRight(), mid); down.closeSubPath();
        theme::hatch (g, up, theme::ink, 0.5f, 3.5f);
        theme::hatch (g, down, theme::ink, 0.5f, 3.5f, false);
        g.setColour (theme::ink);
        g.strokePath (up, juce::PathStrokeType (1.0f));
        g.strokePath (down, juce::PathStrokeType (1.0f));
    }
    else
    {
        for (size_t i = 0; i < res.irMs.size(); ++i)
        {
            const float x = sx (res.irMs[i]);
            if (res.irL[i] > lo + 1.0f) { g.setColour (theme::ink); g.drawLine (x, mid, x, mid - h (res.irL[i]), 1.4f); }
            if (res.irR[i] > lo + 1.0f) { g.setColour (theme::accent); g.drawLine (x, mid, x, mid + h (res.irR[i]), 1.4f); }
        }
    }
    g.restoreState();
    g.setColour (theme::textFaint);
    g.setFont (theme::mono (9.0f));
    const double step = t1 > 4000 ? 1000.0 : t1 > 1500 ? 500.0 : t1 > 600 ? 200.0 : 100.0;
    for (double t = 0; t <= t1; t += step)
        g.drawText (t >= 1000 ? numText (t / 1000.0, 1) + " s" : juce::String ((int) t) + " ms",
                    juce::Rectangle<float> (sx ((float) t) + 2.0f, p.getBottom() - 13.0f, 60.0f, 12.0f), juce::Justification::centredLeft);
    g.drawText ("L", p.reduced (6.0f, 4.0f), juce::Justification::topLeft);
    g.drawText ("R", p.reduced (6.0f, 4.0f), juce::Justification::bottomLeft);
    if (reverb)
    {
        numbers.removeFromLeft (16.0f);
        numbers.removeFromTop (22.0f);
        bigNumber (g, numbers.removeFromTop (48.0f), "RT60 (measured)", numText (res.rt60, 2) + " s");
        bigNumber (g, numbers.removeFromTop (48.0f), "Lows < 250 Hz", numText (res.rtLow, 2) + " s");
        bigNumber (g, numbers.removeFromTop (48.0f), "Mids ~1 kHz", numText (res.rtMid, 2) + " s");
        bigNumber (g, numbers.removeFromTop (48.0f), "Highs > 4 kHz", numText (res.rtHigh, 2) + " s");
    }
}

// ------------------------------------------------------------------ modulation map

void BlockFigure::drawModMap (juce::Graphics& g, juce::Rectangle<float> r)
{
    if (res.mapRows == 0) return;
    const auto p = fig::sheet (g, r, "Response over time",
                               juce::String (juce::CharPointer_UTF8 ("dark = cancelled \xc2\xb7 camel = boosted \xc2\xb7 ")) + numText (res.mapSeconds, 1) + " s");
    // time runs left to right, frequency bottom to top
    const float cw = p.getWidth() / (float) res.mapRows;
    const float ch = p.getHeight() / (float) res.mapCols;
    for (int t = 0; t < res.mapRows; ++t)
        for (int f = 0; f < res.mapCols; ++f)
        {
            const float d = res.mapDb[(size_t) (t * res.mapCols + f)];
            juce::Colour c;
            if (d < 0.0f) c = theme::ink.withAlpha (juce::jlimit (0.0f, 0.92f, -d / 24.0f));
            else c = theme::logo.withAlpha (juce::jlimit (0.0f, 0.9f, d / 9.0f));
            g.setColour (c);
            g.fillRect (juce::Rectangle<float> (p.getX() + t * cw, p.getBottom() - (f + 1) * ch, cw + 0.6f, ch + 0.6f));
        }
    g.setFont (theme::mono (9.0f));
    for (double f : { 100.0, 1000.0, 10000.0 })
    {
        const float y = p.getBottom() - (float) (std::log (f / 20.0) / std::log (1000.0)) * p.getHeight();
        g.setColour (theme::card.withAlpha (0.9f));
        g.fillRect (juce::Rectangle<float> (p.getX() + 3.0f, y - 6.0f, 28.0f, 12.0f));
        g.setColour (theme::textDim);
        g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), juce::Rectangle<float> (p.getX() + 3.0f, y - 6.0f, 28.0f, 12.0f), juce::Justification::centred);
        g.setColour (theme::edge.withAlpha (0.6f));
        g.drawHorizontalLine ((int) y, p.getX() + 32.0f, p.getRight());
    }
}

// ------------------------------------------------------------------ pitch

void BlockFigure::drawPitch (juce::Graphics& g, juce::Rectangle<float> r)
{
    if (res.specHz.empty()) return;
    auto info = r.removeFromRight (150.0f);
    const auto p = fig::sheet (g, r, "A 220 Hz tone, before and after", juce::String (juce::CharPointer_UTF8 ("dotted = in \xc2\xb7 solid = out")));
    const double fMin = res.specHz.front(), fMax = res.specHz.back();
    fig::logFreqAxis (g, p, true, fMin, fMax);
    const double lo = -70.0, hi = 0.0;
    auto sx = [&] (float f) { return plot::freqToX (f, p, fMin, fMax); };
    auto sy = [&] (float d) { return plot::dbToY (juce::jlimit (lo, hi, (double) d), p, lo, hi); };
    g.saveState();
    g.reduceClipRegion (p.toNearestInt());
    auto in = curvePath (res.specHz, res.specInDb, sx, sy);
    auto out = curvePath (res.specHz, res.specOutDb, sx, sy);
    juce::Path area (out);
    area.lineTo (p.getRight(), p.getBottom());
    area.lineTo (p.getX(), p.getBottom());
    area.closeSubPath();
    theme::hatch (g, area, theme::ink, 0.2f, 4.0f);
    juce::Path dashed;
    const float dash[] = { 2.0f, 2.0f };
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, in, dash, 2);
    g.setColour (theme::textDim);
    g.fillPath (dashed);
    g.setColour (theme::ink);
    g.strokePath (out, juce::PathStrokeType (1.4f));
    g.restoreState();

    if (auto* n = ui.proc.graph().node (nodeId))
    {
        const double st = n->module->getParam ("semitones") + n->module->getParam ("cents") * 0.01;
        info.removeFromLeft (16.0f);
        info.removeFromTop (22.0f);
        bigNumber (g, info.removeFromTop (48.0f), "Shift", (st > 0 ? "+" : "") + numText (st, std::abs (st - std::round (st)) > 0.005 ? 2 : 0) + " st");
        bigNumber (g, info.removeFromTop (48.0f), "220 Hz becomes", numText (220.0 * std::pow (2.0, st / 12.0), 1) + " Hz");
        const int whole = (int) std::lround (std::abs (st));
        juce::String name = whole <= 12 ? juce::String (kIntervals[whole]) : juce::String (whole / 12) + " oct + " + kIntervals[whole % 12];
        g.setColour (theme::textDim);
        g.setFont (theme::heading (17.0f));
        g.drawFittedText (name + (st < 0 ? " down" : st > 0 ? " up" : ""), info.removeFromTop (40.0f).toNearestInt(), juce::Justification::topLeft, 2);
    }
}

// ------------------------------------------------------------------ stereo

void BlockFigure::drawStereo (juce::Graphics& g, juce::Rectangle<float> r)
{
    if (res.scopeM.empty()) return;
    auto left = r.removeFromLeft (juce::jmin (r.getHeight(), r.getWidth() * 0.45f));
    r.removeFromLeft (16.0f);
    {
        const auto p = fig::sheet (g, left, "Image", "correlation " + numText (res.correlation, 2));
        g.setColour (theme::grid);
        g.drawVerticalLine ((int) p.getCentreX(), p.getY(), p.getBottom());
        g.drawHorizontalLine ((int) p.getCentreY(), p.getX(), p.getRight());
        g.setColour (theme::ink.withAlpha (0.55f));
        const float s = juce::jmin (p.getWidth(), p.getHeight()) * 0.46f;
        for (size_t i = 0; i < res.scopeM.size(); ++i)
            g.fillRect (p.getCentreX() + res.scopeS[i] * s - 0.6f, p.getCentreY() - res.scopeM[i] * s - 0.6f, 1.3f, 1.3f);
        g.setColour (theme::textFaint);
        g.setFont (theme::mono (9.0f));
        g.drawText ("MID", p.reduced (4.0f), juce::Justification::centredTop);
        g.drawText ("L", p.reduced (6.0f), juce::Justification::centredLeft);
        g.drawText ("R", p.reduced (6.0f), juce::Justification::centredRight);
    }
    {
        const auto p = fig::sheet (g, r, "Width per frequency", "100 % = unchanged");
        fig::logFreqAxis (g, p.withTrimmedBottom (0.0f), true, 30.0, 16000.0);
        const double hi = 220.0;
        auto sx = [&] (float f) { return plot::freqToX (f, p, 30.0, 16000.0); };
        auto sy = [&] (float w) { return p.getBottom() - juce::jlimit (0.0f, 1.0f, w / (float) hi) * p.getHeight(); };
        g.setColour (theme::edgeHi);
        g.drawHorizontalLine ((int) sy (100.0f), p.getX(), p.getRight());
        g.setFont (theme::mono (9.0f));
        g.setColour (theme::textFaint);
        g.drawText ("100 %", juce::Rectangle<float> (p.getX() + 3.0f, sy (100.0f) - 12.0f, 50.0f, 10.0f), juce::Justification::centredLeft);
        g.drawText ("MONO", juce::Rectangle<float> (p.getX() + 3.0f, p.getBottom() - 24.0f, 50.0f, 10.0f), juce::Justification::centredLeft);
        auto path = curvePath (res.widthHz, res.widthPct, sx, sy);
        juce::Path area (path);
        area.lineTo (p.getRight(), p.getBottom());
        area.lineTo (p.getX(), p.getBottom());
        area.closeSubPath();
        theme::hatch (g, area, theme::ink, 0.25f, 4.0f);
        g.setColour (theme::ink);
        g.strokePath (path, juce::PathStrokeType (1.8f));
    }
}
