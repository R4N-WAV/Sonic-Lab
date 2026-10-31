// Atelier: graph engine + block tests. Every check measures real output.

#include "sonic/Analysis.h"
#include "sonic/DspUtils.h"
#include "sonic/Graph.h"
#include "sonic/ModuleRegistry.h"
#include "sonic/SignalGenerator.h"
#include "sonic/modules/CharacterModules.h"
#include "sonic/modules/CompressorModule.h"
#include "sonic/modules/EffectModules.h"
#include "sonic/modules/ParametricEqModule.h"
#include "sonic/modules/SaturationModule.h"
#include "sonic/modules/TubeModule.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace sonic;

namespace
{
int failures = 0, checks = 0;
void check (bool ok, const std::string& what)
{
    ++checks;
    std::printf ("  [%s] %s\n", ok ? " OK " : "FAIL", what.c_str());
    if (! ok) ++failures;
}
std::string fmt (const char* f, double a, double b = 0, double c = 0)
{
    char buf[300];
    std::snprintf (buf, sizeof buf, f, a, b, c);
    return buf;
}

constexpr double kFs = 48000.0;
constexpr int kBlock = 256;

struct Stereo
{
    std::vector<float> l, r;
    explicit Stereo (int n = 0) : l ((size_t) n, 0.0f), r ((size_t) n, 0.0f) {}
    int size() const { return (int) l.size(); }
};

Stereo sine (double f, double db, int n)
{
    SignalGenerator g;
    g.prepare (kFs);
    g.frequencyHz = (float) f;
    g.levelDb = (float) db;
    Stereo s (n);
    for (int i = 0; i < n; i += kBlock)
        g.render (s.l.data() + i, s.r.data() + i, std::min (kBlock, n - i));
    return s;
}

void run (Module& m, Stereo& s)
{
    for (int i = 0; i < s.size(); i += kBlock)
    {
        float* ch[2] = { s.l.data() + i, s.r.data() + i };
        m.process (ch, 2, std::min (kBlock, s.size() - i));
    }
}

void run (Graph& g, Stereo& s)
{
    for (int i = 0; i < s.size(); i += kBlock)
    {
        float* ch[2] = { s.l.data() + i, s.r.data() + i };
        g.process (ch, 2, std::min (kBlock, s.size() - i));
    }
}

HarmonicResult harm (const Stereo& s, double f0)
{
    auto spec = computeSpectrum (s.l.data(), s.size(), 16384, kFs, WindowType::blackmanHarris4);
    return analyseHarmonics (spec, f0, 10, 20000.0);
}

double toneDb (const std::vector<float>& x, double f)
{
    auto spec = computeSpectrum (x.data(), (int) x.size(), 16384, kFs, WindowType::blackmanHarris4);
    return gainToDb (spec.toneAmplitude (f, 5));
}

int peakIndex (const std::vector<float>& x)
{
    int p = 0;
    for (int i = 0; i < (int) x.size(); ++i)
        if (std::abs (x[(size_t) i]) > std::abs (x[(size_t) p])) p = i;
    return p;
}

// ------------------------------------------------------------------ graph

void testGraph()
{
    std::printf ("Graph (bridge)\n");
    Graph g;
    g.prepare (kFs, kBlock);
    {
        Stereo s = sine (1000, -12, 4096), ref = s;
        run (g, s);
        bool same = true;
        for (int i = 0; i < s.size(); ++i) same = same && s.l[(size_t) i] == ref.l[(size_t) i];
        check (same, "default bridge Input -> Output is bit-exact");
    }

    const int tube = g.addNode ("tube.stage");
    g.node (tube)->module->setParam ("drive_db", 0.0f);
    g.node (tube)->module->setParam ("sag", 0.0f);
    check (g.connect (Graph::kInputId, tube) && g.connect (tube, Graph::kOutputId), "connect Input -> Tube -> Output (parallel to dry)");
    check (! g.connect (tube, tube), "self-connection rejected");
    {
        Stereo s (4096);
        s.l[500] = s.r[500] = 0.01f;
        run (g, s);
        const int p = peakIndex (s.l);
        check (p == 564, fmt ("parallel paths aligned: single peak at %.0f (expected 564 = 500 + 64)", p));
        check (s.l[564] > 0.017f, fmt ("aligned sum = dry + tube peak = %.4f (both add in phase)", s.l[564]));
        check (std::abs (s.l[500]) < 1e-6f, "nothing leaks through un-delayed");
        check (g.latencySamples() == 64, fmt ("bridge reports latency %.0f", g.latencySamples()));
    }

    g.node (tube)->mix = 0.0f;
    {
        Stereo s (4096);
        for (int k = 0; k < 4; ++k) run (g, s); // settle mix ramp
        s = Stereo (4096);
        s.l[500] = s.r[500] = 0.01f;
        run (g, s);
        check (std::abs (s.l[564] - 0.02f) < 1e-6f, fmt ("Mix 0: block passes its delayed dry input exactly (%.6f)", s.l[564]));
    }
    g.node (tube)->mix = 1.0f;

    const int eq = g.addNode ("tone.eq");
    check (g.connect (tube, eq), "connect Tube -> EQ");
    check (! g.connect (eq, tube), "cycle EQ -> Tube rejected");
    check (g.wouldCreateCycle (eq, tube), "cycle detection");
    check (g.activeOrder() == std::vector<int> ({ tube }), "EQ without path to Output is not processed");
    g.connect (eq, Graph::kOutputId);
    check (g.activeOrder() == std::vector<int> ({ tube, eq }), "topological order Tube, EQ");

    // stress: edit structure while audio runs on another thread
    std::atomic<bool> stop { false };
    std::atomic<bool> finite { true };
    std::thread audio ([&]
    {
        Stereo s = sine (440, -12, kBlock);
        while (! stop)
        {
            float* ch[2] = { s.l.data(), s.r.data() };
            g.process (ch, 2, kBlock);
            for (float v : s.l) if (! std::isfinite (v)) finite = false;
            s = sine (440, -12, kBlock);
        }
    });
    const auto& mods = availableModules();
    for (int k = 0; k < 300; ++k)
    {
        const int id = g.addNode (mods[(size_t) k % mods.size()].typeId);
        g.connect (Graph::kInputId, id);
        g.connect (id, Graph::kOutputId);
        if (k % 3 == 0) g.removeNode (id);
        g.collectGarbage();
    }
    stop = true;
    audio.join();
    g.collectGarbage();
    check (finite.load(), "300 structure edits while processing: no crash, output finite");
}

// ------------------------------------------------------------------ saturation

void testSaturation()
{
    std::printf ("Saturation (harmonic recipes)\n");
    {
        SaturationModule m;
        for (int h = 2; h <= 10; ++h) m.setParam ("h" + std::to_string (h) + "_dbc", -90.0f);
        m.setParam ("h2_dbc", -20.0f);
        m.setParam ("h3_dbc", -30.0f);
        m.setParam ("level_dep", 0.0f);
        m.setParam ("softness", 0.0f);
        m.prepare (kFs, kBlock, 2);
        Stereo s = sine (1000, -12, 48000);
        run (m, s);
        auto h = harm (s, 1000);
        check (std::abs (h.levelDbc[1] + 20.0) < 1.0, fmt ("designed H2 -20 dBc -> measured %.2f dBc", h.levelDbc[1]));
        check (std::abs (h.levelDbc[2] + 30.0) < 1.0, fmt ("designed H3 -30 dBc -> measured %.2f dBc", h.levelDbc[2]));
        check (h.levelDbc[3] < -60.0, fmt ("undesigned H4 stays low (%.1f dBc)", h.levelDbc[3]));
    }
    {
        SaturationModule m;
        m.applyRecipe (3); // Square
        m.setParam ("level_dep", 0.0f);
        m.setParam ("softness", 0.0f);
        m.prepare (kFs, kBlock, 2);
        Stereo s = sine (1000, -6, 48000);
        run (m, s);
        auto h = harm (s, 1000);
        check (h.levelDbc[1] < -55.0 && std::abs (h.levelDbc[2] + 9.5) < 1.5,
               fmt ("recipe 'Square': odd only (H2 %.1f, H3 %.1f dBc)", h.levelDbc[1], h.levelDbc[2]));
    }
    {
        SaturationModule silky, square;
        silky.applyRecipe (0);
        square.applyRecipe (3);
        for (auto* m : { &silky, &square }) { m->setParam ("level_dep", 0.0f); m->prepare (kFs, kBlock, 2); }
        Stereo a = sine (1000, -6, 48000), b = a;
        run (silky, a);
        run (square, b);
        auto ha = harm (a, 1000), hb = harm (b, 1000);
        check (ha.levelDbc[1] > ha.levelDbc[2] && hb.levelDbc[2] > hb.levelDbc[1],
               fmt ("Silky is even-dominant, Square odd-dominant (H2-H3: %.1f vs %.1f dB)", ha.levelDbc[1] - ha.levelDbc[2], hb.levelDbc[1] - hb.levelDbc[2]));
    }
    {
        SaturationModule m;
        m.setParam ("level_dep", 1.0f);
        m.prepare (kFs, kBlock, 2);
        Stereo s = sine (1000, -60, 48000);
        run (m, s);
        auto h = harm (s, 1000);
        check (std::abs (h.levelDbfs[0] + 60.0) < 0.5, fmt ("auto gain: small signals at unity (-60 in -> %.2f out)", h.levelDbfs[0]));
    }
    {
        SaturationModule m;
        m.setParam ("focus_lo_hz", 200.0f);
        m.setParam ("drive_db", 0.0f);
        m.prepare (kFs, kBlock, 2);
        Stereo s (4096);
        s.l[300] = s.r[300] = 0.001f;
        run (m, s);
        check (peakIndex (s.l) - 300 == m.latencySamples(), fmt ("clean and saturated paths stay aligned (peak delay %.0f = latency %.0f)", peakIndex (s.l) - 300, m.latencySamples()));
    }
}

// ------------------------------------------------------------------ compressor

double outLevelDb (CompressorModule& m, double inDb, int n = 96000)
{
    m.prepare (kFs, kBlock, 2);
    Stereo s = sine (1000, inDb, n);
    run (m, s);
    return harm (s, 1000).levelDbfs[0];
}

void testCompressor()
{
    std::printf ("Compressor (5 topologies)\n");
    const char* names[] = { "Opto", "FET", "VCA", "Vari-Mu", "Diode" };
    for (int model : { 2, 1 })
    {
        CompressorModule m;
        m.setParam ("model", (float) model);
        m.setParam ("threshold_db", -24.0f);
        m.setParam ("ratio", 4.0f);
        m.setParam ("knee_db", 0.0f);
        m.setParam ("attack_ms", 1.0f);
        m.setParam ("release_ms", 100.0f);
        m.setParam ("color", 0.0f);
        const double out = outLevelDb (m, -6.0);
        const double pred = m.staticCurveDb (-6.0);
        check (std::abs (out - pred) < 1.0 && std::abs (pred + 19.5) < 0.05,
               std::string (names[model]) + fmt (" steady state: -6 dBFS in -> %.2f dBFS out (static curve %.2f, ideal -19.5)", out, pred));
    }
    for (int model = 0; model < 5; ++model)
    {
        CompressorModule m;
        m.setParam ("model", (float) model);
        const double out = outLevelDb (m, -3.0);
        check (out < -6.0, std::string (names[model]) + fmt (": compresses a loud tone (-3 dBFS -> %.1f dBFS)", out));
    }
    {
        // Opto programme dependence: release after a long burst is slower than after a short one
        auto grAfter = [] (double burstS)
        {
            CompressorModule m;
            m.setParam ("model", 0.0f);
            m.setParam ("threshold_db", -30.0f);
            m.setParam ("release_ms", 2000.0f);
            m.setParam ("program_dep", 1.0f);
            m.prepare (kFs, kBlock, 2);
            const int nb = (int) (burstS * kFs);
            Stereo s = sine (1000, -6, nb + (int) (0.3 * kFs));
            for (int i = nb; i < s.size(); ++i) s.l[(size_t) i] = s.r[(size_t) i] = 0.0f;
            run (m, s);
            return m.meter (0);
        };
        const double shortB = grAfter (0.15), longB = grAfter (3.0);
        check (longB < shortB - 1.0, fmt ("Opto: release is programme-dependent (GR 300 ms after burst: short %.1f dB, long %.1f dB)", shortB, longB));
    }
    {
        CompressorModule m;
        m.setParam ("model", 3.0f);
        m.setParam ("threshold_db", -30.0f);
        m.setParam ("ratio", 8.0f);
        const double s1 = m.staticCurveDb (-28.0) - m.staticCurveDb (-29.0);
        const double s2 = m.staticCurveDb (-6.0) - m.staticCurveDb (-7.0);
        check (s2 < s1 - 0.2, fmt ("Vari-Mu: ratio rises with level (slope %.2f near threshold, %.2f far above)", s1, s2));
    }
}

// ------------------------------------------------------------------ EQ

double measureGainDb (Module& m, double f, double levelDb = -20.0, int n = 48000)
{
    Stereo s = sine (f, levelDb, n);
    run (m, s);
    return toneDb (s.l, f) - levelDb;
}

void testEq()
{
    std::printf ("Parametric EQ\n");
    using EQ = ParametricEqModule;
    {
        EQ m;
        m.setParam (EQ::bandParam (0, EQ::fOn), 1);
        m.setParam (EQ::bandParam (0, EQ::fFreq), 1000);
        m.setParam (EQ::bandParam (0, EQ::fGain), 6);
        m.setParam (EQ::bandParam (1, EQ::fOn), 1);
        m.setParam (EQ::bandParam (1, EQ::fShape), (float) EQ::Shape::lowCut);
        m.setParam (EQ::bandParam (1, EQ::fFreq), 100);
        m.setParam (EQ::bandParam (1, EQ::fQ), 0.7071f);
        m.setParam (EQ::bandParam (1, EQ::fSlope), 3); // 24 dB/oct
        m.prepare (kFs, kBlock, 2);
        for (double f : { 50.0, 1000.0, 5000.0 })
        {
            m.reset();
            const double meas = measureGainDb (m, f);
            const double pred = m.responseDb (f);
            check (std::abs (meas - pred) < 0.15, fmt ("%.0f Hz: measured %.2f dB = displayed %.2f dB", f, meas, pred));
        }
        check (std::abs (m.responseDb (50.0) + 24.1) < 1.0, fmt ("24 dB/oct low cut: -%.1f dB one octave below", -m.responseDb (50.0)));
    }
    {
        EQ m;
        m.setParam (EQ::bandParam (0, EQ::fOn), 1);
        m.setParam (EQ::bandParam (0, EQ::fFreq), 1000);
        m.setParam (EQ::bandParam (0, EQ::fGain), -12);
        m.setParam (EQ::bandParam (0, EQ::fPlace), (float) EQ::Place::side);
        m.prepare (kFs, kBlock, 2);
        const double monoG = measureGainDb (m, 1000);
        check (std::abs (monoG) < 0.05, fmt ("Side band leaves a mono signal untouched (%.3f dB)", monoG));
        m.setParam (EQ::bandParam (0, EQ::fPlace), (float) EQ::Place::mid);
        m.reset();
        const double midG = measureGainDb (m, 1000);
        check (std::abs (midG + 12.0) < 0.1, fmt ("Mid band cuts a mono signal fully (%.2f dB)", midG));
    }
    {
        EQ m;
        m.setParam (EQ::bandParam (0, EQ::fOn), 1);
        m.setParam (EQ::bandParam (0, EQ::fFreq), 1000);
        m.setParam (EQ::bandParam (0, EQ::fDyn), 1);
        m.setParam (EQ::bandParam (0, EQ::fThr), -30);
        m.setParam (EQ::bandParam (0, EQ::fRange), -12);
        m.prepare (kFs, kBlock, 2);
        const double loud = measureGainDb (m, 1000, -6);
        m.reset();
        const double quiet = measureGainDb (m, 1000, -60);
        check (loud < -10.0 && std::abs (quiet) < 0.5, fmt ("dynamic band: loud %.1f dB, quiet %.2f dB", loud, quiet));
    }
    {
        EQ m;
        m.setParam (0, 1); // natural
        m.setParam (EQ::bandParam (0, EQ::fOn), 1);
        m.setParam (EQ::bandParam (0, EQ::fFreq), 12000);
        m.setParam (EQ::bandParam (0, EQ::fGain), 6);
        m.prepare (kFs, kBlock, 2);
        const double meas = measureGainDb (m, 12000);
        check (std::abs (meas - m.responseDb (12000)) < 0.2, fmt ("Natural mode: 12 kHz measured %.2f = displayed %.2f dB", meas, m.responseDb (12000)));
        check (m.latencySamples() == 64, "Natural mode latency 64");
    }
    {
        EQ m;
        m.setParam (0, 2); // linear
        m.setParam (EQ::bandParam (0, EQ::fOn), 1);
        m.setParam (EQ::bandParam (0, EQ::fFreq), 1000);
        m.setParam (EQ::bandParam (0, EQ::fGain), 6);
        m.prepare (kFs, kBlock, 2);
        Stereo warm (4096);
        run (m, warm); // activates the designer thread
        std::this_thread::sleep_for (std::chrono::milliseconds (300));
        Stereo s (16384);
        s.l[1000] = s.r[1000] = 0.1f;
        run (m, s);
        const int p = peakIndex (s.l);
        check (p - 1000 == m.latencySamples(), fmt ("Linear phase: impulse centre at reported latency (%.0f vs %.0f)", p - 1000, m.latencySamples()));
        double asym = 0.0;
        for (int k = 1; k < 400; ++k) asym = std::max (asym, (double) std::abs (s.l[(size_t) (p + k)] - s.l[(size_t) (p - k)]));
        check (asym < 1e-4, fmt ("Linear phase: impulse response symmetric (max asymmetry %.2e)", asym));
        const double meas = measureGainDb (m, 1000);
        check (std::abs (meas - 6.0) < 0.3, fmt ("Linear phase: +6 dB bell measured %.2f dB", meas));
    }
}

// ------------------------------------------------------------------ character + effects

void testCharacterAndEffects()
{
    std::printf ("Character + effects\n");
    {
        double prev = -300;
        bool rising = true;
        for (float in : { -6.0f, 6.0f, 18.0f })
        {
            TapeModule m;
            m.setParam ("input_db", in);
            m.setParam ("wow", 0.0f);
            m.setParam ("flutter", 0.0f);
            m.prepare (kFs, kBlock, 2);
            Stereo s = sine (1000, -12, 48000);
            run (m, s);
            const double thd = harm (s, 1000).thdDb;
            rising = rising && thd > prev;
            prev = thd;
        }
        check (rising, fmt ("Tape: distortion rises with input level (last THD %.1f dB)", prev));
    }
    {
        TapeModule m;
        m.setParam ("wow", 0.0001f);
        m.setParam ("flutter", 0.0f);
        m.prepare (kFs, kBlock, 2);
        Stereo s (8192);
        s.l[100] = s.r[100] = 0.01f;
        run (m, s);
        check (std::abs (peakIndex (s.l) - 100 - m.latencySamples()) <= 1, fmt ("Tape: reported latency %.0f matches impulse (%.0f)", m.latencySamples(), peakIndex (s.l) - 100));
    }
    {
        auto thdAt = [] (double f)
        {
            TransformerModule m;
            m.setParam ("drive_db", 12.0f);
            m.setParam ("core_sat", 0.8f);
            m.prepare (kFs, kBlock, 2);
            Stereo s = sine (f, -6, 96000);
            run (m, s);
            return harm (s, f).thdDb;
        };
        const double lo = thdAt (40.0), hi = thdAt (1000.0);
        check (lo > hi + 10.0, fmt ("Transformer: bass saturates first (THD 40 Hz %.1f dB vs 1 kHz %.1f dB)", lo, hi));
    }
    {
        ConsoleModule m;
        m.setParam ("crosstalk_db", -40.0f);
        m.prepare (kFs, kBlock, 2);
        Stereo s = sine (5000, -12, 48000);
        std::fill (s.r.begin(), s.r.end(), 0.0f);
        run (m, s);
        const double leak = toneDb (s.r, 5000) + 12.0;
        check (leak > -45.0 && leak < -35.0, fmt ("Console: crosstalk into the silent channel at 5 kHz = %.1f dB", leak));
    }
    {
        DelayModule m;
        m.setParam ("time_ms", 375.0f);
        m.setParam ("feedback_pct", 50.0f);
        m.setParam ("wow", 0.0f);
        m.setParam ("drive", 0.0f);
        m.setParam ("lp_hz", 20000.0f);
        m.setParam ("hp_hz", 20.0f);
        m.prepare (kFs, kBlock, 2);
        Stereo s (48000);
        s.l[10] = s.r[10] = 0.5f;
        run (m, s);
        const int first = peakIndex (std::vector<float> (s.l.begin(), s.l.begin() + 30000));
        check (first - 10 == 18000, fmt ("Delay: echo at %.0f samples (375 ms = 18000)", first - 10));
        const double e1 = std::abs (s.l[(size_t) first]);
        double e2 = 0.0;
        for (int k = -2; k <= 2; ++k) e2 = std::max (e2, (double) std::abs (s.l[(size_t) (first + 18000 + k)]));
        check (e2 < e1 && e2 > e1 * 0.3, fmt ("Delay: second echo lower (%.3f -> %.3f)", e1, e2));
    }
    {
        ReverbModule m;
        m.setParam ("decay_s", 1.0f);
        m.setParam ("predelay_ms", 0.0f);
        m.setParam ("early", 0.0f);
        m.setParam ("mod_depth", 0.0f);
        m.prepare (kFs, kBlock, 2);
        Stereo s ((int) (kFs * 2));
        s.l[0] = s.r[0] = 1.0f;
        run (m, s);
        auto energy = [&] (double t0, double t1)
        {
            double e = 0;
            for (int i = (int) (t0 * kFs); i < (int) (t1 * kFs); ++i) e += s.l[(size_t) i] * s.l[(size_t) i];
            return 10.0 * std::log10 (e + 1e-30);
        };
        const double slope = (energy (0.6, 0.7) - energy (0.2, 0.3)) / 0.4; // dB per second
        const double t60 = -60.0 / slope;
        check (t60 > 0.7 && t60 < 1.4, fmt ("Reverb: measured RT60 %.2f s for Decay 1.0 s", t60));
    }
    {
        bool ok = true;
        for (const char* id : { "mod.chorus", "mod.flanger", "mod.phaser" })
        {
            auto m = createModule (id);
            m->prepare (kFs, kBlock, 2);
            Stereo s = sine (440, -12, 48000), ref = s;
            run (*m, s);
            double diff = 0;
            bool finite = true;
            for (int i = 0; i < s.size(); ++i)
            {
                diff = std::max (diff, (double) std::abs (s.l[(size_t) i] - ref.l[(size_t) i]));
                finite = finite && std::isfinite (s.l[(size_t) i]);
            }
            ok = ok && finite && diff > 0.01;
        }
        check (ok, "Chorus / Flanger / Phaser: audible, finite processing");
    }
    {
        // Pitch: 1 kHz in -> the dominant tone moves by the chosen interval
        auto dominant = [] (const std::vector<float>& x)
        {
            auto spec = computeSpectrum (x.data() + x.size() / 2, (int) x.size() / 2, 16384, kFs, WindowType::blackmanHarris4);
            int best = 1;
            for (int k = 1; k < (int) spec.amplitude.size(); ++k) if (spec.amplitude[(size_t) k] > spec.amplitude[(size_t) best]) best = k;
            return best * spec.binHz();
        };
        auto shifted = [&] (float semis, float cents)
        {
            auto m = createModule ("pitch.shift");
            m->prepare (kFs, kBlock, 2);
            m->setParam ("semitones", semis);
            m->setParam ("cents", cents);
            Stereo s = sine (1000, -12, 96000);
            run (*m, s);
            return dominant (s.l);
        };
        const double up = shifted (12, 0), down = shifted (-12, 0), fifth = shifted (7, 0), zero = shifted (0, 0);
        check (std::abs (up - 2000) < 8 && std::abs (down - 500) < 8,
               fmt ("Pitch: +12 st -> %.0f Hz, -12 st -> %.0f Hz (expected 2000 / 500)", up, down));
        check (std::abs (fifth - 1000 * std::pow (2.0, 7.0 / 12.0)) < 8 && std::abs (zero - 1000) < 4,
               fmt ("Pitch: +7 st -> %.0f Hz (expected 1498), 0 st -> %.0f Hz", fifth, zero));
        auto m = createModule ("pitch.shift");
        m->prepare (kFs, kBlock, 2);
        Stereo s = sine (1000, -12, 48000), ref = s;
        run (*m, s);
        double e = 0, r = 0;
        for (int i = 24000; i < 48000; ++i) { e += s.l[(size_t) i] * (double) s.l[(size_t) i]; r += ref.l[(size_t) i] * (double) ref.l[(size_t) i]; }
        check (std::abs (10.0 * std::log10 (e / r)) < 1.0, fmt ("Pitch at 0 st keeps the level (%.2f dB)", 10.0 * std::log10 (e / r)));
    }
    {
        auto crest = [] (float attack)
        {
            TransientModule m;
            m.setParam ("attack_pct", attack);
            m.prepare (kFs, kBlock, 2);
            Stereo s (48000);
            for (int i = 0; i < s.size(); ++i)
            {
                const double t = (i % 12000) / kFs;
                s.l[(size_t) i] = s.r[(size_t) i] = (float) (0.3 * std::exp (-t * 12.0) * std::sin (kTwoPi * 180.0 * t));
            }
            run (m, s);
            double pk = 0, ms = 0;
            for (float v : s.l) { pk = std::max (pk, (double) std::abs (v)); ms += v * v; }
            return gainToDb (pk) - gainToDb (std::sqrt (ms / s.size()));
        };
        const double flat = crest (0.0f), punchy = crest (100.0f);
        check (punchy > flat + 2.0, fmt ("Transient: +100 %% attack raises crest factor (%.1f -> %.1f dB)", flat, punchy));
    }
    {
        StereoModule m;
        m.setParam ("bass_mono_hz", 200.0f);
        m.prepare (kFs, kBlock, 2);
        Stereo s = sine (50, -12, 48000);
        for (auto& v : s.r) v = -v; // pure side signal
        for (int i = 0; i < s.size(); ++i) s.r[(size_t) i] = -s.l[(size_t) i];
        run (m, s);
        const double lvl = toneDb (s.l, 50) + 12.0;
        check (lvl < -10.0, fmt ("Stereo: bass mono removes 50 Hz side content (%.1f dB)", lvl));
    }
    {
        ExciterModule m;
        m.setParam ("freq_hz", 3000.0f);
        m.setParam ("character", 0.0f);
        m.setParam ("amount_db", -6.0f);
        m.prepare (kFs, kBlock, 2);
        Stereo s = sine (5000, -12, 48000);
        run (m, s);
        const double h2 = harm (s, 5000).levelDbc[1];
        check (h2 > -50.0, fmt ("Exciter: adds an even harmonic above the band (H2 %.1f dBc)", h2));
    }
    {
        FilterModule m;
        m.setParam ("cutoff_hz", 1000.0f);
        m.setParam ("resonance", 0.0f);
        m.setParam ("slope", 1.0f);
        m.prepare (kFs, kBlock, 2);
        const double g4k = measureGainDb (m, 4000);
        check (g4k < -40.0, fmt ("Filter: 24 dB/oct low-pass at 1 kHz -> %.1f dB at 4 kHz", g4k));
    }
}
} // namespace

int main()
{
    testGraph();
    testSaturation();
    testCompressor();
    testEq();
    testCharacterAndEffects();
    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
