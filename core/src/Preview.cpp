#include "sonic/Preview.h"

#include "sonic/Analysis.h"
#include "sonic/Biquad.h"
#include "sonic/ModuleRegistry.h"
#include "sonic/modules/CompressorModule.h"
#include "sonic/modules/ParametricEqModule.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <memory>
#include <random>

namespace sonic::preview
{

namespace
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr int kBlock = 512;

    double toDb (double g, double floorDb = -200.0) { return g > 0.0 ? std::max (floorDb, 20.0 * std::log10 (g)) : floorDb; }

    bool cancelled (const std::atomic<bool>* c) { return c != nullptr && c->load (std::memory_order_relaxed); }

    std::unique_ptr<Module> makeCopy (const Settings& s, const std::vector<std::pair<std::string, float>>& overrides = {})
    {
        auto m = createModule (s.type);
        if (m == nullptr)
            return nullptr;
        for (int i = 0; i < m->numParams() && i < (int) s.params.size(); ++i)
            m->setParam (i, s.params[(size_t) i]);
        for (auto& [id, v] : overrides)
            if (m->paramIndex (id) >= 0)
                m->setParam (id, v);
        m->prepare (s.sampleRate, kBlock, 2);
        if (auto* eq = dynamic_cast<ParametricEqModule*> (m.get()))
            eq->prepareOffline();
        HostInfo h;
        h.bpm = s.bpm;
        h.playing = true;
        m->setHostInfo (h);
        return m;
    }

    // Plays L/R through the block like the bridge does (Mix against the latency-aligned
    // dry signal, then the block's output gain), and removes the latency so the result
    // lines up with the input.
    void run (Module& m, std::vector<float>& L, std::vector<float>& R, float mix, float outDb)
    {
        const int n = (int) L.size();
        const int lat = std::max (0, m.latencySamples());
        const std::vector<float> dL = L, dR = R;
        for (int i = 0; i < n; i += kBlock)
        {
            const int len = std::min (kBlock, n - i);
            float* ch[2] = { L.data() + i, R.data() + i };
            m.process (ch, 2, len);
        }
        const float g = (float) std::pow (10.0, outDb / 20.0);
        for (int i = 0; i < n; ++i)
        {
            const float a = i >= lat ? dL[(size_t) (i - lat)] : 0.0f;
            const float b = i >= lat ? dR[(size_t) (i - lat)] : 0.0f;
            L[(size_t) i] = (a + (L[(size_t) i] - a) * mix) * g;
            R[(size_t) i] = (b + (R[(size_t) i] - b) * mix) * g;
        }
        if (lat > 0 && lat < n)
        {
            L.erase (L.begin(), L.begin() + lat);
            R.erase (R.begin(), R.begin() + lat);
            L.resize ((size_t) n, 0.0f);
            R.resize ((size_t) n, 0.0f);
        }
    }

    std::vector<std::complex<double>> spectrumOf (const float* x, int n, int fftSize)
    {
        std::vector<std::complex<double>> d ((size_t) fftSize);
        for (int i = 0; i < fftSize && i < n; ++i)
            d[(size_t) i] = x[i];
        fft (d);
        return d;
    }

    // Power average of |H|^2 over a 1/frac-octave band around f (linear bins).
    double bandPowerDb (const std::vector<double>& pow, double binHz, double f, double frac)
    {
        const double k = std::pow (2.0, 0.5 / frac);
        const int a = std::max (1, (int) std::floor (f / k / binHz));
        const int b = std::min ((int) pow.size() - 1, (int) std::ceil (f * k / binHz));
        double s = 0.0;
        int c = 0;
        for (int i = a; i <= b; ++i) { s += pow[(size_t) i]; ++c; }
        return c > 0 ? 10.0 * std::log10 (s / c + 1e-30) : -200.0;
    }

    std::vector<float> logGrid (int n, double f0, double f1)
    {
        std::vector<float> g ((size_t) n);
        for (int i = 0; i < n; ++i)
            g[(size_t) i] = (float) (f0 * std::pow (f1 / f0, (double) i / (n - 1)));
        return g;
    }

    // ---------------------------------------------------------------- response

    std::vector<float> measureResponse (const Settings& s, const std::vector<float>& grid,
                                        const std::vector<std::pair<std::string, float>>& overrides)
    {
        auto m = makeCopy (s, overrides);
        if (m == nullptr) return {};
        const int pre = 4096, fftN = 16384;
        const float a = 0.0316f;   // -30 dBFS: small-signal behaviour of nonlinear blocks
        std::vector<float> L ((size_t) (pre + fftN), 0.0f), R;
        L[(size_t) pre] = a;
        R = L;
        run (*m, L, R, s.mix, s.outDb);
        std::vector<float> ir (L.begin() + pre, L.begin() + pre + fftN);
        for (int i = fftN * 3 / 4; i < fftN; ++i)   // gentle taper of the tail
            ir[(size_t) i] *= (float) (0.5 + 0.5 * std::cos (kPi * (i - fftN * 3 / 4) / (fftN / 4.0)));
        auto H = spectrumOf (ir.data(), fftN, fftN);
        std::vector<double> pw ((size_t) fftN / 2 + 1);
        for (size_t k = 0; k < pw.size(); ++k)
            pw[k] = std::norm (H[k]) / (double) (a * a);
        const double binHz = s.sampleRate / fftN;
        std::vector<float> out (grid.size());
        for (size_t i = 0; i < grid.size(); ++i)
            out[i] = (float) bandPowerDb (pw, binHz, grid[i], 24.0);
        return out;
    }

    // ---------------------------------------------------------------- harmonics

    void measureHarmonics (const Settings& s, Result& r)
    {
        auto m = makeCopy (s);
        if (m == nullptr) return;
        double f = 1000.0;
        if (s.type == "tone.exciter" && m->paramIndex ("freq_hz") >= 0)
            f = std::max (1000.0, m->getParam ("freq_hz") * 1.25);
        r.testHz = f;
        r.testDbfs = -12.0;
        const double amp = std::pow (10.0, r.testDbfs / 20.0);
        const int fftN = 16384, settle = (int) (0.3 * s.sampleRate);
        const int n = settle + fftN;
        std::vector<float> L ((size_t) n), R;
        for (int i = 0; i < n; ++i)
            L[(size_t) i] = (float) (amp * std::sin (2.0 * kPi * f * i / s.sampleRate));
        R = L;
        run (*m, L, R, s.mix, s.outDb);
        const auto spec = computeSpectrum (L.data(), n - 2048, fftN, s.sampleRate, WindowType::blackmanHarris4);
        const auto h = analyseHarmonics (spec, f, 10, 20000.0);
        r.harmDbc.assign (9, -150.0f);
        double even = 0.0, odd = 0.0;
        if (h.valid)
        {
            for (int k = 1; k < h.numHarmonics && k <= 9; ++k)
            {
                const double d = std::max (-150.0, h.levelDbc[(size_t) k]);
                r.harmDbc[(size_t) k - 1] = (float) d;
                const double p = std::pow (10.0, d / 10.0);
                ((k + 1) % 2 == 0 ? even : odd) += p;
            }
            r.thdPct = h.thdPercent;
        }
        r.evenPct = even + odd > 0.0 ? 100.0 * even / (even + odd) : 50.0;
    }

    // ---------------------------------------------------------------- transfer

    void measureTransfer (const Settings& s, Result& r)
    {
        auto m = makeCopy (s);
        if (m == nullptr) return;
        const double f = 100.0;
        const int period = (int) std::lround (s.sampleRate / f);
        const int n = period * 30;
        std::vector<float> in ((size_t) n), L, R;
        for (int i = 0; i < n; ++i)
            in[(size_t) i] = (float) std::sin (2.0 * kPi * i / period);
        L = in;
        R = in;
        run (*m, L, R, s.mix, s.outDb);
        const int start = n - period * 2;
        const int pts = 240;
        r.xyIn.resize (pts);
        r.xyOut.resize (pts);
        for (int k = 0; k < pts; ++k)
        {
            const int i = start + (int) ((double) k / pts * period);
            r.xyIn[(size_t) k] = in[(size_t) i];
            r.xyOut[(size_t) k] = L[(size_t) i];
        }
    }

    // ---------------------------------------------------------------- envelopes

    // Level envelope: RMS over a window of 'span' samples, one value every 'hop' samples.
    std::vector<float> envelopeDb (const std::vector<float>& x, int from, int to, int hop, int span = 0)
    {
        if (span <= 0) span = hop;
        std::vector<float> e;
        for (int i = from; i + hop <= to; i += hop)
        {
            const int a = std::max (0, i + hop / 2 - span / 2), b = std::min ((int) x.size(), a + span);
            double s = 0.0;
            for (int k = a; k < b; ++k) s += (double) x[(size_t) k] * x[(size_t) k];
            e.push_back ((float) toDb (std::sqrt (s / std::max (1, b - a)) * std::sqrt (2.0), -120.0));
        }
        return e;
    }

    void measureDynamics (const Settings& s, Result& r)
    {
        auto m = makeCopy (s);
        if (m == nullptr) return;
        auto* comp = dynamic_cast<CompressorModule*> (m.get());
        const double thr = m->paramIndex ("threshold_db") >= 0 ? m->getParam ("threshold_db") : -18.0;
        r.thresholdDb = thr;
        const double g = std::pow (10.0, s.outDb / 20.0);
        for (double in = -60.0; in <= 0.001; in += 0.5)
        {
            const double wetDb = comp != nullptr ? comp->staticCurveDb (in) : in;
            const double y = (s.mix * std::pow (10.0, wetDb / 20.0) + (1.0 - s.mix) * std::pow (10.0, in / 20.0)) * g;
            r.staticIn.push_back ((float) in);
            r.staticOut.push_back ((float) toDb (y, -120.0));
        }

        // a 1 kHz tone that steps up into compression and back down
        r.stepLowDb = std::max (-70.0, thr - 12.0);
        r.stepHighDb = std::min (0.0, thr + 18.0);
        const double fs = s.sampleRate;
        const int t0 = (int) (0.15 * fs), t1 = t0 + (int) (0.7 * fs), n = t1 + (int) (1.4 * fs);
        std::vector<float> in ((size_t) n), L, R;
        const double lo = std::pow (10.0, r.stepLowDb / 20.0), hi = std::pow (10.0, r.stepHighDb / 20.0);
        for (int i = 0; i < n; ++i)
            in[(size_t) i] = (float) ((i >= t0 && i < t1 ? hi : lo) * std::sin (2.0 * kPi * 1000.0 * i / fs));
        L = in;
        R = in;
        run (*m, L, R, s.mix, s.outDb);
        const int win = std::max (1, (int) (0.001 * fs));
        r.stepInDb = envelopeDb (in, 0, n, win);
        r.stepOutDb = envelopeDb (L, 0, n, win);
        for (size_t i = 0; i < r.stepInDb.size(); ++i)
        {
            r.stepMs.push_back ((float) (i * win * 1000.0 / fs - 150.0));
            r.stepGrDb.push_back (r.stepOutDb[i] - r.stepInDb[i]);
        }
    }

    void measureEnvelope (const Settings& s, Result& r)
    {
        auto m = makeCopy (s);
        if (m == nullptr) return;
        const double fs = s.sampleRate;
        const int hitLen = (int) (0.45 * fs), first = (int) (0.05 * fs), second = (int) (0.55 * fs), n = (int) (1.05 * fs);
        std::vector<float> in ((size_t) n, 0.0f), L, R;
        std::mt19937 rng (11);
        std::uniform_real_distribution<float> u (-1.0f, 1.0f);
        for (int start : { first, second })
            for (int i = 0; i < hitLen && start + i < n; ++i)
            {
                const double t = i / fs;
                in[(size_t) (start + i)] = (float) (0.35 * u (rng) * std::exp (-t / 0.025) + 0.45 * std::sin (2.0 * kPi * 70.0 * t) * std::exp (-t / 0.22));
            }
        L = in;
        R = in;
        run (*m, L, R, s.mix, s.outDb);
        const int win = std::max (1, (int) (0.001 * fs));
        const int from = second - (int) (0.02 * fs), to = std::min (n, second + (int) (0.43 * fs));
        const int span = (int) (0.012 * fs);   // smooths the 70 Hz body without hiding the attack
        r.envInDb = envelopeDb (in, from, to, win, span);
        r.envOutDb = envelopeDb (L, from, to, win, span);
        for (size_t i = 0; i < r.envInDb.size(); ++i)
            r.envMs.push_back ((float) (i * win * 1000.0 / fs - 20.0));
    }

    // ---------------------------------------------------------------- impulse

    double decayTime (const std::vector<float>& x, double fs)
    {
        // Schroeder backward integration, T20 fit (-5 .. -25 dB) extrapolated to 60 dB
        std::vector<double> e (x.size());
        double acc = 0.0;
        for (size_t i = x.size(); i-- > 0;)
        {
            acc += (double) x[i] * x[i];
            e[i] = acc;
        }
        if (acc <= 0.0) return 0.0;
        int i5 = -1, i25 = -1;
        for (size_t i = 0; i < e.size(); ++i)
        {
            const double d = 10.0 * std::log10 (e[i] / acc + 1e-30);
            if (i5 < 0 && d <= -5.0) i5 = (int) i;
            if (i25 < 0 && d <= -25.0) { i25 = (int) i; break; }
        }
        if (i5 < 0 || i25 <= i5) return 0.0;
        return 3.0 * (i25 - i5) / fs;
    }

    void measureImpulse (const Settings& s, Result& r)
    {
        auto m = makeCopy (s);
        if (m == nullptr) return;
        const double fs = s.sampleRate;
        const bool reverb = s.type == "space.reverb";
        double seconds = 3.0;
        if (reverb)
        {
            const double decay = m->paramIndex ("decay_s") >= 0 ? m->getParam ("decay_s") : 2.0;
            const double pre = m->paramIndex ("predelay_ms") >= 0 ? m->getParam ("predelay_ms") * 0.001 : 0.0;
            seconds = std::clamp (1.25 * decay + pre + 0.2, 1.0, 10.0);
        }
        const int n = (int) (seconds * fs);
        std::vector<float> L ((size_t) n, 0.0f), R;
        L[0] = 0.5f;
        R = L;
        run (*m, L, R, 1.0f, s.outDb);   // the wet signal only: the picture is the space itself
        const int win = std::max (1, (int) ((reverb ? 0.005 : 0.002) * fs));
        for (int i = 0; i + win <= n; i += win)
        {
            float pl = 0.0f, pr = 0.0f;
            double el = 0.0, er = 0.0;
            for (int k = 0; k < win; ++k)
            {
                pl = std::max (pl, std::abs (L[(size_t) (i + k)]));
                pr = std::max (pr, std::abs (R[(size_t) (i + k)]));
                el += (double) L[(size_t) (i + k)] * L[(size_t) (i + k)];
                er += (double) R[(size_t) (i + k)] * R[(size_t) (i + k)];
            }
            r.irMs.push_back ((float) (i * 1000.0 / fs));
            if (reverb)
            {
                r.irL.push_back ((float) toDb (std::sqrt (el / win) * 2.0 / 0.5, -120.0));
                r.irR.push_back ((float) toDb (std::sqrt (er / win) * 2.0 / 0.5, -120.0));
            }
            else
            {
                r.irL.push_back ((float) toDb (pl / 0.5, -120.0));
                r.irR.push_back ((float) toDb (pr / 0.5, -120.0));
            }
        }
        if (reverb)
        {
            std::vector<float> mono ((size_t) n);
            for (int i = 0; i < n; ++i) mono[(size_t) i] = 0.5f * (L[(size_t) i] + R[(size_t) i]);
            r.rt60 = decayTime (mono, fs);
            auto filtered = [&] (std::vector<BiquadCoeffs> cs)
            {
                std::vector<Biquad> bq (cs.size());
                for (size_t k = 0; k < cs.size(); ++k) bq[k].setCoeffs (cs[k]);
                std::vector<float> y (mono.size());
                for (size_t i = 0; i < mono.size(); ++i)
                {
                    double v = mono[i];
                    for (auto& b : bq) v = b.process (v);
                    y[i] = (float) v;
                }
                return decayTime (y, fs);
            };
            r.rtLow = filtered ({ BiquadCoeffs::lowPass (fs, 250.0, 0.7071), BiquadCoeffs::lowPass (fs, 250.0, 0.7071) });
            r.rtMid = filtered ({ BiquadCoeffs::bandPass (fs, 1000.0, 0.7) });
            r.rtHigh = filtered ({ BiquadCoeffs::highPass (fs, 4000.0, 0.7071), BiquadCoeffs::highPass (fs, 4000.0, 0.7071) });
        }
    }

    // ---------------------------------------------------------------- modulation map

    void measureModMap (const Settings& s, Result& r, const std::atomic<bool>* cancel)
    {
        auto m = makeCopy (s);
        if (m == nullptr) return;
        const double fs = s.sampleRate;
        const int hop = 4096;
        const double rate = m->paramIndex ("rate_hz") >= 0 ? std::max (0.02f, m->getParam ("rate_hz")) : 0.5;
        const double seconds = std::clamp (2.0 / rate, 1.0, 6.0);
        const int frames = std::clamp ((int) std::ceil (seconds * fs / hop), 16, 72);
        const int n = (frames + 1) * hop;
        std::vector<float> L ((size_t) n, 0.0f), R;
        for (int f = 0; f <= frames; ++f)
            L[(size_t) (f * hop)] = 0.25f;
        R = L;
        run (*m, L, R, s.mix, s.outDb);
        if (cancelled (cancel)) return;
        r.mapCols = 160;
        r.mapHz = logGrid (r.mapCols, 20.0, 20000.0);
        r.mapRows = frames;
        r.mapSeconds = frames * (double) hop / fs;
        r.mapDb.resize ((size_t) (frames * r.mapCols));
        const double binHz = fs / hop;
        for (int f = 0; f < frames; ++f)
        {
            auto H = spectrumOf (L.data() + f * hop, hop, hop);
            std::vector<double> pw ((size_t) hop / 2 + 1);
            for (size_t k = 0; k < pw.size(); ++k) pw[k] = std::norm (H[k]) / (0.25 * 0.25);
            for (int c = 0; c < r.mapCols; ++c)
                r.mapDb[(size_t) (f * r.mapCols + c)] = (float) bandPowerDb (pw, binHz, r.mapHz[(size_t) c], 12.0);
        }
    }

    // ---------------------------------------------------------------- pitch

    void measurePitch (const Settings& s, Result& r)
    {
        auto m = makeCopy (s);
        if (m == nullptr) return;
        const double fs = s.sampleRate, f0 = 220.0;
        r.pitchInHz = f0;
        const int n = (int) (1.2 * fs), fftN = 16384;
        std::vector<float> in ((size_t) n, 0.0f), L, R;
        for (int k = 1; k * f0 < 18000.0; ++k)
            for (int i = 0; i < n; ++i)
                in[(size_t) i] += (float) (0.3 / k * std::sin (2.0 * kPi * f0 * k * i / fs));
        L = in;
        R = in;
        run (*m, L, R, s.mix, s.outDb);
        const int at = n - fftN - 1024;
        auto win = [&] (const std::vector<float>& x)
        {
            std::vector<float> w ((size_t) fftN);
            for (int i = 0; i < fftN; ++i)
                w[(size_t) i] = x[(size_t) (at + i)] * (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / (fftN - 1)));
            auto H = spectrumOf (w.data(), fftN, fftN);
            std::vector<double> a ((size_t) fftN / 2 + 1);
            for (size_t k = 0; k < a.size(); ++k) a[k] = std::abs (H[k]) * 4.0 / fftN;
            return a;
        };
        const auto A = win (in), B = win (L);
        r.specHz = logGrid (360, 60.0, 6000.0);
        const double binHz = fs / fftN;
        for (size_t i = 0; i < r.specHz.size(); ++i)
        {
            const double f = r.specHz[i], k = std::pow (2.0, 1.0 / 72.0);
            const int a = std::max (1, (int) std::floor (f / k / binHz)), b = std::min ((int) A.size() - 1, (int) std::ceil (f * k / binHz));
            double pa = 0.0, pb = 0.0;
            for (int j = a; j <= b; ++j) { pa = std::max (pa, A[(size_t) j]); pb = std::max (pb, B[(size_t) j]); }
            r.specInDb.push_back ((float) toDb (pa, -120.0));
            r.specOutDb.push_back ((float) toDb (pb, -120.0));
        }
    }

    // ---------------------------------------------------------------- stereo

    void measureStereo (const Settings& s, Result& r)
    {
        auto m = makeCopy (s);
        if (m == nullptr) return;
        const double fs = s.sampleRate;
        const int n = (int) (1.0 * fs);
        std::mt19937 rng (5);
        std::normal_distribution<float> nd (0.0f, 0.18f);
        std::vector<float> L ((size_t) n), R ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const float a = nd (rng), b = nd (rng);
            L[(size_t) i] = a;
            R[(size_t) i] = 0.6f * a + 0.8f * b;   // a typical, partly wide signal (correlation 0.6)
        }
        const auto inL = L, inR = R;
        run (*m, L, R, s.mix, s.outDb);

        // cloud
        const int from = n - 4096;
        float peak = 1e-6f;
        for (int i = from; i < n; ++i)
            peak = std::max ({ peak, std::abs (L[(size_t) i] + R[(size_t) i]) * 0.5f, std::abs (L[(size_t) i] - R[(size_t) i]) * 0.5f });
        for (int i = from; i < n; i += 2)
        {
            r.scopeM.push_back ((L[(size_t) i] + R[(size_t) i]) * 0.5f / peak);
            r.scopeS.push_back ((L[(size_t) i] - R[(size_t) i]) * 0.5f / peak);
        }
        double sl = 0.0, sr = 0.0, slr = 0.0;
        for (int i = n / 4; i < n; ++i)
        {
            sl += (double) L[(size_t) i] * L[(size_t) i];
            sr += (double) R[(size_t) i] * R[(size_t) i];
            slr += (double) L[(size_t) i] * R[(size_t) i];
        }
        r.correlation = sl > 0.0 && sr > 0.0 ? slr / std::sqrt (sl * sr) : 0.0;

        // width per band: (side / mid) after, relative to before
        const int fftN = 4096;
        std::vector<double> mOut ((size_t) fftN / 2 + 1), sOut (mOut.size()), mIn (mOut.size()), sIn (mOut.size());
        std::vector<float> w ((size_t) fftN);
        auto acc = [&] (const std::vector<float>& a, const std::vector<float>& b, int at, double sign, std::vector<double>& dst)
        {
            for (int i = 0; i < fftN; ++i)
                w[(size_t) i] = 0.5f * (a[(size_t) (at + i)] + (float) sign * b[(size_t) (at + i)]) * (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / (fftN - 1)));
            auto H = spectrumOf (w.data(), fftN, fftN);
            for (size_t k = 0; k < dst.size(); ++k) dst[k] += std::norm (H[k]);
        };
        for (int at = n / 4; at + fftN <= n; at += fftN / 2)
        {
            acc (L, R, at, 1.0, mOut); acc (L, R, at, -1.0, sOut);
            acc (inL, inR, at, 1.0, mIn); acc (inL, inR, at, -1.0, sIn);
        }
        r.widthHz = logGrid (28, 30.0, 16000.0);
        const double binHz = fs / fftN;
        for (float f : r.widthHz)
        {
            const double after = bandPowerDb (sOut, binHz, f, 3.0) - bandPowerDb (mOut, binHz, f, 3.0);
            const double before = bandPowerDb (sIn, binHz, f, 3.0) - bandPowerDb (mIn, binHz, f, 3.0);
            r.widthPct.push_back ((float) std::clamp (100.0 * std::pow (10.0, (after - before) / 20.0), 0.0, 400.0));
        }
    }
}

unsigned kindsFor (const std::string& t)
{
    if (t == "tube.stage" || t == "character.tape" || t == "character.transformer" || t == "character.console"
        || t == "character.saturation")
        return response | harmonics | transfer;
    if (t == "utility.gain") return transfer | response;
    if (t == "dynamics.compressor") return dynamics;
    if (t == "dynamics.transient") return envelope;
    if (t == "tone.filter") return response;
    if (t == "tone.exciter") return response | harmonics;
    if (t == "space.delay" || t == "space.reverb") return impulse;
    if (t == "mod.chorus" || t == "mod.flanger" || t == "mod.phaser") return modmap;
    if (t == "pitch.shift") return pitch;
    if (t == "utility.stereo") return stereo;
    return 0;
}

Result render (const Settings& s, unsigned kinds, const std::atomic<bool>* cancel)
{
    Result r;
    if (createModule (s.type) == nullptr)
        return r;

    if ((kinds & response) && ! cancelled (cancel))
    {
        r.respHz = logGrid (240, 20.0, 20000.0);
        if (s.type == "tone.filter")
        {
            auto copy = makeCopy (s);
            const double cut = copy->getParam ("cutoff_hz");
            const double depth = copy->getParam ("lfo_depth_oct");
            const bool moving = copy->getParam ("lfo_rate_hz") > 0.0f && depth > 0.0;
            r.respDb = measureResponse (s, r.respHz, { { "lfo_rate_hz", 0.0f }, { "env_oct", 0.0f } });
            if (moving)
            {
                r.respLoDb = measureResponse (s, r.respHz, { { "lfo_rate_hz", 0.0f }, { "env_oct", 0.0f },
                                                             { "cutoff_hz", (float) std::clamp (cut * std::pow (2.0, -depth), 20.0, 20000.0) } });
                r.respHiDb = measureResponse (s, r.respHz, { { "lfo_rate_hz", 0.0f }, { "env_oct", 0.0f },
                                                             { "cutoff_hz", (float) std::clamp (cut * std::pow (2.0, depth), 20.0, 20000.0) } });
            }
        }
        else
            r.respDb = measureResponse (s, r.respHz, {});
        r.kinds |= response;
    }
    if ((kinds & harmonics) && ! cancelled (cancel)) { measureHarmonics (s, r); r.kinds |= harmonics; }
    if ((kinds & transfer) && ! cancelled (cancel)) { measureTransfer (s, r); r.kinds |= transfer; }
    if ((kinds & dynamics) && ! cancelled (cancel)) { measureDynamics (s, r); r.kinds |= dynamics; }
    if ((kinds & envelope) && ! cancelled (cancel)) { measureEnvelope (s, r); r.kinds |= envelope; }
    if ((kinds & impulse) && ! cancelled (cancel)) { measureImpulse (s, r); r.kinds |= impulse; }
    if ((kinds & modmap) && ! cancelled (cancel)) { measureModMap (s, r, cancel); r.kinds |= modmap; }
    if ((kinds & pitch) && ! cancelled (cancel)) { measurePitch (s, r); r.kinds |= pitch; }
    if ((kinds & stereo) && ! cancelled (cancel)) { measureStereo (s, r); r.kinds |= stereo; }
    return r;
}

} // namespace sonic::preview
