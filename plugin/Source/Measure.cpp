#include "Measure.h"
#include "PluginProcessor.h"

#include <sonic/Analysis.h>
#include <sonic/DspBlocks.h>
#include <sonic/SignalGenerator.h>
#include <sonic/TransferAnalyzer.h>
#include <sonic/modules/ParametricEqModule.h>

namespace measure
{

namespace
{
    constexpr int kBlock = 512;

    std::vector<float> sine (double fs, double f, double db, int n)
    {
        std::vector<float> v ((size_t) n);
        const double a = std::pow (10.0, db / 20.0);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = (float) (a * std::sin (sonic::kTwoPi * f * i / fs));
        return v;
    }

    std::vector<float> pink (double fs, double db, int n, int skip = 0)
    {
        sonic::SignalGenerator g;
        g.prepare (fs);
        g.waveform = (int) sonic::SignalGenerator::Waveform::pinkNoise;
        g.levelDb = (float) db;
        std::vector<float> l ((size_t) (n + skip)), r ((size_t) (n + skip));
        for (int i = 0; i < n + skip; i += kBlock)
            g.render (l.data() + i, r.data() + i, std::min (kBlock, n + skip - i));
        return std::vector<float> (l.begin() + skip, l.end());
    }

    double rmsDb (const std::vector<float>& x, size_t from = 0)
    {
        double s = 0;
        for (size_t i = from; i < x.size(); ++i) s += (double) x[i] * x[i];
        return sonic::gainToDb (std::sqrt (s / std::max<size_t> (1, x.size() - from)), -200.0);
    }
}

void render (const juce::ValueTree& tree, double fs, const std::vector<float>& inL, const std::vector<float>& inR,
             std::vector<float>& outL, std::vector<float>& outR, int* latencyOut)
{
    sonic::Graph g;
    AtelierProcessor::treeToGraph (tree, g);
    g.prepare (fs, kBlock);
    for (int id : g.nodeIds())
        if (auto* n = g.node (id); n != nullptr && n->type == "tone.eq")
            static_cast<sonic::ParametricEqModule&> (*n->module).prepareOffline();

    // one silent block establishes the latency
    std::vector<float> zl (kBlock, 0.0f), zr (kBlock, 0.0f);
    float* z[2] = { zl.data(), zr.data() };
    g.process (z, 2, kBlock);
    const int lat = g.latencySamples();
    if (latencyOut) *latencyOut = lat;

    const int n = (int) inL.size();
    const int total = n + lat;
    std::vector<float> l ((size_t) total, 0.0f), r ((size_t) total, 0.0f);
    std::copy (inL.begin(), inL.end(), l.begin());
    std::copy (inR.begin(), inR.end(), r.begin());
    for (int i = 0; i < total; i += kBlock)
    {
        float* ch[2] = { l.data() + i, r.data() + i };
        g.process (ch, 2, std::min (kBlock, total - i));
    }
    outL.assign (l.begin() + lat, l.end());
    outR.assign (r.begin() + lat, r.end());
}

juce::ValueTree isolateBlock (const juce::ValueTree& tree, int id)
{
    juce::ValueTree t ("BRIDGE");
    for (const auto& c : tree)
        if (c.hasType ("BLOCK") && (int) c["id"] == id)
            t.appendChild (c.createCopy(), nullptr);
    juce::ValueTree a ("WIRE"), b ("WIRE");
    a.setProperty ("from", 0, nullptr); a.setProperty ("to", id, nullptr);
    b.setProperty ("from", id, nullptr); b.setProperty ("to", 1, nullptr);
    t.appendChild (a, nullptr);
    t.appendChild (b, nullptr);
    return t;
}

void referenceProgram (double fs, std::vector<float>& l, std::vector<float>& r)
{
    l.clear();
    auto append = [&] (const std::vector<float>& v) { l.insert (l.end(), v.begin(), v.end()); };
    append (pink (fs, -18.0, (int) (3 * fs)));
    append (std::vector<float> ((size_t) (0.5 * fs), 0.0f));
    {   // log sweep 20 Hz - 20 kHz, 5 s, -18 dBFS
        sonic::SignalGenerator g;
        g.prepare (fs);
        g.waveform = (int) sonic::SignalGenerator::Waveform::logSweep;
        g.levelDb = -18.0f;
        g.sweepSeconds = 5.0f;
        std::vector<float> a ((size_t) (5 * fs)), b ((size_t) (5 * fs));
        for (int i = 0; i < (int) a.size(); i += kBlock) g.render (a.data() + i, b.data() + i, std::min (kBlock, (int) a.size() - i));
        append (a);
    }
    append (std::vector<float> ((size_t) (0.5 * fs), 0.0f));
    {   // percussive bursts
        std::vector<float> d ((size_t) (4 * fs));
        for (size_t i = 0; i < d.size(); ++i)
        {
            const double t = std::fmod (i / fs, 0.5);
            const double env = std::exp (-t * 14.0);
            d[i] = (float) (0.5 * env * (std::sin (sonic::kTwoPi * 90.0 * t) * 0.8 + std::sin (sonic::kTwoPi * 2400.0 * t) * 0.2 * std::exp (-t * 60.0)));
        }
        append (d);
    }
    append (sine (fs, 1000.0, -12.0, (int) (2 * fs)));
    append (sine (fs, 100.0, -6.0, (int) (2 * fs)));
    r = l;
}

Result run (const juce::ValueTree& tree, double fs, std::function<void (double)> progress, std::function<bool()> stop)
{
    Result res;
    res.fs = fs;
    auto report = [&] (double p) { if (progress) progress (p); };
    std::vector<float> oL, oR;

    // ---- frequency response ----
    {
        sonic::Noise rng (0x1234);
        const int n = (int) (8 * fs);
        std::vector<float> x ((size_t) n);
        const double a = std::pow (10.0, -18.0 / 20.0) * std::sqrt (3.0);
        for (auto& v : x) v = (float) (a * rng.next());
        render (tree, fs, x, x, oL, oR, &res.latency);
        sonic::TransferAnalyzer ta;
        ta.prepare (fs, 16384, 0.0);
        ta.addBlock (x.data() + 16384, oL.data() + 16384, n - 16384);
        const auto t = ta.result();
        res.meanCoherence = t.meanCoherence;
        const double frac = std::pow (2.0, 1.0 / 24.0);
        for (int i = 0; i < 160; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, i / 159.0);
            if (f > fs * 0.48) break;
            const int k0 = std::max (1, (int) (f / frac / t.binHz)), k1 = std::min ((int) t.magnitudeDb.size() - 1, std::max (k0, (int) (f * frac / t.binHz)));
            double sp = 0, sph = 0, sw = 0, coh = 0;
            for (int k = k0; k <= k1; ++k)
            {
                if (! t.usable[(size_t) k]) continue;
                const double w = std::max (1e-3, t.coherence[(size_t) k]);
                sp += w * std::pow (10.0, t.magnitudeDb[(size_t) k] / 10.0);
                sph += w * t.phaseRad[(size_t) k];
                sw += w;
                coh += t.coherence[(size_t) k];
            }
            if (sw <= 0) continue;
            res.frF.push_back (f);
            res.frMag.push_back (10.0 * std::log10 (sp / sw));
            res.frPhaseDeg.push_back (sph / sw * 180.0 / sonic::kPi);
            res.frCoh.push_back (coh / (k1 - k0 + 1));
        }
    }
    report (0.15);
    if (stop && stop()) return res;

    // ---- harmonics vs level ----
    for (double lvl : { -36.0, -30.0, -24.0, -18.0, -12.0, -6.0, 0.0 })
    {
        auto x = sine (fs, 1000.0, lvl, (int) (1.5 * fs));
        render (tree, fs, x, x, oL, oR);
        auto sp = sonic::computeSpectrum (oL.data(), (int) oL.size(), 16384, fs, sonic::WindowType::blackmanHarris4);
        auto h = sonic::analyseHarmonics (sp, 1000.0, 10, std::min (20000.0, fs * 0.45));
        LevelPoint p;
        p.inDb = lvl;
        if (h.valid)
        {
            p.outDb = h.levelDbfs[0];
            p.thdPct = h.thdPercent;
            p.thdDb = h.thdDb;
            for (int k = 1; k < 10; ++k)
                p.hDbc.push_back (k < h.numHarmonics ? h.levelDbc[(size_t) k] : -200.0);
        }
        else
            p.outDb = -200.0;
        res.levels.push_back (p);
        if (lvl == -12.0)
        {
            res.spectrumBinHz = sp.binHz();
            for (double v : sp.amplitude)
                res.spectrumDb.push_back ((float) sonic::gainToDb (v, -200.0));
        }
    }
    report (0.35);
    if (stop && stop()) return res;

    // ---- THD vs frequency ----
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0 })
    {
        auto x = sine (fs, f, -12.0, (int) (1.5 * fs));
        render (tree, fs, x, x, oL, oR);
        auto sp = sonic::computeSpectrum (oL.data(), (int) oL.size(), 32768, fs, sonic::WindowType::blackmanHarris4);
        auto h = sonic::analyseHarmonics (sp, f, 10, std::min (20000.0, fs * 0.45));
        res.thdFreqF.push_back (f);
        res.thdFreqDb.push_back (h.valid ? h.thdDb : -200.0);
    }
    report (0.5);
    if (stop && stop()) return res;

    // ---- dynamics: step response ----
    {
        std::vector<float> x;
        for (auto [db, sec] : { std::pair<double, double> { -30.0, 1.0 }, { -6.0, 1.0 }, { -30.0, 2.0 } })
        {
            auto s = sine (fs, 1000.0, db, (int) (sec * fs));
            x.insert (x.end(), s.begin(), s.end());
        }
        render (tree, fs, x, x, oL, oR);
        const int frame = (int) (0.005 * fs);
        for (size_t i = 0; i + (size_t) frame <= x.size(); i += (size_t) frame)
        {
            double si = 0, so = 0;
            for (int k = 0; k < frame; ++k) { si += (double) x[i + (size_t) k] * x[i + (size_t) k]; so += (double) oL[i + (size_t) k] * oL[i + (size_t) k]; }
            res.envIn.push_back ((float) sonic::gainToDb (std::sqrt (si / frame), -200.0));
            res.envOut.push_back ((float) sonic::gainToDb (std::sqrt (so / frame), -200.0));
        }
        auto gr = [&] (size_t i) { return res.envOut[i] - res.envIn[i]; };
        const size_t up = 200, down = 400; // frame index of the steps (5 ms frames)
        if (res.envIn.size() > down + 300)
        {
            const double g0 = gr (up - 5), g1 = gr (down - 5), g2 = gr (res.envIn.size() - 5);
            res.grStepDb = g1 - g0;
            if (std::abs (g1 - g0) > 0.5)
            {
                for (size_t i = up; i < down; ++i)
                    if (std::abs (gr (i) - g0) >= 0.632 * std::abs (g1 - g0)) { res.attackMs = (double) (i - up) * 5.0; break; }
                for (size_t i = down; i < res.envIn.size(); ++i)
                    if (std::abs (gr (i) - g1) >= 0.632 * std::abs (g2 - g1)) { res.releaseMs = (double) (i - down) * 5.0; break; }
            }
        }
    }
    report (0.65);
    if (stop && stop()) return res;

    // ---- impulse response ----
    {
        std::vector<float> x ((size_t) (0.5 * fs), 0.0f);
        x[0] = 0.01f;
        render (tree, fs, x, x, oL, oR);
        res.ir.resize (oL.size());
        for (size_t i = 0; i < oL.size(); ++i) res.ir[i] = oL[i] * 100.0f;
    }
    report (0.72);

    // ---- stereo ----
    {
        const int n = (int) (3 * fs);
        auto a = pink (fs, -18.0, n);
        std::vector<float> zero ((size_t) n, 0.0f);
        render (tree, fs, a, zero, oL, oR);
        res.crosstalkDb = rmsDb (oR, (size_t) fs) - rmsDb (oL, (size_t) fs);

        render (tree, fs, a, a, oL, oR);
        auto corr = [] (const std::vector<float>& l, const std::vector<float>& r, size_t from)
        {
            double xy = 0, xx = 0, yy = 0;
            for (size_t i = from; i < l.size(); ++i) { xy += (double) l[i] * r[i]; xx += (double) l[i] * l[i]; yy += (double) r[i] * r[i]; }
            return (xx > 1e-12 && yy > 1e-12) ? xy / std::sqrt (xx * yy) : 0.0;
        };
        res.corrMono = corr (oL, oR, (size_t) fs);

        auto b = pink (fs, -18.0, n, 77777);
        render (tree, fs, a, b, oL, oR);
        res.corrWide = corr (oL, oR, (size_t) fs);
        double m = 0, s = 0;
        for (size_t i = (size_t) fs; i < oL.size(); ++i)
        {
            const double mm = 0.5 * (oL[i] + oR[i]), ss = 0.5 * (oL[i] - oR[i]);
            m += mm * mm; s += ss * ss;
        }
        res.sideToMidDb = m > 1e-12 ? 10.0 * std::log10 (s / m + 1e-20) : -200.0;
        for (size_t i = (size_t) fs; i < (size_t) fs + 4096 && i < oL.size(); ++i)
        {
            res.scopeL.push_back (oL[i]);
            res.scopeR.push_back (oR[i]);
        }
    }
    report (0.85);

    // ---- noise floor ----
    {
        std::vector<float> x ((size_t) (2 * fs), 0.0f);
        render (tree, fs, x, x, oL, oR);
        res.noiseFloorDb = rmsDb (oL, (size_t) (0.5 * fs));
    }

    // ---- IMD (SMPTE 60 Hz + 7 kHz, 4:1) ----
    {
        sonic::SignalGenerator g;
        g.prepare (fs);
        g.waveform = (int) sonic::SignalGenerator::Waveform::imdSmpte;
        g.levelDb = -12.0f;
        const int n = (int) (2 * fs);
        std::vector<float> l ((size_t) n), r ((size_t) n);
        for (int i = 0; i < n; i += kBlock) g.render (l.data() + i, r.data() + i, std::min (kBlock, n - i));
        render (tree, fs, l, r, oL, oR);
        auto sp = sonic::computeSpectrum (oL.data(), (int) oL.size(), 32768, fs, sonic::WindowType::blackmanHarris4);
        const double carrier = sp.toneAmplitude (7000.0, 5);
        double side = 0;
        for (int k = 1; k <= 3; ++k)
            for (int sgn : { -1, 1 })
            {
                const double a = sp.toneAmplitude (7000.0 + sgn * 60.0 * k, 3);
                side += a * a;
            }
        res.imdPct = carrier > 1e-9 ? std::sqrt (side) / carrier * 100.0 : 0.0;
    }
    report (1.0);
    return res;
}

} // namespace measure
