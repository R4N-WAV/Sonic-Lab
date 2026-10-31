#include "sonic/modules/CharacterModules.h"

#include <cmath>

namespace sonic
{

namespace
{
    constexpr int kFactors[] = { 1, 2, 4, 8 };
}

// =============================================================== TAPE

namespace tape
{
    enum P { pSpeed, pForm, pIn, pBias, pSat, pBump, pHf, pWow, pFlut, pNoise, pEmph, pOs, pAuto, pOut };
    constexpr double kBumpHz[] = { 40.0, 70.0, 120.0 };
    constexpr double kGapHz[] = { 9000.0, 15000.0, 22000.0 };
    constexpr double kEmphDb[] = { 10.0, 7.0, 5.0 };
    constexpr double kHeadroom[] = { 0.7, 1.0, 1.4 };
    constexpr double kWidth[] = { 0.12, 0.08, 0.05 };
    constexpr double kBaseDelayMs = 5.0;
}

TapeModule::TapeModule()
{
    using namespace tape;
    specs = {
        ParamSpec::choice ("speed", "Speed", { "7.5 ips", "15 ips", "30 ips" }, 1,
            "Tape speed. Slower = lower head bump, earlier high-frequency loss, more colour.", true, "Machine"),
        ParamSpec::choice ("formulation", "Formulation", { "Vintage", "Modern", "Hot" }, 1,
            "Tape type: Vintage saturates early, Hot has the most headroom.", false, "Machine"),
        ParamSpec::num ("input_db", "Input", "dB", -12, 24, 0, 0, "Recording level: how hard you hit the tape.", true, "Machine"),
        ParamSpec::num ("bias", "Bias", "", 0, 1, 0.5f, 0,
            "Recording bias. Low = more distortion and brighter, high = cleaner and darker.", false, "Magnetics"),
        ParamSpec::num ("saturation", "Saturation", "", 0, 1, 0.5f, 0, "Strength of the magnetic hysteresis.", false, "Magnetics"),
        ParamSpec::num ("head_bump", "Head bump", "", 0, 1, 0.5f, 0, "Low-frequency resonance of the playback head.", false, "Response"),
        ParamSpec::num ("hf_loss", "HF loss", "", 0, 1, 0.5f, 0, "Gap / spacing loss of the highest frequencies.", false, "Response"),
        ParamSpec::num ("wow", "Wow", "", 0, 1, 0.0f, 0, "Slow pitch drift (0.5 Hz + random). In a parallel path it will gently flange against the dry signal.", false, "Transport"),
        ParamSpec::num ("flutter", "Flutter", "", 0, 1, 0.0f, 0, "Fast pitch modulation (6 and 11 Hz).", false, "Transport"),
        ParamSpec::num ("noise_db", "Hiss", "dB", -120, -40, -120, -80, "Tape hiss level. -120 = off.", false, "Transport"),
        ParamSpec::toggle ("emphasis", "Emphasis", true, "Record pre-emphasis / playback de-emphasis (high frequencies saturate earlier).", false, "Response"),
        ParamSpec::choice ("oversampling", "Oversampling", { "1x", "2x", "4x", "8x" }, 1, "Anti-aliasing for the nonlinearity.", false, "Machine"),
        ParamSpec::toggle ("auto_gain", "Auto gain", true, "Compensates the Input level at the output.", false, "Machine"),
        ParamSpec::num ("output_db", "Output", "dB", -24, 12, 0, 0, "Output level.", true, "Machine"),
    };
    finaliseParams();
}

std::string TapeModule::topology() const
{
    return "Record: pre-emphasis high shelf (+5..10 dB at 3 kHz by speed) -> input gain / formulation headroom -> "
           "hysteresis: m = 0.6*tanh(h) + 0.4*tanh(p), p = play operator (backlash) of h with level-dependent radius "
           "r = width(formulation) x (1.25 - bias) x saturation x 2 x tanh(2h)^2 (AC bias linearises small signals), oversampled. Playback: de-emphasis (inverse shelf), "
           "head bump = peaking filter at 40/70/120 Hz (by speed), gap loss = two 2nd-order low-passes at 9/15/22 kHz scaled by HF loss and bias. "
           "Wow & flutter: cubic-interpolated delay around 5 ms modulated by 0.5 Hz + smoothed random drift (up to 1.5 ms) and 6/11 Hz "
           "(up to 0.12 ms). Hiss: white noise, 1-pole low-passed at 9 kHz. 8 Hz DC blocker.";
}

void TapeModule::prepare (double sampleRate, int maxBlockSize, int)
{
    fs = sampleRate;
    maxBlock = std::max (1, maxBlockSize);
    factor = kFactors[(int) getParam (tape::pOs)];
    for (auto& c : ch)
    {
        c.os.prepare (maxBlock);
        c.os.setFactor (factor);
        c.wf.allocate ((int) (fs * 0.02));
        c.dc.prepare (fs, 8.0);
        c.hissLp.setCutoff (fs, 9000.0);
    }
    wow.set (fs, 0.5);
    flutter.set (fs, 6.0);
    flutter2.set (fs, 11.0);
    wowSmooth.setCutoff (fs, 0.3);
    tmp.assign ((size_t) maxBlock, 0.0f);
    reset();
}

void TapeModule::reset()
{
    for (auto& c : ch)
    {
        c.os.reset();
        c.pre.reset(); c.de.reset(); c.bump.reset(); c.gap1.reset(); c.gap2.reset();
        c.play = 0.0;
        c.wf.clear();
        c.dc.reset();
        c.hissLp.reset();
    }
    wowSmooth.reset();
}

int TapeModule::latencySamples() const
{
    int lat = kFactors[(int) getParam (tape::pOs)] > 1 ? Oversampler::kLatency : 0;
    if (getParam (tape::pWow) > 0.0f || getParam (tape::pFlut) > 0.0f)
        lat += (int) std::lround (tape::kBaseDelayMs * 0.001 * fs);
    return lat;
}

void TapeModule::process (float* const* io, int numCh, int n)
{
    using namespace tape;
    numCh = std::min (numCh, 2);
    const int want = kFactors[(int) getParam (pOs)];
    if (want != factor)
    {
        factor = want;
        for (auto& c : ch) c.os.setFactor (factor);
    }

    const int sp = (int) getParam (pSpeed), fm = (int) getParam (pForm);
    const double drive = dbToGain (getParam (pIn));
    const double bias = getParam (pBias);
    const double head = kHeadroom[fm];
    const double r = kWidth[fm] * (1.25 - bias) * getParam (pSat) * 2.0;
    const bool emph = getParam (pEmph) > 0.5f;
    const double emphDb = emph ? kEmphDb[sp] : 0.0;
    const double gapHz = kGapHz[sp] * (1.6 - getParam (pHf) * 1.0) * (1.15 - 0.3 * bias);
    const bool autoGain = getParam (pAuto) > 0.5f;
    const double out = dbToGain (getParam (pOut));
    const double wowAmt = getParam (pWow), flutAmt = getParam (pFlut);
    const bool modOn = wowAmt > 0.0 || flutAmt > 0.0;
    const double base = kBaseDelayMs * 0.001 * fs;
    const double hiss = getParam (pNoise) > -119.0f ? dbToGain (getParam (pNoise)) : 0.0;

    for (auto& c : ch)
    {
        c.pre.setCoeffs (BiquadCoeffs::highShelf (fs, 3000.0, 0.7, emphDb));
        c.de.setCoeffs (BiquadCoeffs::highShelf (fs, 3000.0, 0.7, -emphDb));
        c.bump.setCoeffs (BiquadCoeffs::peaking (fs, kBumpHz[sp], 1.2, 3.5 * getParam (pBump)));
        c.gap1.setCoeffs (BiquadCoeffs::lowPass (fs, std::min (gapHz, fs * 0.45), 0.6));
        c.gap2.setCoeffs (BiquadCoeffs::lowPass (fs, std::min (gapHz * 1.3, fs * 0.45), 0.55));
    }

    for (int start = 0; start < n; start += maxBlock)
    {
        const int len = std::min (maxBlock, n - start);
        for (int c = 0; c < numCh; ++c)
        {
            Chan& st = ch[(size_t) c];
            float* x = io[c] + start;
            for (int i = 0; i < len; ++i)
                tmp[(size_t) i] = (float) (st.pre.process (x[i]) * drive / head);

            double* u = st.os.upsample (tmp.data(), len);
            for (int j = 0; j < len * factor; ++j)
            {
                const double h = u[j];
                const double t = std::tanh (2.0 * h);
                const double rEff = r * t * t; // bias linearises small signals: hysteresis grows with level
                st.play = std::clamp (st.play, h - rEff, h + rEff);
                u[j] = (0.6 * std::tanh (h) + 0.4 * std::tanh (st.play)) * head;
            }
            st.os.downsample (tmp.data(), len);

            for (int i = 0; i < len; ++i)
            {
                double y = tmp[(size_t) i];
                if (autoGain) y /= drive;
                y = st.de.process (y);
                y = st.bump.process (y);
                y = st.gap2.process (st.gap1.process (y));
                x[i] = (float) y;
            }
        }

        // wow & flutter (shared modulation, both channels) + hiss
        for (int i = 0; i < len; ++i)
        {
            double mod = 0.0;
            if (modOn)
            {
                driftPhase = wowSmooth.process (noise.next());
                mod = wowAmt * (0.001 * fs) * (wow.next() + 2.0 * driftPhase) * 0.75
                    + flutAmt * (0.00012 * fs) * (0.7 * flutter.next() + 0.3 * flutter2.next());
            }
            for (int c = 0; c < numCh; ++c)
            {
                Chan& st = ch[(size_t) c];
                float* x = io[c] + start;
                double y = x[i];
                if (modOn)
                {
                    st.wf.push ((float) y);
                    y = st.wf.read (base + mod);
                }
                if (hiss > 0.0)
                    y += st.hissLp.process (noise.next()) * hiss * 2.0;
                y = st.dc.process (y);
                x[i] = (float) (y * out);
            }
        }
    }
}

// =============================================================== TRANSFORMER

namespace xfmr
{
    enum P { pProfile, pDrive, pCore, pLf, pResHz, pResDb, pHfLp, pOs, pAuto, pOut };
    constexpr double kSatLevel[] = { 0.6, 0.35, 0.2 };
    constexpr double kAsym[] = { 0.0, 0.03, 0.08 };
}

TransformerModule::TransformerModule()
{
    specs = {
        ParamSpec::choice ("profile", "Core", { "Nickel (clean)", "Steel (balanced)", "Iron (vintage)" }, 1,
            "Core material: nickel saturates late and cleanly, iron early with more asymmetry.", true, "Core"),
        ParamSpec::num ("drive_db", "Drive", "dB", -12, 24, 6, 0, "Level into the transformer.", true, "Core"),
        ParamSpec::num ("core_sat", "Core saturation", "", 0, 1, 0.5f, 0,
            "How easily the core saturates. Affects bass first, like a real transformer.", false, "Core"),
        ParamSpec::num ("lf_corner_hz", "LF corner", "Hz", 5, 80, 15, 20, "Low-frequency limit of the transformer.", false, "Response"),
        ParamSpec::num ("hf_res_hz", "HF resonance", "Hz", 8000, 40000, 28000, 18000, "Frequency of the leakage-inductance resonance.", false, "Response"),
        ParamSpec::num ("hf_res_db", "HF resonance gain", "dB", 0, 6, 1.5f, 0, "Height of the HF resonance.", false, "Response"),
        ParamSpec::num ("hf_lp_hz", "HF roll-off", "Hz", 8000, 40000, 32000, 18000, "High-frequency roll-off above the resonance.", false, "Response"),
        ParamSpec::choice ("oversampling", "Oversampling", { "1x", "2x", "4x", "8x" }, 1, "Anti-aliasing.", false, "Core"),
        ParamSpec::toggle ("auto_gain", "Auto gain", true, "Compensates Drive at the output.", false, "Core"),
        ParamSpec::num ("output_db", "Output", "dB", -24, 12, 0, 0, "Output level.", true, "Core"),
    };
    finaliseParams();
}

std::string TransformerModule::topology() const
{
    return "Flux-domain core saturation: flux phi = one-pole low-pass of the drive-scaled input at the LF corner "
           "(approximates the integral of voltage above the corner). phi_s = L*tanh(phi/L + a) - L*tanh(a), "
           "L = core level(profile) x (1.05 - core_sat), a = asymmetry(profile). The flux error e = phi_s - phi is differentiated "
           "back (e[n] - c*e[n-1])/(1-c) and added to the signal, so distortion rises as frequency falls (12 dB/oct below ~LF corner x 10). "
           "Followed by 2nd-order HPF at the LF corner (Q 0.6), peaking resonance at HF resonance (Q 1.4) and 2nd-order low-pass at HF roll-off, "
           "all inside the oversampler; 8 Hz DC blocker.";
}

void TransformerModule::prepare (double sampleRate, int maxBlockSize, int)
{
    fs = sampleRate;
    maxBlock = std::max (1, maxBlockSize);
    factor = kFactors[(int) getParam (xfmr::pOs)];
    for (auto& c : ch)
    {
        c.os.prepare (maxBlock);
        c.os.setFactor (factor);
        c.dc.prepare (fs, 8.0);
    }
    reset();
}

void TransformerModule::reset()
{
    for (auto& c : ch)
    {
        c.os.reset();
        c.flux = c.errPrev = c.hyst = 0.0;
        c.lfCut.reset(); c.hfRes.reset(); c.hfLp.reset();
        c.dc.reset();
    }
}

int TransformerModule::latencySamples() const
{
    return kFactors[(int) getParam (xfmr::pOs)] > 1 ? Oversampler::kLatency : 0;
}

void TransformerModule::process (float* const* io, int numCh, int n)
{
    using namespace xfmr;
    numCh = std::min (numCh, 2);
    const int want = kFactors[(int) getParam (pOs)];
    if (want != factor)
    {
        factor = want;
        for (auto& c : ch) c.os.setFactor (factor);
    }
    const int prof = (int) getParam (pProfile);
    const double drive = dbToGain (getParam (pDrive));
    const double L = kSatLevel[prof] * (1.05 - getParam (pCore));
    const double asym = kAsym[prof];
    const double osRate = fs * factor;
    const double a = std::exp (-kTwoPi * getParam (pLf) / osRate);
    const bool autoGain = getParam (pAuto) > 0.5f;
    const double out = dbToGain (getParam (pOut));
    const double off = L * std::tanh (asym);

    for (auto& c : ch)
    {
        c.lfCut.setCoeffs (BiquadCoeffs::highPass (osRate, getParam (pLf), 0.6));
        c.hfRes.setCoeffs (BiquadCoeffs::peaking (osRate, std::min ((double) getParam (pResHz), osRate * 0.45), 1.4, getParam (pResDb)));
        c.hfLp.setCoeffs (BiquadCoeffs::lowPass (osRate, std::min ((double) getParam (pHfLp), osRate * 0.45), 0.707));
    }

    float scratch[256];
    for (int c = 0; c < numCh; ++c)
    {
        Chan& st = ch[(size_t) c];
        const int chunk = std::min (256, maxBlock);
        for (int start = 0; start < n; start += chunk)
        {
            const int len = std::min (chunk, n - start);
            float* x = io[c] + start;
            for (int i = 0; i < len; ++i)
                scratch[i] = (float) (x[i] * drive);
            double* u = st.os.upsample (scratch, len);
            for (int j = 0; j < len * factor; ++j)
            {
                const double v = u[j];
                st.flux = flushDenormal (v + a * (st.flux - v));
                const double phiS = L * std::tanh (st.flux / L + asym) - off;
                const double e = phiS - st.flux;
                double y = v + (e - a * st.errPrev) / (1.0 - a);
                st.errPrev = e;
                y = st.lfCut.process (y);
                y = st.hfRes.process (y);
                y = st.hfLp.process (y);
                u[j] = y;
            }
            st.os.downsample (scratch, len);
            for (int i = 0; i < len; ++i)
            {
                double y = scratch[i];
                if (autoGain) y /= drive;
                x[i] = (float) (st.dc.process (y) * out);
            }
        }
    }
}

// =============================================================== CONSOLE

namespace console
{
    enum P { pModel, pDrive, pXtalk, pNoise, pTol, pSeed, pOut };
}

ConsoleModule::ConsoleModule()
{
    specs = {
        ParamSpec::choice ("model", "Voicing", { "Clean", "British", "American" }, 1,
            "Channel voicing: British = silky top and firm low end; American = forward mids and punch.", true, "Channel"),
        ParamSpec::num ("drive_db", "Drive", "dB", -12, 24, 0, 0, "Level into the channel amplifiers.", true, "Channel"),
        ParamSpec::num ("crosstalk_db", "Crosstalk", "dB", -100, -30, -70, -60,
            "Leakage between left and right (rises with frequency, like capacitive coupling).", false, "Imperfections"),
        ParamSpec::num ("noise_db", "Noise", "dB", -120, -60, -120, -90, "Noise floor. -120 = off.", false, "Imperfections"),
        ParamSpec::num ("tolerance", "Component tolerance", "", 0, 1, 0.3f, 0,
            "Small random differences between the left and right channel, like real components.", false, "Imperfections"),
        ParamSpec::num ("seed", "Unit", "", 1, 99, 7, 0, "Which 'unit' the tolerances come from (reproducible).", false, "Imperfections"),
        ParamSpec::num ("output_db", "Output", "dB", -24, 12, 0, 0, "Output level.", true, "Channel"),
    };
    finaliseParams();
}

std::string ConsoleModule::topology() const
{
    return "Per channel: tolerance offsets (gain +-0.2 dB, shelf +-0.3 dB, HPF +-15 %, seeded) -> voicing EQ "
           "(British: +0.5 dB low shelf 80 Hz, -0.3 dB bell 400 Hz, +0.8 dB high shelf 12 kHz; American: +0.4 dB low shelf 100 Hz, "
           "+0.6 dB bell 1.5 kHz Q 0.7; Clean: flat) -> 2nd-order HPF ~15 Hz -> amplifier saturation y = tanh(0.5u + 0.04u^2)/0.5 "
           "with u = drive x input (gain-compensated) -> DC blocker -> crosstalk: other channel through a 1st-order HPF at 2 kHz at the "
           "crosstalk level -> noise floor.";
}

void ConsoleModule::design()
{
    using namespace console;
    const int model = (int) getParam (pModel);
    const double tol = getParam (pTol);
    Noise rng ((uint64_t) getParam (pSeed) * 7919u + 13u);
    for (auto& c : ch)
    {
        c.tolGain = dbToGain (0.2 * tol * rng.next());
        c.tolShelf = 0.3 * tol * rng.next();
        c.tolHp = 1.0 + 0.15 * tol * rng.next();
        const double loDb = (model == 1 ? 0.5 : model == 2 ? 0.4 : 0.0) + c.tolShelf;
        const double hiDb = (model == 1 ? 0.8 : 0.0) - c.tolShelf * 0.5;
        c.lo.setCoeffs (BiquadCoeffs::lowShelf (fs, model == 2 ? 100.0 : 80.0, 0.7, loDb));
        c.mid.setCoeffs (model == 1 ? BiquadCoeffs::peaking (fs, 400.0, 0.8, -0.3)
                       : model == 2 ? BiquadCoeffs::peaking (fs, 1500.0, 0.7, 0.6) : BiquadCoeffs::identity());
        c.hi.setCoeffs (BiquadCoeffs::highShelf (fs, 12000.0, 0.7, hiDb));
        c.hp.setCoeffs (BiquadCoeffs::highPass (fs, 15.0 * c.tolHp, 0.6));
        c.xtalk.setCoeffs (BiquadCoeffs::highPass1 (fs, 2000.0));
    }
    lastVersion = paramVersion();
}

void ConsoleModule::prepare (double sampleRate, int, int)
{
    fs = sampleRate;
    for (auto& c : ch)
        c.dc.prepare (fs, 5.0);
    lastVersion = ~0u;
    reset();
}

void ConsoleModule::reset()
{
    for (auto& c : ch)
    {
        c.lo.reset(); c.mid.reset(); c.hi.reset(); c.hp.reset(); c.xtalk.reset();
        c.dc.reset();
    }
}

void ConsoleModule::process (float* const* io, int numCh, int n)
{
    using namespace console;
    if (lastVersion != paramVersion())
        design();
    const double drive = dbToGain (getParam (pDrive));
    const double xt = dbToGain (getParam (pXtalk));
    const double noiseG = getParam (pNoise) > -119.0f ? dbToGain (getParam (pNoise)) : 0.0;
    const double out = dbToGain (getParam (pOut));

    for (int i = 0; i < n; ++i)
    {
        double y[2] = { 0.0, 0.0 };
        for (int c = 0; c < std::min (numCh, 2); ++c)
        {
            Chan& st = ch[(size_t) c];
            double v = io[c][i] * st.tolGain;
            v = st.hi.process (st.mid.process (st.lo.process (v)));
            v = st.hp.process (v);
            const double u = v * drive;
            v = std::tanh (0.5 * u + 0.04 * u * u) / 0.5 / drive;
            y[c] = st.dc.process (v);
        }
        if (numCh == 2)
        {
            const double l = y[0], r = y[1];
            y[0] = l + xt * ch[0].xtalk.process (r);
            y[1] = r + xt * ch[1].xtalk.process (l);
        }
        for (int c = 0; c < std::min (numCh, 2); ++c)
        {
            double v = y[c];
            if (noiseG > 0.0)
                v += noise.next() * noiseG * 1.7320508; // uniform -> RMS = level
            io[c][i] = (float) (v * out);
        }
    }
}

} // namespace sonic
