#include "sonic/modules/ParametricEqModule.h"
#include "sonic/Analysis.h"

#include <chrono>
#include <cmath>

namespace sonic
{

namespace
{
    enum G { gPhase = 0, gLinQ, gOut };
    constexpr int kOrders[] = { 1, 2, 3, 4, 6, 8, 12, 16 };
    constexpr int kLinearLens[] = { 2048, 4096, 8192 };
    constexpr int kPartition = 256;
    constexpr int kSub = 32;
    const char* kSlopeNames[] = { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct" };

    bool affects (int place, int view)
    {
        // view: 0 L, 1 R, 2 M, 3 S
        switch (place)
        {
            case 0: return true;          // stereo
            case 1: return view == 0 || view == 2 || view == 3;
            case 2: return view == 1 || view == 2 || view == 3;
            case 3: return view == 2 || view == 0 || view == 1;
            case 4: return view == 3;
            default: return false;
        }
    }
}

const char* ParametricEqModule::slopeName (int i) { return kSlopeNames[std::clamp (i, 0, 7)]; }

ParametricEqModule::ParametricEqModule()
{
    specs.push_back (ParamSpec::choice ("phase_mode", "Phase mode", { "Zero latency", "Natural", "Linear" }, 0,
        "Zero latency: classic IIR. Natural: IIR at 2x rate, no high-frequency cramping (64 samples latency). "
        "Linear: no phase shift at all (FIR), adds latency.", false, "Processing"));
    specs.push_back (ParamSpec::choice ("linear_quality", "Linear quality", { "Low (2048)", "Medium (4096)", "High (8192)" }, 1,
        "FIR length in Linear mode. Longer = more accurate low end, more latency.", false, "Processing"));
    specs.push_back (ParamSpec::num ("output_db", "Output", "dB", -24, 24, 0, 0, "Output gain.", true, "Processing"));

    for (int b = 0; b < kMaxBands; ++b)
    {
        const std::string p = "b" + std::to_string (b + 1) + "_";
        const std::string g = "Band " + std::to_string (b + 1);
        const float f = (float) (30.0 * std::pow (16000.0 / 30.0, b / (double) (kMaxBands - 1)));
        specs.push_back (ParamSpec::toggle (p + "on", g + " on", false, "Band active.", false, g));
        specs.push_back (ParamSpec::choice (p + "shape", g + " shape",
            { "Bell", "Low shelf", "High shelf", "Low cut", "High cut", "Notch", "Band pass", "Tilt" }, 0, "Filter shape.", false, g));
        specs.push_back (ParamSpec::num (p + "freq_hz", g + " freq", "Hz", 10, 22000, f, 1000, "Centre / corner frequency.", false, g));
        specs.push_back (ParamSpec::num (p + "gain_db", g + " gain", "dB", -30, 30, 0, 0, "Gain (bell, shelves, tilt).", false, g));
        specs.push_back (ParamSpec::num (p + "q", g + " Q", "", 0.025f, 40, 1, 1, "Bandwidth / resonance.", false, g));
        specs.push_back (ParamSpec::choice (p + "slope", g + " slope",
            { kSlopeNames[0], kSlopeNames[1], kSlopeNames[2], kSlopeNames[3], kSlopeNames[4], kSlopeNames[5], kSlopeNames[6], kSlopeNames[7] }, 1,
            "Steepness of cut filters (Butterworth).", false, g));
        specs.push_back (ParamSpec::choice (p + "place", g + " placement", { "Stereo", "Left", "Right", "Mid", "Side" }, 0,
            "Which part of the stereo signal the band affects.", false, g));
        specs.push_back (ParamSpec::toggle (p + "dyn", g + " dynamic", false,
            "Gain changes with the level in this band (dynamic EQ).", false, g));
        specs.push_back (ParamSpec::num (p + "dyn_thr_db", g + " threshold", "dB", -80, 0, -30, 0,
            "Level in the band where the dynamic gain starts to move.", false, g));
        specs.push_back (ParamSpec::num (p + "dyn_range_db", g + " range", "dB", -30, 30, -6, 0,
            "Extra gain applied when the band is 6 dB or more above the threshold (negative = cuts when loud).", false, g));
        specs.push_back (ParamSpec::num (p + "dyn_att_ms", g + " attack", "ms", 0.1f, 500, 10, 10, "Dynamic attack.", false, g));
        specs.push_back (ParamSpec::num (p + "dyn_rel_ms", g + " release", "ms", 5, 3000, 150, 150, "Dynamic release.", false, g));
    }
    finaliseParams();
    for (auto& d : dynGain)
        d.store (0.0f);
}

ParametricEqModule::~ParametricEqModule()
{
    quit = true;
    if (worker.joinable())
        worker.join();
}

std::string ParametricEqModule::topology() const
{
    static const char* modes[] = { "Zero latency (bilinear RBJ biquads at host rate)",
                                   "Natural (same biquads at 2x rate inside a linear-phase FIR oversampler, 64 samples latency)",
                                   "Linear phase (FIR from the static magnitude, Blackman-Harris windowed, uniformly partitioned FFT convolution, P = 256)" };
    std::string s = "Up to 24 serial bands. Phase mode: ";
    s += modes[(int) getParam (gPhase)];
    s += ". Shapes: RBJ peaking (bell), RBJ shelves parameterised by Q, Butterworth cascades for cuts (6..96 dB/oct; "
         "resonance scales the last section's Q by Q/0.707), RBJ notch, constant-0-dB band-pass, tilt = complementary "
         "shelves at +-gain/2. Placement per band: Stereo, Left, Right, Mid, Side (M = (L+R)/2, S = (L-R)/2). "
         "Dynamic bands: detector = band-pass (bell/notch/band-pass) or 2nd-order HP/LP (shelves/cuts) of the EQ input, peak envelope "
         "with attack/release, amount = clamp((env_dB - threshold)/6, 0, 1), gain = static + range x amount, coefficients updated every 32 samples.";
    return s;
}

int ParametricEqModule::firstFreeBand() const
{
    for (int b = 0; b < kMaxBands; ++b)
        if (! bandOn (b))
            return b;
    return -1;
}

ParametricEqModule::Design ParametricEqModule::designBand (int b, double rate, double gainDb, bool useOverride) const
{
    Design d;
    const auto shape = (Shape) (int) getParam (bandParam (b, fShape));
    const double f = std::clamp ((double) getParam (bandParam (b, fFreq)), 5.0, rate * 0.49);
    const double g = useOverride ? gainDb : getParam (bandParam (b, fGain));
    const double q = getParam (bandParam (b, fQ));
    const int order = kOrders[(int) getParam (bandParam (b, fSlope))];

    switch (shape)
    {
        case Shape::bell:      d.s[d.n++] = BiquadCoeffs::peaking (rate, f, q, g); break;
        case Shape::lowShelf:  d.s[d.n++] = BiquadCoeffs::lowShelfQ (rate, f, q, g); break;
        case Shape::highShelf: d.s[d.n++] = BiquadCoeffs::highShelfQ (rate, f, q, g); break;
        case Shape::notch:     d.s[d.n++] = BiquadCoeffs::notch (rate, f, q); break;
        case Shape::bandPass:  d.s[d.n++] = BiquadCoeffs::bandPass (rate, f, q); break;
        case Shape::tilt:
            d.s[d.n++] = BiquadCoeffs::lowShelf (rate, f, 0.5, -g * 0.5);
            d.s[d.n++] = BiquadCoeffs::highShelf (rate, f, 0.5, g * 0.5);
            break;
        case Shape::lowCut:
        case Shape::highCut:
        {
            const bool hp = shape == Shape::lowCut;
            const auto qs = butterworthQs (order);
            const double res = std::clamp (q / 0.70710678, 0.1, 20.0);
            for (size_t k = 0; k < qs.size() && d.n < kMaxSections; ++k)
            {
                const double qq = k + 1 == qs.size() ? qs[k] * res : qs[k];
                d.s[d.n++] = hp ? BiquadCoeffs::highPass (rate, f, qq) : BiquadCoeffs::lowPass (rate, f, qq);
            }
            if (order % 2 == 1 && d.n < kMaxSections)
                d.s[d.n++] = hp ? BiquadCoeffs::highPass1 (rate, f) : BiquadCoeffs::lowPass1 (rate, f);
            break;
        }
    }
    return d;
}

double ParametricEqModule::bandResponseDb (int b, double freq) const
{
    const double rate = (Phase) (int) getParam (gPhase) == Phase::natural ? fs * 2.0 : fs;
    const auto d = designBand (b, rate, 0.0, false);
    double m = 1.0;
    for (int i = 0; i < d.n; ++i)
        m *= d.s[i].magnitudeAt (rate, freq);
    return gainToDb (m, -200.0);
}

double ParametricEqModule::responseDb (double freq, int view) const
{
    double db = 0.0;
    for (int b = 0; b < kMaxBands; ++b)
    {
        if (! bandOn (b))
            continue;
        const int place = (int) getParam (bandParam (b, fPlace));
        if (view >= 0 && ! affects (place, view))
            continue;
        db += bandResponseDb (b, freq);
    }
    return db + getParam (gOut);
}

void ParametricEqModule::prepare (double sampleRate, int maxBlockSize, int)
{
    fs = sampleRate;
    workerFs = fs;
    maxBlock = std::max (1, maxBlockSize);
    for (auto& o : os)
    {
        o.prepare (maxBlock);
        o.setFactor (2);
    }
    bufL.assign ((size_t) maxBlock * 2, 0.0);
    bufR.assign ((size_t) maxBlock * 2, 0.0);
    tmpL.assign ((size_t) maxBlock, 0.0f);
    tmpR.assign ((size_t) maxBlock, 0.0f);
    for (auto& c : conv)
        c.prepare (kPartition, kLinearLens[2]);
    designedVersion = ~0u;
    reset();

    if (! worker.joinable())
        worker = std::thread ([this] { workerLoop(); });
}

void ParametricEqModule::reset()
{
    for (auto& b : bands)
    {
        for (auto& path : b.f)
            for (auto& s : path)
                s.reset();
        for (auto& d : b.det)
            d.reset();
        b.env = { 0.0, 0.0 };
        b.amount = 0.0;
        b.lastVersion = ~0u;
    }
    for (auto& o : os)
        o.reset();
    for (auto& c : conv)
        c.reset();
}

int ParametricEqModule::latencySamples() const
{
    switch ((Phase) (int) getParam (gPhase))
    {
        case Phase::natural: return Oversampler::kLatency;
        case Phase::linear:  return kPartition + kLinearLens[(int) getParam (gLinQ)] / 2;
        default:             return 0;
    }
}

// ---------------------------------------------------------------- IIR path

void ParametricEqModule::processIir (double* L, double* R, int n, double rate)
{
    double preM[kSub], preS[kSub], preL[kSub], preR[kSub];
    const unsigned ver = paramVersion();

    for (int start = 0; start < n; start += kSub)
    {
        const int len = std::min (kSub, n - start);
        double* l = L + start;
        double* r = R + start;
        for (int i = 0; i < len; ++i)
        {
            preL[i] = l[i];
            preR[i] = r[i];
            preM[i] = 0.5 * (l[i] + r[i]);
            preS[i] = 0.5 * (l[i] - r[i]);
        }

        for (int bi = 0; bi < kMaxBands; ++bi)
        {
            if (! bandOn (bi))
            {
                dynGain[(size_t) bi].store (0.0f, std::memory_order_relaxed);
                continue;
            }
            Band& b = bands[(size_t) bi];
            const auto shape = (Shape) (int) getParam (bandParam (bi, fShape));
            const int place = (int) getParam (bandParam (bi, fPlace));
            const bool hasGain = shape == Shape::bell || shape == Shape::lowShelf || shape == Shape::highShelf || shape == Shape::tilt;
            const bool dyn = hasGain && getParam (bandParam (bi, fDyn)) > 0.5f;
            const double staticGain = getParam (bandParam (bi, fGain));
            double gain = staticGain;

            if (dyn)
            {
                // detector on the EQ input of this band's path(s)
                const double f = std::clamp ((double) getParam (bandParam (bi, fFreq)), 10.0, rate * 0.45);
                const double q = std::max (0.3, (double) getParam (bandParam (bi, fQ)));
                const auto dc = shape == Shape::lowShelf ? BiquadCoeffs::lowPass (rate, f, 0.7071)
                              : shape == Shape::highShelf ? BiquadCoeffs::highPass (rate, f, 0.7071)
                              : shape == Shape::tilt ? BiquadCoeffs::highPass (rate, f, 0.7071)
                              : BiquadCoeffs::bandPass (rate, f, q);
                const double att = std::exp (-1.0 / (getParam (bandParam (bi, fAtt)) * 0.001 * rate));
                const double rel = std::exp (-1.0 / (getParam (bandParam (bi, fRel)) * 0.001 * rate));
                const double* src0 = place >= 3 ? (place == 3 ? preM : preS) : (place == 2 ? preR : preL);
                const double* src1 = place == 0 ? preR : nullptr;
                b.det[0].setCoeffs (dc);
                b.det[1].setCoeffs (dc);
                for (int i = 0; i < len; ++i)
                {
                    double a = std::abs (b.det[0].process (src0[i]));
                    if (src1 != nullptr)
                        a = std::max (a, std::abs (b.det[1].process (src1[i])));
                    b.env[0] = a + (a > b.env[0] ? att : rel) * (b.env[0] - a);
                }
                b.env[0] = flushDenormal (b.env[0]);
                const double thr = getParam (bandParam (bi, fThr));
                b.amount = std::clamp ((gainToDb (b.env[0], -200.0) - thr) / 6.0, 0.0, 1.0);
                gain = staticGain + getParam (bandParam (bi, fRange)) * b.amount;
            }
            dynGain[(size_t) bi].store ((float) gain, std::memory_order_relaxed);

            if (b.lastVersion != ver || std::abs (gain - b.lastGain) > 1e-4)
            {
                const auto d = designBand (bi, rate, gain, true);
                for (auto& path : b.f)
                    for (int k = 0; k < d.n; ++k)
                        path[(size_t) k].setCoeffs (d.s[k]);
                if (d.n != b.nSections)
                    for (auto& path : b.f)
                        for (int k = d.n; k < kMaxSections; ++k)
                            path[(size_t) k].reset();
                b.nSections = d.n;
                b.lastVersion = ver;
                b.lastGain = gain;
            }

            auto run = [&b] (int path, double* x, int count)
            {
                auto& secs = b.f[(size_t) path];
                for (int i = 0; i < count; ++i)
                {
                    double v = x[i];
                    for (int k = 0; k < b.nSections; ++k)
                        v = secs[(size_t) k].process (v);
                    x[i] = v;
                }
            };

            switch (place)
            {
                case 0: run (0, l, len); run (1, r, len); break;
                case 1: run (0, l, len); break;
                case 2: run (1, r, len); break;
                case 3:
                case 4:
                {
                    double m[kSub], s[kSub];
                    for (int i = 0; i < len; ++i)
                    {
                        m[i] = 0.5 * (l[i] + r[i]);
                        s[i] = 0.5 * (l[i] - r[i]);
                    }
                    run (place == 3 ? 0 : 1, place == 3 ? m : s, len);
                    for (int i = 0; i < len; ++i)
                    {
                        l[i] = m[i] + s[i];
                        r[i] = m[i] - s[i];
                    }
                    break;
                }
                default: break;
            }
        }
    }
}

// ---------------------------------------------------------------- linear path

void ParametricEqModule::designLinearIr()
{
    const double rate = workerFs.load();
    const int N = kLinearLens[(int) getParam (gLinQ)];
    bool anyMs = false;
    for (int b = 0; b < kMaxBands; ++b)
        if (bandOn (b) && (int) getParam (bandParam (b, fPlace)) >= 3)
            anyMs = true;

    // path 0 / path 1 views
    const int v0 = anyMs ? 2 : 0;
    const int v1 = anyMs ? 3 : 1;

    std::vector<double> ir[2];
    for (int path = 0; path < 2; ++path)
    {
        const int view = path == 0 ? v0 : v1;
        std::vector<std::complex<double>> spec ((size_t) N);
        for (int k = 0; k <= N / 2; ++k)
        {
            const double f = std::max (1.0, k * rate / N);
            double db = 0.0;
            for (int b = 0; b < kMaxBands; ++b)
            {
                if (! bandOn (b))
                    continue;
                const int place = (int) getParam (bandParam (b, fPlace));
                const bool use = anyMs ? (place == 0 || place == 1 || place == 2 || (view == 2 ? place == 3 : place == 4))
                                       : affects (place, view);
                if (! use)
                    continue;
                const auto d = designBand (b, rate, 0.0, false);
                double m = 1.0;
                for (int i = 0; i < d.n; ++i)
                    m *= d.s[i].magnitudeAt (rate, f);
                db += gainToDb (m, -200.0);
            }
            const double mag = dbToGain (db);
            spec[(size_t) k] = mag;
            if (k > 0 && k < N / 2)
                spec[(size_t) (N - k)] = mag;
        }
        for (auto& c : spec) c = std::conj (c);
        fft (spec); // real, even -> zero-phase impulse (scaled by N)
        ir[path].assign ((size_t) N, 0.0);
        const auto w = makeWindow (WindowType::blackmanHarris4, N);
        for (int i = 0; i < N; ++i)
        {
            const int src = (i + N / 2) % N; // centre the impulse at N/2
            // periodic window is centred at N/2 as well
            ir[path][(size_t) i] = spec[(size_t) src].real() / N * w[(size_t) i];
        }
    }

    if (conv[0].loadIr (ir[0]) && conv[1].loadIr (ir[1]))
        linearMs = anyMs ? 1 : 0;
}

void ParametricEqModule::workerLoop()
{
    unsigned lastQuality = ~0u;
    double lastRate = 0.0;
    while (! quit.load())
    {
        if (linearActive.load())
        {
            const unsigned v = paramVersion();
            const unsigned qual = (unsigned) getParam (gLinQ);
            if (v != designedVersion.load() || qual != lastQuality || workerFs.load() != lastRate)
            {
                designedVersion = v;
                lastQuality = qual;
                lastRate = workerFs.load();
                designLinearIr();
            }
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (25));
    }
}

// ---------------------------------------------------------------- process

void ParametricEqModule::process (float* const* io, int numCh, int n)
{
    const auto phase = (Phase) (int) getParam (gPhase);
    linearActive.store (phase == Phase::linear);
    const double out = dbToGain (getParam (gOut));
    float* L = io[0];
    float* R = numCh > 1 ? io[1] : io[0];

    for (int start = 0; start < n; start += maxBlock)
    {
        const int len = std::min (maxBlock, n - start);
        float* l = L + start;
        float* r = R + start;

        if (phase == Phase::linear)
        {
            const bool ms = linearMs.load() == 1;
            for (int i = 0; i < len; ++i)
            {
                float a = l[i], b = r[i];
                if (ms)
                {
                    const float m = 0.5f * (a + b), s = 0.5f * (a - b);
                    const float ym = conv[0].processSample (m), ys = conv[1].processSample (s);
                    a = ym + ys;
                    b = ym - ys;
                }
                else
                {
                    a = conv[0].processSample (a);
                    b = conv[1].processSample (b);
                }
                l[i] = a * (float) out;
                if (numCh > 1) r[i] = b * (float) out;
            }
            for (int bi = 0; bi < kMaxBands; ++bi)
                dynGain[(size_t) bi].store (bandOn (bi) ? getParam (bandParam (bi, fGain)) : 0.0f, std::memory_order_relaxed);
            continue;
        }

        if (phase == Phase::natural)
        {
            double* ul = os[0].upsample (l, len);
            double* ur = os[1].upsample (r, len);
            processIir (ul, ur, len * 2, fs * 2.0);
            os[0].downsample (tmpL.data(), len);
            os[1].downsample (tmpR.data(), len);
            for (int i = 0; i < len; ++i)
            {
                l[i] = tmpL[(size_t) i] * (float) out;
                if (numCh > 1) r[i] = tmpR[(size_t) i] * (float) out;
            }
            continue;
        }

        for (int i = 0; i < len; ++i)
        {
            bufL[(size_t) i] = l[i];
            bufR[(size_t) i] = r[i];
        }
        processIir (bufL.data(), bufR.data(), len, fs);
        for (int i = 0; i < len; ++i)
        {
            l[i] = (float) (bufL[(size_t) i] * out);
            if (numCh > 1) r[i] = (float) (bufR[(size_t) i] * out);
        }
    }
}

} // namespace sonic
