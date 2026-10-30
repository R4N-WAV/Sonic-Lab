#include "sonic/modules/EqModule.h"
#include "sonic/DspUtils.h"

#include <cmath>

namespace sonic
{

namespace
{
    enum P
    {
        hpOn, hpFreq,
        lsFreq, lsGain,
        bellFreq, bellGain, bellQ,
        hsFreq, hsGain,
        lpOn, lpFreq,
        numP
    };

    constexpr int kSubBlock = 32; // coefficient update interval (samples)
}

EqModule::EqModule()
{
    specs = {
        { "hp_enabled",   "HPF",        "",   0, 1, 0, 0, { "Off", "On" }, "12 dB/oct Butterworth high-pass on/off." },
        { "hp_freq_hz",   "HPF Freq",   "Hz", 10, 1000, 30, 100, {}, "High-pass cutoff (-3 dB point)." },
        { "ls_freq_hz",   "Low Freq",   "Hz", 20, 1000, 100, 150, {}, "Low-shelf corner frequency." },
        { "ls_gain_db",   "Low Gain",   "dB", -18, 18, 0, 0, {}, "Low-shelf gain." },
        { "bell_freq_hz", "Mid Freq",   "Hz", 40, 18000, 1000, 1000, {}, "Bell centre frequency." },
        { "bell_gain_db", "Mid Gain",   "dB", -18, 18, 0, 0, {}, "Bell gain." },
        { "bell_q",       "Mid Q",      "",   0.1f, 10, 0.707f, 1.0f, {}, "Bell bandwidth (higher = narrower)." },
        { "hs_freq_hz",   "High Freq",  "Hz", 1000, 20000, 8000, 6000, {}, "High-shelf corner frequency." },
        { "hs_gain_db",   "High Gain",  "dB", -18, 18, 0, 0, {}, "High-shelf gain." },
        { "lp_enabled",   "LPF",        "",   0, 1, 0, 0, { "Off", "On" }, "12 dB/oct Butterworth low-pass on/off." },
        { "lp_freq_hz",   "LPF Freq",   "Hz", 1000, 22000, 20000, 8000, {}, "Low-pass cutoff (-3 dB point)." },
    };
    finaliseParams();
}

EqModule::Design EqModule::design (double sr, const float* v)
{
    Design d;
    d[0] = v[hpOn] > 0.5f ? BiquadCoeffs::highPass (sr, v[hpFreq], 0.70710678) : BiquadCoeffs::identity();
    d[1] = BiquadCoeffs::lowShelf (sr, v[lsFreq], 1.0, v[lsGain]);
    d[2] = BiquadCoeffs::peaking (sr, v[bellFreq], v[bellQ], v[bellGain]);
    d[3] = BiquadCoeffs::highShelf (sr, v[hsFreq], 1.0, v[hsGain]);
    d[4] = v[lpOn] > 0.5f ? BiquadCoeffs::lowPass (sr, v[lpFreq], 0.70710678) : BiquadCoeffs::identity();
    return d;
}

EqModule::Design EqModule::currentDesign (double sr) const
{
    float v[numP];
    for (int i = 0; i < numP; ++i)
        v[i] = getParam (i);
    return design (sr, v);
}

double EqModule::magnitudeAt (const Design& d, double sr, double f)
{
    double m = 1.0;
    for (auto& c : d)
        m *= c.magnitudeAt (sr, f);
    return m;
}

double EqModule::phaseAt (const Design& d, double sr, double f)
{
    std::complex<double> h (1.0, 0.0);
    for (auto& c : d)
        h *= c.responseAt (sr, f);
    return std::arg (h);
}

void EqModule::prepare (double sampleRate, int, int)
{
    fs = sampleRate;
    smoothed.resize (numP);
    for (int i = 0; i < numP; ++i)
        smoothed[(size_t) i] = getParam (i);
    // ~20 ms smoothing applied per sub-block
    smoothCoeff = std::exp (-(double) kSubBlock / (0.02 * fs));
    reset();
}

void EqModule::reset()
{
    for (auto& ch : filters)
        for (auto& f : ch)
            f.reset();
}

void EqModule::process (float* const* ch, int numCh, int n)
{
    numCh = std::min (numCh, kMaxChannels);

    for (int start = 0; start < n; start += kSubBlock)
    {
        const int len = std::min (kSubBlock, n - start);

        // Smooth continuous params; switch discrete params immediately.
        for (int i = 0; i < numP; ++i)
        {
            const float target = getParam (i);
            if (specs[(size_t) i].isChoice())
                smoothed[(size_t) i] = target;
            else
                smoothed[(size_t) i] = (float) (target + smoothCoeff * (smoothed[(size_t) i] - target));
        }

        const Design d = design (fs, smoothed.data());

        for (int c = 0; c < numCh; ++c)
        {
            auto& bank = filters[(size_t) c];
            for (int s = 0; s < kNumSections; ++s)
                bank[(size_t) s].setCoeffs (d[(size_t) s]);

            float* x = ch[c] + start;
            for (int i = 0; i < len; ++i)
            {
                double y = x[i];
                for (auto& f : bank)
                    y = f.process (y);
                x[i] = (float) y;
            }
        }
    }
}

} // namespace sonic
