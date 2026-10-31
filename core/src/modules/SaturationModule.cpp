#include "sonic/modules/SaturationModule.h"

#include <cmath>

namespace sonic
{

namespace
{
    enum P
    {
        pChar, pDrive,
        pH2, pH3, pH4, pH5, pH6, pH7, pH8, pH9, pH10,
        pEvenOdd, pDecay, pLevelDep, pSoft, pBias,
        pFocusLo, pFocusHi, pTilt, pLp, pStereo, pOs, pAuto, pOut,
        numP
    };
    constexpr int kFactors[] = { 1, 2, 4, 8 };

    const SaturationModule::Recipe kRecipes[SaturationModule::kNumRecipes] = {
        { "Silky",  "Even, gently falling. Smooth and sweet, like a triode.",
          { -14, -42, -30, -60, -46, -80, -62, -90, -72 }, 0.7f, 0.6f },
        { "Warm",   "Even + odd, round. Tape and console glow.",
          { -16, -19, -29, -33, -41, -47, -55, -61, -67 }, 0.6f, 0.6f },
        { "Round",  "Soft odd. Thick and dense, like a transformer.",
          { -42, -14, -52, -29, -62, -45, -72, -58, -82 }, 0.8f, 0.7f },
        { "Square", "Hard odd series. Boxy, fuzzy, aggressive.",
          { -90, -9.5f, -90, -14, -90, -16.9f, -90, -19.1f, -90 }, 0.1f, 0.3f },
        { "Sharp",  "Upper harmonics forward. Edge and bite, transistor-like.",
          { -32, -28, -26, -20, -24, -21, -25, -23, -27 }, 0.3f, 0.5f },
        { "Gritty", "Asymmetric hard clip. Dense, dirty, broadband.",
          { -11, -13, -17, -19, -21, -23, -25, -27, -29 }, 0.0f, 0.4f },
        { "Glassy", "Sparse high even. Airy shimmer without harshness.",
          { -38, -62, -19, -62, -25, -72, -33, -82, -41 }, 0.6f, 0.5f },
        { "Custom", "Your own harmonic recipe.",
          { -90, -90, -90, -90, -90, -90, -90, -90, -90 }, 0.5f, 0.5f },
    };
}

const SaturationModule::Recipe& SaturationModule::recipe (int index)
{
    return kRecipes[std::clamp (index, 0, kNumRecipes - 1)];
}

void SaturationModule::applyRecipe (int index)
{
    index = std::clamp (index, 0, kNumRecipes - 1);
    setParam (pChar, (float) index);
    if (index == kNumRecipes - 1)
        return; // Custom keeps the current harmonics
    const auto& r = kRecipes[index];
    for (int h = 0; h < kMaxHarmonic - 1; ++h)
        setParam (pH2 + h, r.levelsDbc[h]);
    setParam (pSoft, r.softness);
    setParam (pLevelDep, r.levelDependence);
    setParam (pEvenOdd, 0.0f);
    setParam (pDecay, 0.0f);
}

SaturationModule::SaturationModule()
{
    std::vector<std::string> names;
    for (auto& r : kRecipes)
        names.push_back (r.name);

    specs.push_back (ParamSpec::choice ("character", "Character", names, 1,
        "Harmonic recipe. Choosing one loads its harmonic levels below; 'Custom' leaves them as you set them.", true, "Character"));
    specs.push_back (ParamSpec::num ("drive_db", "Drive", "dB", 0, 36, 12, 12,
        "How hard the signal hits the shaper. More drive = more harmonics.", true, "Stage"));
    for (int h = 2; h <= kMaxHarmonic; ++h)
    {
        const float def = kRecipes[1].levelsDbc[h - 2];
        specs.push_back (ParamSpec::num ("h" + std::to_string (h) + "_dbc", "H" + std::to_string (h), "dBc", -90, 0, def, -30,
            "Level of harmonic " + std::to_string (h) + " relative to the fundamental, for a full-scale sine at the shaper. -90 = off.",
            false, "Harmonics"));
    }
    specs.push_back (ParamSpec::num ("even_odd", "Even / odd", "", -1, 1, 0, 0,
        "Tilts the recipe: + favours even harmonics (sweeter), - favours odd (harder). +-1 = +-12 dB.", false, "Shape"));
    specs.push_back (ParamSpec::num ("decay", "Decay", "", 0, 1, 0, 0,
        "Extra fade of upper harmonics: 1 = -6 dB per harmonic order above H2.", false, "Shape"));
    specs.push_back (ParamSpec::num ("level_dep", "Level dependence", "", 0, 1, 0.6f, 0,
        "0 = harmonic balance stays constant at any level. 1 = natural: harmonics grow strongly when pushed.", false, "Shape"));
    specs.push_back (ParamSpec::num ("softness", "Softness", "", 0, 1, 0.6f, 0,
        "Knee before the shaper. 0 = hard edge at full scale, 1 = gradual from -8 dB below full scale.", false, "Shape"));
    specs.push_back (ParamSpec::num ("bias", "Bias", "", -0.5f, 0.5f, 0, 0,
        "Shifts the operating point. Adds asymmetry (even harmonics) that grows with level.", false, "Shape"));
    specs.push_back (ParamSpec::num ("focus_lo_hz", "Focus low", "Hz", 20, 2000, 20, 150,
        "Frequencies below this pass clean (not saturated). 20 Hz = full range.", false, "Focus"));
    specs.push_back (ParamSpec::num ("focus_hi_hz", "Focus high", "Hz", 1000, 20000, 20000, 6000,
        "Frequencies above this pass clean. 20 kHz = full range.", false, "Focus"));
    specs.push_back (ParamSpec::num ("tilt_db", "Tone tilt", "dB", -6, 6, 0, 0,
        "Post-shaper tilt around 1 kHz. + = brighter.", false, "Tone"));
    specs.push_back (ParamSpec::num ("lp_hz", "Tone low-pass", "Hz", 2000, 22000, 22000, 9000,
        "Post-shaper low-pass to tame harshness. 22 kHz = off.", false, "Tone"));
    specs.push_back (ParamSpec::choice ("stereo", "Stereo", { "Linked L/R", "Mid/Side" }, 0,
        "Process left/right or mid/side channels.", false, "Stage"));
    specs.push_back (ParamSpec::choice ("oversampling", "Oversampling", { "1x", "2x", "4x", "8x" }, 2,
        "Higher = less aliasing, more CPU. Adds 64 samples latency when above 1x.", false, "Stage"));
    specs.push_back (ParamSpec::toggle ("auto_gain", "Auto gain", true,
        "Keeps small-signal level constant, so Drive changes tone rather than loudness.", false, "Stage"));
    specs.push_back (ParamSpec::num ("output_db", "Output", "dB", -24, 12, 0, 0, "Output level.", true, "Stage"));
    finaliseParams();
}

std::string SaturationModule::topology() const
{
    return "Band-focused waveshaper. Signal is split (subtractive 2nd-order Butterworth crossovers at Focus low/high); "
           "only the focus band is saturated. Focus band x drive -> optional envelope normalisation (gain = env^(levelDep-1), "
           "peak follower 1 ms / 300 ms, normalised to 0.98) -> soft knee limiter to |v|<=1 (knee = 1 - 0.6*softness, tanh above knee) -> "
           "y = sum_{n=1..10} c_n T_n(v + bias) with Chebyshev polynomials T_n, c_1 = 1, c_n = 10^(H_n/20) x even/odd tilt x decay; "
           "operating-point offset removed and normalised by small-signal slope. Runs inside a linear-phase polyphase FIR "
           "oversampler (Kaiser beta 9, 64 taps/phase). Post: tilt shelves at 1 kHz, optional 2nd-order low-pass, 8 Hz DC blocker, output gain.";
}

SaturationModule::Coeffs SaturationModule::makeCoeffs() const
{
    Coeffs k;
    k.c[1] = 1.0;
    const double eo = getParam (pEvenOdd);
    const double decay = getParam (pDecay);
    for (int n = 2; n <= kMaxHarmonic; ++n)
    {
        const double db = getParam (pH2 + (n - 2));
        if (db <= -89.9)
            continue;
        double c = dbToGain (db);
        c *= dbToGain ((n % 2 == 0 ? 12.0 : -12.0) * eo);
        c *= dbToGain (-6.0 * decay * (n - 2));
        k.c[n] = c;
    }
    k.knee = 1.0 - 0.6 * getParam (pSoft);
    k.v0 = shaperIn (getParam (pBias), k.knee);
    k.y0 = poly (k, k.v0);
    const double e = 1e-4;
    const double s = (poly (k, std::min (1.0, k.v0 + e)) - poly (k, std::max (-1.0, k.v0 - e))) / (2.0 * e);
    k.slope = std::abs (s) > 1e-3 ? s : 1.0;
    return k;
}

double SaturationModule::shaperIn (double a, double knee)
{
    const double m = std::abs (a);
    if (m <= knee)
        return a;
    if (knee >= 0.9999)
        return a > 0 ? 1.0 : -1.0;
    const double v = knee + (1.0 - knee) * std::tanh ((m - knee) / (1.0 - knee));
    return a > 0 ? v : -v;
}

double SaturationModule::poly (const Coeffs& k, double v)
{
    double t[kMaxHarmonic + 1];
    chebyshev (v, t, kMaxHarmonic);
    double y = 0.0;
    for (int n = 1; n <= kMaxHarmonic; ++n)
        y += k.c[n] * t[n];
    return y;
}

double SaturationModule::staticCurve (double x) const
{
    const auto k = makeCoeffs();
    const double drive = dbToGain (getParam (pDrive));
    const double v = shaperIn (x * drive + getParam (pBias), k.knee);
    double y = (poly (k, v) - k.y0) / k.slope;
    if (getParam (pAuto) > 0.5f)
        y /= drive;
    return y * dbToGain (getParam (pOut));
}

void SaturationModule::prepare (double sampleRate, int maxBlockSize, int)
{
    fs = sampleRate;
    maxBlock = std::max (1, maxBlockSize);
    factor = kFactors[(int) getParam (pOs)];
    for (auto& c : ch)
    {
        c.os.prepare (maxBlock);
        c.os.setFactor (factor);
        c.env.setTimes (fs, 0.001, 0.300);
        c.dc.prepare (fs, 8.0);
        c.dLo.allocate (256); c.dHi.allocate (256); c.dG.allocate (256);
    }
    lowBuf.assign ((size_t) maxBlock, 0.0f);
    highBuf.assign ((size_t) maxBlock, 0.0f);
    bandBuf.assign ((size_t) maxBlock, 0.0f);
    reset();
}

void SaturationModule::reset()
{
    for (auto& c : ch)
    {
        c.os.reset();
        c.lo.reset(); c.hi.reset();
        c.env.reset();
        c.dc.reset();
        c.tiltLo.reset(); c.tiltHi.reset(); c.lp.reset();
        c.dLo.clear(); c.dHi.clear(); c.dG.clear();
    }
}

int SaturationModule::latencySamples() const
{
    return kFactors[(int) getParam (pOs)] > 1 ? Oversampler::kLatency : 0;
}

void SaturationModule::process (float* const* io, int numCh, int n)
{
    numCh = std::min (numCh, 2);
    const int want = kFactors[(int) getParam (pOs)];
    if (want != factor)
    {
        factor = want;
        for (auto& c : ch)
            c.os.setFactor (factor);
    }

    const auto k = makeCoeffs();
    const double drive = dbToGain (getParam (pDrive));
    const double ld = getParam (pLevelDep);
    const double bias = getParam (pBias);
    const bool autoGain = getParam (pAuto) > 0.5f;
    const double out = dbToGain (getParam (pOut));
    const double fLo = getParam (pFocusLo), fHi = getParam (pFocusHi);
    const bool useLo = fLo > 21.0, useHi = fHi < 19990.0;
    const double tilt = getParam (pTilt), lpHz = getParam (pLp);
    const bool ms = getParam (pStereo) > 0.5f && numCh == 2;

    for (auto& c : ch)
    {
        c.lo.setCoeffs (BiquadCoeffs::lowPass (fs, fLo, 0.70710678));
        c.hi.setCoeffs (BiquadCoeffs::lowPass (fs, fHi, 0.70710678));
        c.tiltLo.setCoeffs (BiquadCoeffs::lowShelf (fs, 1000.0, 0.5, -tilt * 0.5));
        c.tiltHi.setCoeffs (BiquadCoeffs::highShelf (fs, 1000.0, 0.5, tilt * 0.5));
        c.lp.setCoeffs (lpHz < 21900.0 ? BiquadCoeffs::lowPass (fs, lpHz, 0.70710678) : BiquadCoeffs::identity());
    }

    for (int start = 0; start < n; start += maxBlock)
    {
        const int len = std::min (maxBlock, n - start);
        float* L = io[0] + start;
        float* R = numCh > 1 ? io[1] + start : nullptr;

        if (ms)
            for (int i = 0; i < len; ++i)
            {
                const float m = 0.5f * (L[i] + R[i]), s = 0.5f * (L[i] - R[i]);
                L[i] = m;
                R[i] = s;
            }

        for (int c = 0; c < numCh; ++c)
        {
            Chan& st = ch[(size_t) c];
            float* x = c == 0 ? L : R;

            // ---- focus split + drive + level normalisation ----
            for (int i = 0; i < len; ++i)
            {
                const double in = x[i];
                const double lo = useLo ? st.lo.process (in) : 0.0;
                const double rest = in - lo;
                const double band = useHi ? st.hi.process (rest) : rest;
                lowBuf[(size_t) i] = (float) lo;
                highBuf[(size_t) i] = (float) (rest - band);
                const double u = band * drive;
                const double e = st.env.process (std::abs (u));
                const double g = std::clamp (std::pow (std::max (e, 1e-4) / 0.98, ld - 1.0), 1e-3, 1e3);
                bandBuf[(size_t) i] = (float) (u * g);
                x[i] = (float) g; // temporarily store the normalisation gain
            }

            // ---- oversampled shaper ----
            double* os = st.os.upsample (bandBuf.data(), len);
            const int total = len * factor;
            for (int j = 0; j < total; ++j)
            {
                const double v = shaperIn (os[j] + bias, k.knee);
                os[j] = (poly (k, v) - k.y0) / k.slope;
            }
            st.os.downsample (bandBuf.data(), len);

            // ---- recombine + post ----
            const int lat = factor > 1 ? Oversampler::kLatency : 0;
            for (int i = 0; i < len; ++i)
            {
                st.dLo.push (lowBuf[(size_t) i]);
                st.dHi.push (highBuf[(size_t) i]);
                st.dG.push (x[i]);
                const double g = lat > 0 ? st.dG.readInt (lat) : x[i];
                double y = bandBuf[(size_t) i] / std::max (g, 1e-3);
                if (autoGain)
                    y /= drive;
                y += lat > 0 ? (double) st.dLo.readInt (lat) + st.dHi.readInt (lat) : (double) lowBuf[(size_t) i] + highBuf[(size_t) i];
                y = st.tiltHi.process (st.tiltLo.process (y));
                y = st.lp.process (y);
                y = st.dc.process (y);
                x[i] = (float) (y * out);
            }
        }

        if (ms)
            for (int i = 0; i < len; ++i)
            {
                const float m = L[i], s = R[i];
                L[i] = m + s;
                R[i] = m - s;
            }
    }
}

} // namespace sonic
