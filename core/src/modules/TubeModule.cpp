#include "sonic/modules/TubeModule.h"

#include <algorithm>
#include <cmath>

namespace sonic
{

namespace
{
    enum P { pDrive, pBias, pChar, pSag, pHf, pOs, pComp, pOut, numP };
    constexpr int kFactors[] = { 1, 2, 4, 8 };
    constexpr double kBiasScale = 0.5;   // bias param (-1..1) -> operating point offset
    constexpr double kSagDepth = 0.6;    // how far grid current can shift the bias
}

TubeModule::TubeModule()
{
    specs = {
        { "drive_db", "Drive", "dB", 0, 36, 6, 12, {},
          "Gain into the tube's nonlinearity. Higher = more saturation and harmonics." },
        { "bias", "Bias", "", -1, 1, 0, 0, {},
          "Operating point. Moving away from centre makes the curve more asymmetric (more even harmonics)." },
        { "character", "Character", "", 0, 1, 0, 0, {},
          "0 = triode (soft, asymmetric, 2nd-harmonic rich). 1 = pentode (harder, symmetric, odd-harmonic rich)." },
        { "sag", "Sag", "", 0, 1, 0.3f, 0, {},
          "Dynamic bias shift from grid current on loud peaks. Adds program-dependent compression and evolving harmonics." },
        { "hf_rolloff_hz", "HF Roll-off", "Hz", 2000, 40000, 22000, 10000, {},
          "Input low-pass before the nonlinearity (Miller capacitance). Lower = darker saturation." },
        { "oversampling", "Oversampling", "", 0, 3, 2, 0, { "1x", "2x", "4x", "8x" },
          "Runs the nonlinearity at a higher rate to suppress aliasing. Adds 64 samples latency when > 1x." },
        { "gain_comp", "Gain Comp", "", 0, 1, 1, 0, { "Off", "Unity" },
          "Unity: small signals pass at 0 dB regardless of Drive, so Drive changes tone, not loudness." },
        { "output_db", "Output", "dB", -36, 24, 0, 0, {}, "Output trim." },
    };
    finaliseParams();
}

TubeModule::Shape TubeModule::shapeFor (double c)
{
    c = std::clamp (c, 0.0, 1.0);
    Shape s;
    s.gainPos = 1.8 + (1.0 - 1.8) * c;   // triode clips harder on the positive (grid conduction) side
    s.gainNeg = 0.55 + (1.0 - 0.55) * c; // ... and softer towards cut-off
    s.hardness = 2.0 + (5.0 - 2.0) * c;
    return s;
}

double TubeModule::saturate (const Shape& s, double u)
{
    const double g = u >= 0.0 ? s.gainPos : s.gainNeg;
    const double v = g * u;
    return v / std::pow (1.0 + std::pow (std::fabs (v), s.hardness), 1.0 / s.hardness) / g;
}

double TubeModule::slopeAt (const Shape& s, double u)
{
    const double g = u >= 0.0 ? s.gainPos : s.gainNeg;
    const double v = std::fabs (g * u);
    return std::pow (1.0 + std::pow (v, s.hardness), -1.0 / s.hardness - 1.0);
}

double TubeModule::staticCurve (const float* v, double x)
{
    const Shape s = shapeFor (v[pChar]);
    const double g = dbToGain (v[pDrive]);
    const double b = v[pBias] * kBiasScale;
    const double norm = v[pComp] > 0.5f ? g * slopeAt (s, b) : 1.0;
    return (saturate (s, g * x + b) - saturate (s, b)) / norm * dbToGain (v[pOut]);
}

double TubeModule::staticCurve (double x) const
{
    float v[numP];
    for (int i = 0; i < numP; ++i)
        v[i] = getParam (i);
    return staticCurve (v, x);
}

void TubeModule::prepare (double sampleRate, int maxBlockSize, int)
{
    fs = sampleRate;
    maxBlock = std::max (1, maxBlockSize);
    activeFactor = kFactors[(int) getParam (pOs)];

    for (auto& c : chans)
    {
        c.os.prepare (maxBlock); // designs all kernels, sizes buffers for 8x
        c.os.setFactor (activeFactor);
        c.dc.prepare (fs, 8.0);
    }

    scratch.assign ((size_t) maxBlock * 6, 0.0f);

    driveSm.prepare (fs, 0.02);
    biasSm.prepare (fs, 0.02);
    charSm.prepare (fs, 0.05);
    outSm.prepare (fs, 0.02);
    driveSm.snap (dbToGain (getParam (pDrive)));
    biasSm.snap (getParam (pBias) * kBiasScale);
    charSm.snap (getParam (pChar));
    outSm.snap (dbToGain (getParam (pOut)));
    reset();
}

void TubeModule::reset()
{
    for (auto& c : chans)
    {
        c.os.reset();
        c.lpState = 0.0;
        c.env = 0.0;
        c.dc.reset();
    }
}

int TubeModule::latencySamples() const
{
    return kFactors[(int) getParam (pOs)] > 1 ? Oversampler::kLatency : 0;
}

void TubeModule::process (float* const* ch, int numCh, int n)
{
    numCh = std::min (numCh, kMaxChannels);

    // Oversampling factor change (real-time safe: kernels precomputed in prepare()).
    const int wanted = kFactors[(int) getParam (pOs)];
    if (wanted != activeFactor)
    {
        activeFactor = wanted;
        for (auto& c : chans)
            c.os.setFactor (activeFactor);
    }

    // Process in chunks no larger than maxBlock.
    for (int start = 0; start < n; start += maxBlock)
    {
        const int len = std::min (maxBlock, n - start);

        float* drive = scratch.data();
        float* bias = drive + maxBlock;
        float* chr = bias + maxBlock;
        float* outG = chr + maxBlock;
        float* tmp = outG + maxBlock;

        const double tDrive = dbToGain (getParam (pDrive));
        const double tBias = getParam (pBias) * kBiasScale;
        const double tChar = getParam (pChar);
        const double tOut = dbToGain (getParam (pOut));
        for (int i = 0; i < len; ++i)
        {
            drive[i] = (float) driveSm.next (tDrive);
            bias[i] = (float) biasSm.next (tBias);
            chr[i] = (float) charSm.next (tChar);
            outG[i] = (float) outSm.next (tOut);
        }

        const bool comp = getParam (pComp) > 0.5f;
        const double sag = getParam (pSag) * kSagDepth;
        const int F = activeFactor;
        const double osRate = fs * F;
        const double hf = std::min ((double) getParam (pHf), osRate * 0.45);
        const double lpCoeff = std::exp (-kTwoPi * hf / osRate);
        const double envAtt = std::exp (-1.0 / (0.002 * osRate));
        const double envRel = std::exp (-1.0 / (0.120 * osRate));

        for (int c = 0; c < numCh; ++c)
        {
            Channel& st = chans[(size_t) c];
            float* x = ch[c] + start;

            for (int i = 0; i < len; ++i)
                tmp[i] = x[i] * drive[i];

            double* u = st.os.upsample (tmp, len);

            for (int i = 0; i < len; ++i)
            {
                const Shape s = shapeFor (chr[i]);
                const double b = bias[i];
                const double norm = comp ? drive[i] * slopeAt (s, b) : 1.0;
                const double offset = saturate (s, b);

                for (int p = 0; p < F; ++p)
                {
                    double& v = u[i * F + p];

                    // Miller-capacitance input low-pass
                    st.lpState = v + lpCoeff * (st.lpState - v);
                    const double in = st.lpState;

                    // Grid-current envelope (positive excursions charge the coupling cap)
                    const double pos = std::max (0.0, in + b);
                    st.env = pos > st.env ? pos + envAtt * (st.env - pos)
                                          : pos + envRel * (st.env - pos);
                    st.env = flushDenormal (st.env);

                    const double bEff = b - sag * st.env;
                    v = (saturate (s, in + bEff) - offset) / norm;
                }
            }

            st.os.downsample (tmp, len);

            for (int i = 0; i < len; ++i)
                x[i] = (float) (st.dc.process (tmp[i]) * outG[i]);
        }
    }
}

} // namespace sonic
