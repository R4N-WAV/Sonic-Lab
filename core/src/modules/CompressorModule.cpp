#include "sonic/modules/CompressorModule.h"

#include <cmath>

namespace sonic
{

namespace
{
    enum P { pModel, pThr, pRatio, pAtt, pRel, pAutoRel, pKnee, pMakeup, pAutoMakeup,
             pDet, pScHpf, pLink, pProg, pColor, numP };

    double coeff (double fs, double ms) { return ms > 0.0 ? std::exp (-1.0 / (ms * 0.001 * fs)) : 0.0; }
}

CompressorModule::CompressorModule()
{
    specs = {
        ParamSpec::choice ("model", "Model", { "Opto", "FET", "VCA", "Vari-Mu", "Diode bridge" }, 0,
            "Circuit topology. Each has its own detector, timing and gain-element character.", true, "Main"),
        ParamSpec::num ("threshold_db", "Threshold", "dB", -60, 0, -18, -18,
            "Level above which compression starts.", true, "Main"),
        ParamSpec::num ("ratio", "Ratio", ":1", 1, 20, 4, 4,
            "Steady-state ratio above the threshold (Vari-Mu: maximum ratio, reached gradually).", true, "Main"),
        ParamSpec::num ("attack_ms", "Attack", "ms", 0.02f, 300, 10, 10,
            "How fast gain reduction engages.", false, "Timing"),
        ParamSpec::num ("release_ms", "Release", "ms", 5, 5000, 200, 200,
            "How fast gain recovers. Opto: sets the slow second stage; the first stage is always about 60 ms.", false, "Timing"),
        ParamSpec::toggle ("auto_release", "Auto release", false,
            "Programme-dependent release: fast after transients, slow after sustained compression.", false, "Timing"),
        ParamSpec::num ("knee_db", "Knee", "dB", 0, 24, 6, 0,
            "Width of the soft transition around the threshold.", false, "Curve"),
        ParamSpec::num ("makeup_db", "Makeup", "dB", -12, 24, 0, 0,
            "Gain after compression.", true, "Main"),
        ParamSpec::toggle ("auto_makeup", "Auto makeup", false,
            "Adds half of the static gain reduction at 0 dBFS as makeup.", false, "Main"),
        ParamSpec::choice ("detector", "Detector", { "Peak", "RMS" }, 0,
            "Peak reacts to transients; RMS (10 ms) to average loudness.", false, "Detector"),
        ParamSpec::num ("sc_hpf_hz", "Sidechain HPF", "Hz", 20, 500, 20, 100,
            "High-pass on the detector so bass does not drive the compression. 20 Hz = off.", false, "Detector"),
        ParamSpec::num ("link_pct", "Stereo link", "%", 0, 100, 100, 0,
            "100 % = both channels get the same gain reduction (stable image).", false, "Detector"),
        ParamSpec::num ("program_dep", "Programme dependence", "", 0, 1, 0.5f, 0,
            "How strongly timing adapts to the material (Opto memory, auto-release blend).", false, "Timing"),
        ParamSpec::num ("color", "Colour", "", 0, 1, 0.3f, 0,
            "Nonlinearity of the gain element (model-specific harmonics that grow with gain reduction).", false, "Curve"),
    };
    finaliseParams();
}

std::string CompressorModule::topology() const
{
    static const char* names[] = { "Opto", "FET", "VCA", "Vari-Mu", "Diode bridge" };
    std::string s = "Model: ";
    s += names[(int) getParam (pModel)];
    s += ". Detector: sidechain 2nd-order HPF -> peak or 10 ms RMS -> stereo link (max-linked blend). "
         "Gain computer in dB with quadratic soft knee. Feed-forward models (VCA, Diode) use slope (1/R - 1) on the input level; "
         "feedback models (Opto, FET, Vari-Mu) detect the previous output sample and use slope (1 - R), which gives the same "
         "steady-state ratio R. Ballistics are one-pole in the dB domain (attack when reduction increases, release otherwise). "
         "Opto: release blends a 60 ms stage with the Release time, weighted by a 2 s programme memory. "
         "Vari-Mu: ratio rises from 1.5:1 to R over 12 dB of overshoot. Auto release = min of 40 ms and 4x Release branches. "
         "Gain element colour: FET adds GR-scaled cubic saturation, Vari-Mu/Opto add GR-scaled square-law (even) terms, "
         "Diode adds both; even terms are followed by an 8 Hz DC blocker.";
    return s;
}

double CompressorModule::computerDb (double level, Model m, double ratio) const
{
    const double thr = getParam (pThr);
    const double knee = std::max ((double) getParam (pKnee), m == Model::opto ? 6.0 : 0.0);
    const bool feedback = m == Model::opto || m == Model::fet || m == Model::varimu;
    const double over = level - thr;

    double R = ratio;
    if (m == Model::varimu)
        R = 1.5 + (ratio - 1.5) * std::clamp (over / 12.0, 0.0, 1.0);
    R = std::max (1.0, R);
    const double slope = feedback ? (1.0 - R) : (1.0 / R - 1.0);

    if (knee > 0.0 && std::abs (over) <= knee * 0.5)
    {
        const double t = over + knee * 0.5;
        return slope * t * t / (2.0 * knee);
    }
    return over > 0.0 ? slope * over : 0.0;
}

double CompressorModule::staticCurveDb (double in) const
{
    const auto m = (Model) (int) getParam (pModel);
    const bool feedback = m == Model::opto || m == Model::fet || m == Model::varimu;
    const double ratio = getParam (pRatio);
    auto noMakeup = [&] (double x)
    {
        if (! feedback)
            return x + computerDb (x, m, ratio);
        // solve o = x + computer(o): f(o) = o - x - computer(o) is increasing -> bisection
        double lo = x - 200.0, hi = x;
        for (int i = 0; i < 80; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            if (mid - x - computerDb (mid, m, ratio) > 0.0) hi = mid; else lo = mid;
        }
        return 0.5 * (lo + hi);
    };
    double makeup = getParam (pMakeup);
    if (getParam (pAutoMakeup) > 0.5f)
        makeup -= 0.5 * noMakeup (0.0);
    return noMakeup (in) + makeup;
}

void CompressorModule::prepare (double sampleRate, int, int)
{
    fs = sampleRate;
    for (auto& c : ch)
        c.dc.prepare (fs, 8.0);
    reset();
}

void CompressorModule::reset()
{
    for (auto& c : ch)
    {
        c.hpf.reset();
        c.ms = c.gr = c.grSlow = c.grFast = c.memory = c.prevOut = 0.0;
        c.dc.reset();
    }
    grMeter = 0.0f;
}

void CompressorModule::process (float* const* io, int numCh, int n)
{
    numCh = std::min (numCh, 2);
    const auto m = (Model) (int) getParam (pModel);
    const bool feedback = m == Model::opto || m == Model::fet || m == Model::varimu;
    const double ratio = getParam (pRatio);
    const double relMs = getParam (pRel);
    const double aAtt = coeff (fs, getParam (pAtt));
    const double aRel = coeff (fs, relMs);
    const double aFast = coeff (fs, m == Model::opto ? 60.0 : 40.0);
    const double aSlow = coeff (fs, relMs * 4.0);
    const double aRms = coeff (fs, 10.0);
    const double aMem = coeff (fs, 2000.0);
    const double aMemOpto = coeff (fs, std::max (3000.0, relMs * 3.0)); // opto memory fades slower than its release
    const bool autoRel = getParam (pAutoRel) > 0.5f;
    const bool rms = getParam (pDet) > 0.5f;
    const double link = getParam (pLink) * 0.01;
    const double prog = getParam (pProg);
    const double color = getParam (pColor);
    const double hpfHz = getParam (pScHpf);

    // makeup = manual + (auto ? half the static reduction at 0 dBFS : 0); staticCurveDb includes both
    double makeupDb = getParam (pMakeup);
    if (getParam (pAutoMakeup) > 0.5f)
    {
        // staticCurveDb(0) = reduction(0) + manual + auto, with auto = -0.5 * reduction(0)
        // => auto part = -(staticCurveDb(0) - manual)   (since reduction + auto = -auto)
        const double total0 = staticCurveDb (0.0);
        makeupDb = getParam (pMakeup) - (total0 - getParam (pMakeup));
    }
    const double makeupGain = dbToGain (makeupDb);

    for (auto& c : ch)
        c.hpf.setCoeffs (hpfHz > 21.0 ? BiquadCoeffs::highPass (fs, hpfHz, 0.70710678) : BiquadCoeffs::identity());

    float maxGr = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        double lvl[2] = { -200.0, -200.0 };
        for (int c = 0; c < numCh; ++c)
        {
            Chan& st = ch[(size_t) c];
            const double det = st.hpf.process (feedback ? st.prevOut : (double) io[c][i]);
            double a = std::abs (det);
            if (rms)
            {
                st.ms = det * det + aRms * (st.ms - det * det);
                a = std::sqrt (std::max (st.ms, 0.0));
            }
            lvl[c] = gainToDb (a, -200.0);
        }
        if (numCh == 2)
        {
            const double mx = std::max (lvl[0], lvl[1]);
            lvl[0] = link * mx + (1.0 - link) * lvl[0];
            lvl[1] = link * mx + (1.0 - link) * lvl[1];
        }

        for (int c = 0; c < numCh; ++c)
        {
            Chan& st = ch[(size_t) c];
            const double target = computerDb (lvl[c], m, ratio);

            // programme memory: rises while compressing (> 1 dB), decays slowly afterwards
            const double busy = target < -1.0 ? 1.0 : 0.0;
            st.memory = flushDenormal (busy + (m == Model::opto && busy < 0.5 ? aMemOpto : aMem) * (st.memory - busy));

            if (m == Model::opto)
            {
                // Two-stage optical release: part of the gain reduction recovers in ~60 ms, the
                // rest at the Release setting. The longer/harder the compression (memory), the
                // larger the slow share - like a light-dependent resistor that "remembers".
                const double w = std::clamp (0.25 + 0.75 * st.memory * prog, 0.0, 1.0);
                st.grFast = target + (target < st.grFast ? aAtt : aFast) * (st.grFast - target);
                st.grSlow = target + (target < st.grSlow ? aAtt : aRel) * (st.grSlow - target);
                st.grFast = flushDenormal (st.grFast);
                st.grSlow = flushDenormal (st.grSlow);
                st.gr = (1.0 - w) * st.grFast + w * st.grSlow;
            }
            else
            {
                double relCoeff = aRel;
                if (autoRel)
                {
                    const double w = std::clamp (st.memory * (0.3 + 0.7 * prog), 0.0, 1.0);
                    relCoeff = aFast + w * (aSlow - aFast);
                }
                if (target < st.gr)
                    st.gr = target + aAtt * (st.gr - target);
                else
                    st.gr = target + relCoeff * (st.gr - target);
            }
            st.gr = flushDenormal (st.gr);

            const double G = dbToGain (st.gr);
            double y = io[c][i] * G;

            // gain-element colour, scaled by how much reduction is applied
            const double depth = std::clamp (1.0 - G, 0.0, 1.0) * color;
            switch (m)
            {
                case Model::fet:    y = y - depth * 0.6 * y * y * y; break;
                case Model::varimu: y = y + depth * 0.35 * y * y; break;
                case Model::opto:   y = y + depth * 0.12 * y * y; break;
                case Model::diode:  y = y + depth * (0.3 * y * y - 0.4 * y * y * y); break;
                case Model::vca:    break;
            }
            if (m == Model::varimu || m == Model::opto || m == Model::diode)
                y = st.dc.process (y);

            st.prevOut = y;
            const float outS = (float) (y * makeupGain);
            io[c][i] = std::isfinite (outS) ? outS : 0.0f;
            maxGr = std::min (maxGr, (float) st.gr);
        }
    }
    grMeter.store (maxGr, std::memory_order_relaxed);
}

bool CompressorModule::isParamRelevant (int index) const
{
    // The optical model always uses its own two-stage, programme-dependent release.
    if (index == pAutoRel)
        return (Model) (int) getParam (pModel) != Model::opto;
    return true;
}

std::string CompressorModule::irrelevantReason (int index) const
{
    if (index == pAutoRel && ! isParamRelevant (index))
        return "Opto always uses its own two-stage release (about 60 ms, then the Release setting).";
    return {};
}

} // namespace sonic
