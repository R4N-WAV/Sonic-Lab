// SONIC LAB core DSP tests. Plain C++ (no framework) so they run anywhere.
// Each test checks a measurable property against a stated tolerance.

#include "sonic/Analysis.h"
#include "sonic/Chain.h"
#include "sonic/DspUtils.h"
#include "sonic/SignalGenerator.h"
#include "sonic/TransferAnalyzer.h"
#include "sonic/modules/EqModule.h"
#include "sonic/modules/TubeModule.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace sonic;

namespace
{
int failures = 0, checks = 0;

void check (bool ok, const std::string& what)
{
    ++checks;
    std::printf ("  [%s] %s\n", ok ? " OK " : "FAIL", what.c_str());
    if (! ok)
        ++failures;
}

std::string fmt (const char* f, double a, double b = 0, double c = 0)
{
    char buf[256];
    std::snprintf (buf, sizeof buf, f, a, b, c);
    return buf;
}

constexpr double kFs = 48000.0;
constexpr int kBlock = 256;

// Renders a generator into stereo buffers.
void renderGen (SignalGenerator& g, std::vector<float>& l, std::vector<float>& r, int n)
{
    l.assign ((size_t) n, 0.0f);
    r.assign ((size_t) n, 0.0f);
    for (int i = 0; i < n; i += kBlock)
        g.render (l.data() + i, r.data() + i, std::min (kBlock, n - i));
}

// Runs a module over stereo buffers in blocks.
void runModule (Module& m, std::vector<float>& l, std::vector<float>& r)
{
    const int n = (int) l.size();
    for (int i = 0; i < n; i += kBlock)
    {
        float* ch[2] = { l.data() + i, r.data() + i };
        m.process (ch, 2, std::min (kBlock, n - i));
    }
}

HarmonicResult measureTube (TubeModule& t, double freq, double levelDb)
{
    SignalGenerator g;
    g.prepare (kFs);
    g.frequencyHz = (float) freq;
    g.levelDb = (float) levelDb;
    std::vector<float> l, r;
    renderGen (g, l, r, 48000);
    t.prepare (kFs, kBlock, 2);
    runModule (t, l, r);
    auto spec = computeSpectrum (l.data(), (int) l.size(), 16384, kFs, WindowType::blackmanHarris4);
    return analyseHarmonics (spec, freq, 10, 20000.0);
}

void testGeneratorAndAnalyser()
{
    std::printf ("Generator + analyser calibration\n");
    SignalGenerator g;
    g.prepare (kFs);
    g.frequencyHz = 997.0f;
    g.levelDb = -6.0f;
    std::vector<float> l, r;
    renderGen (g, l, r, 32768);
    auto spec = computeSpectrum (l.data(), (int) l.size(), 16384, kFs, WindowType::blackmanHarris4);
    auto h = analyseHarmonics (spec, 997.0);
    check (h.valid, "harmonic analysis valid for pure sine");
    check (std::fabs (h.levelDbfs[0] + 6.0) < 0.05, fmt ("sine level reads -6.00 dBFS (got %.3f)", h.levelDbfs[0]));
    check (h.thdDb < -120.0, fmt ("pure sine THD below -120 dB (got %.1f dB)", h.thdDb));

    // Bin-centred peak amplitude reading
    const double binHz = kFs / 16384.0;
    g.reset();
    g.frequencyHz = (float) (binHz * 300.0);
    g.levelDb = 0.0f;
    renderGen (g, l, r, 16384);
    spec = computeSpectrum (l.data(), (int) l.size(), 16384, kFs, WindowType::blackmanHarris4);
    check (std::fabs (gainToDb (spec.amplitude[300])) < 0.01, fmt ("bin-centred 0 dBFS sine peaks at 0 dB (got %.4f)", gainToDb (spec.amplitude[300])));

    // Square wave: odd harmonics at 1/n
    g.reset();
    g.waveform = (int) SignalGenerator::Waveform::square;
    g.frequencyHz = 500.0f;
    g.levelDb = -6.0f;
    renderGen (g, l, r, 32768);
    spec = computeSpectrum (l.data(), (int) l.size(), 16384, kFs, WindowType::blackmanHarris4);
    h = analyseHarmonics (spec, 500.0);
    check (std::fabs (h.levelDbc[2] - gainToDb (1.0 / 3.0)) < 0.2, fmt ("band-limited square H3 = -9.54 dBc (got %.2f)", h.levelDbc[2]));
    check (h.levelDbc[1] < -60.0, fmt ("square has no even harmonics (H2 %.1f dBc)", h.levelDbc[1]));

    // Pink noise RMS calibration
    g.reset();
    g.waveform = (int) SignalGenerator::Waveform::pinkNoise;
    g.levelDb = 0.0f;
    renderGen (g, l, r, 480000);
    double s = 0;
    for (float v : l) s += v * v;
    const double rmsDb = gainToDb (std::sqrt (s / l.size()));
    check (std::fabs (rmsDb) < 1.0, fmt ("pink noise RMS ~0 dBFS at 0 dB setting (got %.2f)", rmsDb));
}

void testEq()
{
    std::printf ("EQ: measured response == displayed response\n");
    EqModule eq;
    eq.setParam ("bell_freq_hz", 1000.0f);
    eq.setParam ("bell_gain_db", 6.0f);
    eq.setParam ("bell_q", 1.0f);
    eq.setParam ("hs_gain_db", -4.0f);
    eq.setParam ("hp_enabled", 1.0f);
    eq.setParam ("hp_freq_hz", 80.0f);

    const auto design = eq.currentDesign (kFs);
    for (double f : { 50.0, 120.0, 1000.0, 3000.0, 12000.0 })
    {
        SignalGenerator g;
        g.prepare (kFs);
        g.frequencyHz = (float) f;
        g.levelDb = -20.0f;
        std::vector<float> l, r;
        renderGen (g, l, r, 48000);
        eq.prepare (kFs, kBlock, 2);
        runModule (eq, l, r);
        auto spec = computeSpectrum (l.data(), (int) l.size(), 16384, kFs, WindowType::blackmanHarris4);
        const double measured = gainToDb (spec.toneAmplitude (f, 5)) + 20.0;
        const double predicted = gainToDb (EqModule::magnitudeAt (design, kFs, f));
        check (std::fabs (measured - predicted) < 0.05,
               fmt ("%.0f Hz: measured %.3f dB vs predicted %.3f dB", f, measured, predicted));
    }
    check (std::fabs (gainToDb (EqModule::magnitudeAt (design, kFs, 1000.0)) - 6.0) < 0.1, "bell gives +6 dB at centre");
}

void testTube()
{
    std::printf ("Tube stage\n");
    {
        TubeModule t; // defaults: triode, bias 0, drive 12, gain comp unity
        t.setParam ("sag", 0.0f);
        t.setParam ("drive_db", 0.0f);
        auto h = measureTube (t, 1000.0, -60.0);
        check (std::fabs (h.levelDbfs[0] + 60.0) < 0.3, fmt ("unity small-signal gain (-60 dBFS in -> %.2f out)", h.levelDbfs[0]));
    }
    {
        TubeModule t;
        t.setParam ("sag", 0.0f);
        t.setParam ("drive_db", 18.0f);
        auto h = measureTube (t, 1000.0, -12.0);
        check (h.levelDbc[1] > -40.0, fmt ("triode @ +18 dB drive: strong H2 (%.1f dBc)", h.levelDbc[1]));
        check (h.levelDbc[1] > h.levelDbc[2], fmt ("triode: H2 (%.1f) > H3 (%.1f)", h.levelDbc[1], h.levelDbc[2]));
    }
    {
        TubeModule t;
        t.setParam ("sag", 0.0f);
        t.setParam ("character", 1.0f);
        t.setParam ("drive_db", 18.0f);
        auto h = measureTube (t, 1000.0, -12.0);
        check (h.levelDbc[2] > h.levelDbc[1] + 20.0, fmt ("pentode, bias 0: odd dominant (H3 %.1f vs H2 %.1f dBc)", h.levelDbc[2], h.levelDbc[1]));
        t.setParam ("bias", 0.6f);
        auto hb = measureTube (t, 1000.0, -12.0);
        check (hb.levelDbc[1] > h.levelDbc[1] + 20.0, fmt ("pentode: bias raises H2 (%.1f -> %.1f dBc)", h.levelDbc[1], hb.levelDbc[1]));
    }
    {
        // More drive => more THD (monotonic over this range)
        double prev = -1000.0;
        bool mono = true;
        std::string seq;
        for (float d : { 0.0f, 6.0f, 12.0f, 18.0f, 24.0f })
        {
            TubeModule t;
            t.setParam ("sag", 0.0f);
            t.setParam ("drive_db", d);
            auto h = measureTube (t, 1000.0, -18.0);
            mono = mono && h.thdDb > prev;
            prev = h.thdDb;
            seq += fmt ("%.1f ", h.thdDb);
        }
        check (mono, "THD rises with drive: " + seq + "dB");
    }
    {
        // Aliasing: 7 kHz at heavy drive; measure energy at non-harmonic bins
        auto aliasDb = [] (int osIndex)
        {
            TubeModule t;
            t.setParam ("sag", 0.0f);
            t.setParam ("character", 1.0f);
            t.setParam ("drive_db", 30.0f);
            t.setParam ("oversampling", (float) osIndex);
            SignalGenerator g;
            g.prepare (kFs);
            g.frequencyHz = 7000.0f;
            g.levelDb = -6.0f;
            std::vector<float> l, r;
            renderGen (g, l, r, 48000);
            t.prepare (kFs, kBlock, 2);
            runModule (t, l, r);
            auto spec = computeSpectrum (l.data(), (int) l.size(), 16384, kFs, WindowType::blackmanHarris4);
            // Energy in 100 Hz..6.5 kHz (below fundamental: only aliases can land here)
            double sum = 0;
            for (int k = (int) (100 / spec.binHz()); k < (int) (6500 / spec.binHz()); ++k)
                sum += spec.power[(size_t) k];
            return gainToDb (std::sqrt (4.0 * sum / (spec.fftSize * spec.windowSumSq)));
        };
        const double a1 = aliasDb (0), a8 = aliasDb (3);
        check (a8 < a1 - 30.0, fmt ("8x oversampling cuts aliasing (1x %.1f dBFS -> 8x %.1f dBFS)", a1, a8));
    }
    {
        // Reported latency equals measured impulse delay
        TubeModule t;
        t.setParam ("drive_db", 0.0f);
        t.setParam ("sag", 0.0f);
        t.setParam ("oversampling", 2.0f);
        t.prepare (kFs, kBlock, 2);
        std::vector<float> l (2048, 0.0f), r (2048, 0.0f);
        l[100] = r[100] = 0.01f;
        runModule (t, l, r);
        int peak = 0;
        for (int i = 0; i < 2048; ++i)
            if (std::fabs (l[(size_t) i]) > std::fabs (l[(size_t) peak]))
                peak = i;
        check (peak - 100 == t.latencySamples(), fmt ("latency: reported %.0f, measured %.0f samples", t.latencySamples(), peak - 100));
    }
    {
        // Static curve used by the UI matches the processed output for a slow signal
        TubeModule t;
        t.setParam ("sag", 0.0f);
        t.setParam ("drive_db", 12.0f);
        t.setParam ("bias", 0.3f);
        const double y = t.staticCurve (0.25);
        check (std::isfinite (y) && y > 0.0 && y < 0.25, fmt ("static curve compresses positive input (f(0.25) = %.4f)", y));
    }
}

void testChain()
{
    std::printf ("Chain\n");
    Chain c;
    c.addModule (std::make_unique<TubeModule>());
    c.addModule (std::make_unique<EqModule>());
    check (c.order() == std::vector<int> ({ 0, 1 }), "initial order 0,1");
    c.moveSlot (1, 0);
    check (c.order() == std::vector<int> ({ 1, 0 }), "moveSlot reorders to 1,0");
    c.setOrder ({ 0, 0 });
    check (c.order() == std::vector<int> ({ 1, 0 }), "invalid order rejected");
    check (c.latencySamples() == 64, "chain latency = tube 4x latency (64)");
    c.setEnabled (0, false);
    check (c.latencySamples() == 0, "disabled module contributes no latency");
}

void testTransfer()
{
    std::printf ("Transfer analyser (black-box device measurement)\n");
    // "Device" = EQ followed by a 37-sample delay, excited with pink noise.
    EqModule eq;
    eq.setParam ("bell_freq_hz", 1000.0f);
    eq.setParam ("bell_gain_db", 6.0f);
    eq.setParam ("hs_gain_db", -4.0f);
    const auto design = eq.currentDesign (kFs);

    SignalGenerator g;
    g.prepare (kFs);
    g.waveform = (int) SignalGenerator::Waveform::pinkNoise;
    g.levelDb = -20.0f;
    std::vector<float> x, r;
    renderGen (g, x, r, 96000);
    std::vector<float> yl = x, yr = x;
    eq.prepare (kFs, kBlock, 2);
    runModule (eq, yl, yr);
    const int delay = 37;
    std::vector<float> y (x.size(), 0.0f);
    for (size_t i = delay; i < x.size(); ++i)
        y[i] = yl[i - delay];

    TransferAnalyzer ta;
    ta.prepare (kFs, 4096, 0.0);
    ta.addBlock (x.data() + 4096, y.data() + 4096, (int) x.size() - 4096);
    auto res = ta.result();
    check (res.valid, "transfer result valid");
    check (res.estimatedDelaySamples == delay, fmt ("delay estimate %.0f samples (true %.0f)", res.estimatedDelaySamples, delay));
    for (double f : { 200.0, 1000.0, 6000.0 })
    {
        const int k = (int) std::lround (f / res.binHz);
        const double pred = gainToDb (EqModule::magnitudeAt (design, kFs, k * res.binHz));
        check (std::fabs (res.magnitudeDb[(size_t) k] - pred) < 0.3,
               fmt ("%.0f Hz: estimated %.2f dB vs true %.2f dB", f, res.magnitudeDb[(size_t) k], pred));
    }
    check (res.broadband, fmt ("pink noise counts as broadband excitation (%.0f %% of bins usable)", res.usableFraction * 100.0));
    check (res.meanCoherence > 0.97, fmt ("linear device: mean coherence %.3f > 0.97", res.meanCoherence));

    // A heavily driven tube is not linear -> coherence must drop
    TubeModule t;
    t.setParam ("drive_db", 30.0f);
    t.setParam ("character", 1.0f);
    std::vector<float> tl = x, tr = x;
    g.reset();
    g.levelDb = -6.0f;
    renderGen (g, x, r, 96000);
    tl = x; tr = x;
    t.prepare (kFs, kBlock, 2);
    runModule (t, tl, tr);
    TransferAnalyzer ta2;
    ta2.prepare (kFs, 4096, 0.0);
    ta2.addBlock (x.data() + 4096, tl.data() + 4096, (int) x.size() - 4096);
    auto res2 = ta2.result();
    {
        SignalGenerator gs;
        gs.prepare (kFs);
        gs.frequencyHz = 1000.0f;
        std::vector<float> sx, sr;
        renderGen (gs, sx, sr, 48000);
        TransferAnalyzer ta3;
        ta3.prepare (kFs, 4096, 0.0);
        ta3.addBlock (sx.data(), sx.data(), (int) sx.size());
        check (! ta3.result().broadband, fmt ("single sine flagged as narrowband (%.1f %% of bins usable)", ta3.result().usableFraction * 100.0));
    }
    check (res2.meanCoherence < res.meanCoherence - 0.1, fmt ("nonlinear device: coherence drops to %.3f", res2.meanCoherence));
    check (res2.estimatedDelaySamples == t.latencySamples(), fmt ("tube latency found by cross-correlation: %.0f samples", res2.estimatedDelaySamples));
}
} // namespace

int main()
{
    testGeneratorAndAnalyser();
    testEq();
    testTube();
    testChain();
    testTransfer();
    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
