#include "sonic/modules/EffectModules.h"

#include <cmath>

namespace sonic
{

// =============================================================== DELAY

namespace dly
{
    enum P { pTime, pSync, pDiv, pFb, pLp, pHp, pPing, pOffset, pWow, pDrive, pWidth, pDuck };
    const char* kDivs[] = { "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2", "1/1" };
    constexpr double kBeats[] = { 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0, 4.0 };
}

DelayModule::DelayModule()
{
    using namespace dly;
    std::vector<std::string> divs (std::begin (kDivs), std::end (kDivs));
    specs = {
        ParamSpec::num ("time_ms", "Time", "ms", 1, 2000, 375, 300, "Delay time (when Sync is off).", true, "Time"),
        ParamSpec::toggle ("sync", "Sync", false, "Lock the delay time to the host tempo.", false, "Time"),
        ParamSpec::choice ("division", "Division", divs, 8, "Note value when Sync is on.", false, "Time"),
        ParamSpec::num ("feedback_pct", "Feedback", "%", 0, 110, 35, 0, "How much of the echo is fed back (above 100 % builds up, limited by the loop saturation).", true, "Loop"),
        ParamSpec::num ("lp_hz", "Loop low-pass", "Hz", 500, 20000, 9000, 4000, "Each repeat gets darker.", false, "Loop"),
        ParamSpec::num ("hp_hz", "Loop high-pass", "Hz", 20, 2000, 120, 200, "Each repeat gets thinner.", false, "Loop"),
        ParamSpec::toggle ("ping_pong", "Ping-pong", false, "Echoes alternate between left and right.", false, "Stereo"),
        ParamSpec::num ("offset_ms", "Stereo offset", "ms", -20, 20, 0, 0, "Right channel time offset (widens the echo).", false, "Stereo"),
        ParamSpec::num ("wow", "Wow", "", 0, 1, 0.1f, 0, "Slow tape-like pitch wobble in the repeats.", false, "Character"),
        ParamSpec::num ("drive", "Loop drive", "", 0, 1, 0.2f, 0, "Saturation inside the feedback loop.", false, "Character"),
        ParamSpec::num ("width", "Width", "", 0, 1, 1, 0, "Stereo width of the echoes.", false, "Stereo"),
        ParamSpec::num ("ducking", "Ducking", "", 0, 1, 0, 0, "Lowers the echoes while the input is loud.", false, "Character"),
    };
    finaliseParams();
}

std::string DelayModule::topology() const
{
    return "Two cubic-interpolated delay lines (max 2.5 s). Delay time (ms or tempo division) smoothed with a 50 ms one-pole to avoid zipper. "
           "Feedback path: 2nd-order HPF -> 2nd-order LPF -> tanh saturation (drive) -> x feedback; ping-pong crosses the feedback between "
           "channels with mono input into the left line. Wow: 0.4 Hz sine modulation up to 1 ms. Wet output: M/S width, optional ducking "
           "by the input envelope (wet x (1 - ducking x env)).";
}

double DelayModule::delayTimeMs() const
{
    using namespace dly;
    if (getParam (pSync) > 0.5f)
        return kBeats[(int) getParam (pDiv)] * 60000.0 / bpm;
    return getParam (pTime);
}

void DelayModule::prepare (double sampleRate, int, int)
{
    fs = sampleRate;
    for (auto& l : line)
        l.allocate ((int) (fs * 2.6));
    wobble.set (fs, 0.4);
    reset();
}

void DelayModule::reset()
{
    for (auto& l : line) l.clear();
    for (auto& f : lp) f.reset();
    for (auto& f : hp) f.reset();
    const double d = delayTimeMs() * 0.001 * fs;
    smoothedDelay = { d, d };
    duckEnv = 0.0;
}

void DelayModule::process (float* const* io, int numCh, int n)
{
    using namespace dly;
    const double fb = getParam (pFb) * 0.01;
    const bool ping = getParam (pPing) > 0.5f;
    const double drive = 1.0 + 4.0 * getParam (pDrive);
    const double wowDepth = getParam (pWow) * 0.001 * fs;
    const double width = getParam (pWidth);
    const double duck = getParam (pDuck);
    const double sm = std::exp (-1.0 / (0.05 * fs));
    const double target0 = delayTimeMs() * 0.001 * fs;
    const double target1 = std::max (1.0, target0 + getParam (pOffset) * 0.001 * fs);
    for (int c = 0; c < 2; ++c)
    {
        lp[(size_t) c].setCoeffs (BiquadCoeffs::lowPass (fs, getParam (pLp), 0.707));
        hp[(size_t) c].setCoeffs (BiquadCoeffs::highPass (fs, getParam (pHp), 0.707));
    }
    float* L = io[0];
    float* R = numCh > 1 ? io[1] : io[0];

    for (int i = 0; i < n; ++i)
    {
        smoothedDelay[0] = target0 + sm * (smoothedDelay[0] - target0);
        smoothedDelay[1] = target1 + sm * (smoothedDelay[1] - target1);
        const double w = wowDepth * wobble.next();
        const double yl = line[0].read (std::max (1.0, smoothedDelay[0] - 1.0 + w));
        const double yr = line[1].read (std::max (1.0, smoothedDelay[1] - 1.0 + w));

        auto loop = [&] (int c, double y)
        {
            double v = lp[(size_t) c].process (hp[(size_t) c].process (y));
            return std::tanh (v * drive) / drive;
        };
        const double fl = loop (0, yl), fr = loop (1, yr);
        const double inL = L[i], inR = R[i];
        if (ping)
        {
            line[0].push ((float) (0.5 * (inL + inR) + fb * fr));
            line[1].push ((float) (fb * fl));
        }
        else
        {
            line[0].push ((float) (inL + fb * fl));
            line[1].push ((float) (inR + fb * fr));
        }

        // wet output with width and ducking
        const double m = 0.5 * (yl + yr), s = 0.5 * (yl - yr) * width;
        const double a = std::abs (0.5 * (inL + inR));
        duckEnv = a + (a > duckEnv ? 0.99 : 0.9995) * (duckEnv - a);
        const double g = 1.0 - duck * std::min (1.0, duckEnv * 4.0);
        L[i] = (float) ((m + s) * g);
        if (numCh > 1) R[i] = (float) ((m - s) * g);
    }
}

// =============================================================== REVERB

namespace rvb
{
    enum P { pPre, pSize, pDecay, pDamp, pLowMult, pDiff, pModDepth, pModRate, pEarly, pWidth, pLowCut };
    constexpr double kLens[8] = { 1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116 };
    constexpr int kDiffLens[4] = { 142, 107, 379, 277 };
}

ReverbModule::ReverbModule()
{
    specs = {
        ParamSpec::num ("predelay_ms", "Pre-delay", "ms", 0, 250, 10, 40, "Gap before the reverb starts.", false, "Space"),
        ParamSpec::num ("size", "Size", "", 0, 1, 0.5f, 0, "Room size (spacing of reflections).", true, "Space"),
        ParamSpec::num ("decay_s", "Decay", "s", 0.1f, 20, 1.8f, 2, "Time for the tail to fall 60 dB (mid frequencies).", true, "Space"),
        ParamSpec::num ("damping_hz", "Damping", "Hz", 1000, 20000, 6000, 6000, "High frequencies above this die faster.", false, "Tone"),
        ParamSpec::num ("low_mult", "Low decay", "x", 0.5f, 2, 1, 1, "Decay multiplier below 250 Hz.", false, "Tone"),
        ParamSpec::num ("diffusion", "Diffusion", "", 0, 1, 0.7f, 0, "Density of the early build-up.", false, "Space"),
        ParamSpec::num ("mod_depth", "Modulation", "", 0, 1, 0.3f, 0, "Gentle pitch movement in the tail (smoother, less metallic).", false, "Tone"),
        ParamSpec::num ("mod_rate", "Mod rate", "Hz", 0.05f, 3, 0.6f, 0.6f, "Speed of the tail modulation.", false, "Tone"),
        ParamSpec::num ("early", "Early reflections", "", 0, 1, 0.3f, 0, "Level of the first discrete reflections.", false, "Space"),
        ParamSpec::num ("width", "Width", "", 0, 1, 1, 0, "Stereo width of the reverb.", false, "Space"),
        ParamSpec::num ("lowcut_hz", "Low cut", "Hz", 20, 1000, 80, 150, "Removes low end from the reverb input.", false, "Tone"),
    };
    finaliseParams();
}

std::string ReverbModule::topology() const
{
    return "Feedback delay network: mono input -> pre-delay -> 2nd-order low-cut -> 4 series Schroeder allpass diffusers "
           "(142/107/379/277 samples at 44.1 kHz, g = 0.7 x diffusion) -> 8 delay lines (1116..1617 samples at 44.1 kHz x (0.5 + size)), "
           "lines 0..3 modulated by sine LFOs (up to 12 samples). Feedback through an 8x8 normalised Hadamard matrix. Per line: "
           "two-band decay (split 250 Hz): g = 10^(-3 L / (fs T60)) with T60 x Low decay below the split, then 1-pole damping low-pass. "
           "Early reflections: 6 taps from the pre-delay line. Outputs: L = sum of even lines, R = sum of odd lines, M/S width.";
}

void ReverbModule::prepare (double sampleRate, int, int)
{
    fs = sampleRate;
    const double sc = fs / 44100.0;
    pre.allocate ((int) (fs * 0.3));
    for (int i = 0; i < 4; ++i)
    {
        diffLen[(size_t) i] = (int) (rvb::kDiffLens[i] * sc);
        diff[(size_t) i].allocate (diffLen[(size_t) i] + 4);
        mods[(size_t) i].set (fs, 0.6 * (1.0 + 0.13 * i));
        mods[(size_t) i].phase = 0.25 * i;
    }
    for (int i = 0; i < kLines; ++i)
    {
        baseLen[(size_t) i] = rvb::kLens[i] * sc;
        lines[(size_t) i].allocate ((int) (baseLen[(size_t) i] * 1.6) + 64);
    }
    reset();
}

void ReverbModule::reset()
{
    pre.clear();
    for (auto& d : diff) d.clear();
    for (auto& l : lines) l.clear();
    for (auto& d : damp) d.reset();
    for (auto& s : split) s.reset();
    inHp.reset();
}

void ReverbModule::process (float* const* io, int numCh, int n)
{
    using namespace rvb;
    const double size = 0.5 + getParam (pSize);
    const double t60 = getParam (pDecay);
    const double lowMult = getParam (pLowMult);
    const double g = 0.7 * getParam (pDiff);
    const double modDepth = getParam (pModDepth) * 12.0 * fs / 44100.0;
    const double width = getParam (pWidth);
    const double early = getParam (pEarly);
    const double preD = std::max (1.0, getParam (pPre) * 0.001 * fs);
    inHp.setCoeffs (BiquadCoeffs::highPass (fs, getParam (pLowCut), 0.707));

    double len[kLines], gHi[kLines], gLo[kLines];
    for (int i = 0; i < kLines; ++i)
    {
        len[i] = baseLen[(size_t) i] * size;
        gHi[i] = std::pow (10.0, -3.0 * len[i] / (fs * t60));
        gLo[i] = std::pow (10.0, -3.0 * len[i] / (fs * t60 * lowMult));
        damp[(size_t) i].setCutoff (fs, getParam (pDamp));
        split[(size_t) i].setCutoff (fs, 250.0);
    }
    for (int i = 0; i < 4; ++i)
        mods[(size_t) i].set (fs, getParam (pModRate) * (1.0 + 0.13 * i));

    float* L = io[0];
    float* R = numCh > 1 ? io[1] : io[0];
    const double norm = 1.0 / std::sqrt ((double) kLines);
    static constexpr double kEarlyMs[6] = { 7.1, 11.3, 17.9, 23.6, 31.2, 41.7 };

    for (int s = 0; s < n; ++s)
    {
        const double in = 0.5 * (L[s] + R[s]);
        pre.push ((float) in);
        double x = inHp.process (pre.read (preD));

        double er[2] = { 0.0, 0.0 };
        if (early > 0.0)
            for (int k = 0; k < 6; ++k)
                er[k & 1] += pre.read (preD + kEarlyMs[k] * 0.001 * fs * size) * (0.8 - 0.1 * k);

        // series allpass diffusion
        for (int k = 0; k < 4; ++k)
        {
            const double d = diff[(size_t) k].readInt (diffLen[(size_t) k] - 1);
            const double v = x + g * d;
            diff[(size_t) k].push ((float) v);
            x = d - g * v;
        }

        // FDN
        double y[kLines];
        for (int i = 0; i < kLines; ++i)
        {
            const double m = i < 4 ? modDepth * mods[(size_t) i].next() : 0.0;
            y[i] = lines[(size_t) i].read (len[i] + m);
        }
        // Hadamard 8x8 (fast Walsh-Hadamard)
        double h[kLines];
        for (int i = 0; i < kLines; ++i) h[i] = y[i];
        for (int step = 1; step < kLines; step <<= 1)
            for (int i = 0; i < kLines; i += step << 1)
                for (int j = i; j < i + step; ++j)
                {
                    const double a = h[j], b = h[j + step];
                    h[j] = a + b;
                    h[j + step] = a - b;
                }
        for (int i = 0; i < kLines; ++i)
        {
            double v = h[i] * norm;
            const double lo = split[(size_t) i].process (v);
            v = gLo[i] * lo + gHi[i] * (v - lo);
            v = damp[(size_t) i].process (v);
            lines[(size_t) i].push ((float) (v + x));
        }

        double outL = 0.0, outR = 0.0;
        for (int i = 0; i < kLines; i += 2)
        {
            outL += y[i];
            outR += y[i + 1];
        }
        outL = outL * 0.35 + er[0] * early;
        outR = outR * 0.35 + er[1] * early;
        const double mm = 0.5 * (outL + outR), ss = 0.5 * (outL - outR) * width;
        L[s] = (float) (mm + ss);
        if (numCh > 1) R[s] = (float) (mm - ss);
    }
}

// =============================================================== CHORUS

namespace chr { enum P { pVoices, pRate, pDepth, pDelay, pFb, pSpread, pTone }; }

ChorusModule::ChorusModule()
{
    specs = {
        ParamSpec::choice ("voices", "Voices", { "1", "2", "3", "4" }, 1, "Number of modulated copies per side.", false, "Voices"),
        ParamSpec::num ("rate_hz", "Rate", "Hz", 0.05f, 5, 0.8f, 0.8f, "Modulation speed.", true, "Modulation"),
        ParamSpec::num ("depth_ms", "Depth", "ms", 0, 10, 2.5f, 2.5f, "Modulation depth.", true, "Modulation"),
        ParamSpec::num ("delay_ms", "Delay", "ms", 1, 40, 12, 12, "Base delay of the voices.", false, "Voices"),
        ParamSpec::num ("feedback", "Feedback", "", -0.9f, 0.9f, 0, 0, "Feeds the voices back (metallic when high).", false, "Voices"),
        ParamSpec::num ("spread", "Spread", "", 0, 1, 1, 0, "Stereo spread of the voices.", false, "Voices"),
        ParamSpec::num ("tone_hz", "Tone", "Hz", 1000, 20000, 12000, 6000, "Low-pass on the wet voices.", false, "Voices"),
    };
    finaliseParams();
}

std::string ChorusModule::topology() const
{
    return "Per channel: cubic-interpolated delay line; 1..4 voices read at base delay + depth x sine LFO (voices spaced 1/N in phase, "
           "right channel offset by spread x 1/4 cycle); voices averaged, optional feedback of the voice sum, 1-pole tone low-pass.";
}

void ChorusModule::prepare (double sampleRate, int, int)
{
    fs = sampleRate;
    for (auto& l : line) l.allocate ((int) (fs * 0.06));
    reset();
}

void ChorusModule::reset()
{
    for (auto& l : line) l.clear();
    fb = { 0.0, 0.0 };
    toneZ = { 0.0, 0.0 };
    for (auto& l : lfo) l.phase = 0.0;
}

void ChorusModule::process (float* const* io, int numCh, int n)
{
    using namespace chr;
    const int voices = (int) getParam (pVoices) + 1;
    const double depth = getParam (pDepth) * 0.001 * fs;
    const double base = getParam (pDelay) * 0.001 * fs + depth + 2.0;
    const double feedback = getParam (pFb);
    const double spread = getParam (pSpread);
    lfo[0].set (fs, getParam (pRate));
    const double toneA = std::exp (-kTwoPi * getParam (pTone) / fs);

    for (int i = 0; i < n; ++i)
    {
        const double ph = lfo[0].phase;
        lfo[0].next();
        for (int c = 0; c < std::min (numCh, 2); ++c)
        {
            const double x = io[c][i];
            line[(size_t) c].push ((float) (x + feedback * fb[(size_t) c]));
            double sum = 0.0;
            for (int v = 0; v < voices; ++v)
            {
                const double p = ph + (double) v / voices + (c == 1 ? 0.25 * spread : 0.0);
                sum += line[(size_t) c].read (base + depth * std::sin (kTwoPi * p));
            }
            sum /= voices;
            fb[(size_t) c] = sum;
            toneZ[(size_t) c] = sum + toneA * (toneZ[(size_t) c] - sum);
            io[c][i] = (float) toneZ[(size_t) c];
        }
    }
}

// =============================================================== FLANGER

namespace flg { enum P { pRate, pDepth, pDelay, pFb, pPhase }; }

FlangerModule::FlangerModule()
{
    specs = {
        ParamSpec::num ("rate_hz", "Rate", "Hz", 0.02f, 5, 0.25f, 0.5f, "Sweep speed.", true, "Sweep"),
        ParamSpec::num ("depth", "Depth", "", 0, 1, 0.7f, 0, "Sweep range.", true, "Sweep"),
        ParamSpec::num ("delay_ms", "Delay", "ms", 0.1f, 10, 2.0f, 2, "Centre of the sweep.", false, "Sweep"),
        ParamSpec::num ("feedback_pct", "Feedback", "%", -95, 95, 50, 0, "Resonance of the comb (negative = hollow).", false, "Comb"),
        ParamSpec::num ("stereo_deg", "Stereo phase", "deg", 0, 180, 90, 0, "LFO phase offset of the right channel.", false, "Sweep"),
    };
    finaliseParams();
}

std::string FlangerModule::topology() const
{
    return "Feedback comb: y = x + d, d = delay(x + fb x d) with delay = centre x (1 + depth x 0.95 x sine LFO), "
           "cubic interpolation; right channel LFO offset by Stereo phase; output scaled by 0.5.";
}

void FlangerModule::prepare (double sampleRate, int, int)
{
    fs = sampleRate;
    for (auto& l : line) l.allocate ((int) (fs * 0.03));
    reset();
}

void FlangerModule::reset()
{
    for (auto& l : line) l.clear();
    fb = { 0.0, 0.0 };
    lfo.phase = 0.0;
}

void FlangerModule::process (float* const* io, int numCh, int n)
{
    using namespace flg;
    lfo.set (fs, getParam (pRate));
    const double centre = getParam (pDelay) * 0.001 * fs;
    const double depth = getParam (pDepth) * 0.95;
    const double feedback = getParam (pFb) * 0.01;
    const double off = getParam (pPhase) / 360.0;
    for (int i = 0; i < n; ++i)
    {
        const double ph = lfo.phase;
        lfo.next();
        for (int c = 0; c < std::min (numCh, 2); ++c)
        {
            const double d = std::max (1.0, centre * (1.0 + depth * std::sin (kTwoPi * (ph + (c == 1 ? off : 0.0)))));
            const double x = io[c][i];
            const double y = line[(size_t) c].read (d);
            line[(size_t) c].push ((float) (x + feedback * y));
            io[c][i] = (float) (0.5 * (x + y));
        }
    }
}

// =============================================================== PHASER

namespace phs { enum P { pStages, pRate, pDepth, pCentre, pRange, pFb, pPhase }; constexpr int kStages[] = { 2, 4, 6, 8, 10, 12 }; }

PhaserModule::PhaserModule()
{
    specs = {
        ParamSpec::choice ("stages", "Stages", { "2", "4", "6", "8", "10", "12" }, 1, "Number of allpass stages (notches = stages / 2).", false, "Sweep"),
        ParamSpec::num ("rate_hz", "Rate", "Hz", 0.02f, 8, 0.4f, 0.5f, "Sweep speed.", true, "Sweep"),
        ParamSpec::num ("depth", "Depth", "", 0, 1, 0.8f, 0, "Sweep range.", true, "Sweep"),
        ParamSpec::num ("centre_hz", "Centre", "Hz", 100, 5000, 800, 800, "Centre frequency of the sweep.", false, "Sweep"),
        ParamSpec::num ("range_oct", "Range", "oct", 0, 6, 3, 0, "Sweep width in octaves.", false, "Sweep"),
        ParamSpec::num ("feedback_pct", "Feedback", "%", -95, 95, 40, 0, "Resonance of the notches.", false, "Sweep"),
        ParamSpec::num ("stereo_deg", "Stereo phase", "deg", 0, 180, 90, 0, "LFO phase offset of the right channel.", false, "Sweep"),
    };
    finaliseParams();
}

std::string PhaserModule::topology() const
{
    return "Cascade of first-order allpass sections (2..12) with coefficient a = (tan(pi f/fs) - 1)/(tan(pi f/fs) + 1), "
           "f = centre x 2^(range/2 x depth x LFO); feedback from the cascade output to its input; output = 0.5 (x + allpass).";
}

void PhaserModule::prepare (double sampleRate, int, int) { fs = sampleRate; reset(); }

void PhaserModule::reset()
{
    for (auto& c : z) c.fill (0.0);
    fb = { 0.0, 0.0 };
    lfo.phase = 0.0;
}

void PhaserModule::process (float* const* io, int numCh, int n)
{
    using namespace phs;
    lfo.set (fs, getParam (pRate));
    const int stages = kStages[(int) getParam (pStages)];
    const double centre = getParam (pCentre);
    const double range = getParam (pRange) * 0.5 * getParam (pDepth);
    const double feedback = getParam (pFb) * 0.01;
    const double off = getParam (pPhase) / 360.0;
    for (int i = 0; i < n; ++i)
    {
        const double ph = lfo.phase;
        lfo.next();
        for (int c = 0; c < std::min (numCh, 2); ++c)
        {
            const double f = std::clamp (centre * std::pow (2.0, range * std::sin (kTwoPi * (ph + (c == 1 ? off : 0.0)))), 20.0, fs * 0.45);
            const double t = std::tan (kPi * f / fs);
            const double a = (t - 1.0) / (t + 1.0);
            const double x = io[c][i];
            double v = x + feedback * fb[(size_t) c];
            for (int s = 0; s < stages; ++s)
            {
                double& zz = z[(size_t) c][(size_t) s];
                const double y = a * v + zz;
                zz = flushDenormal (v - a * y);
                v = y;
            }
            fb[(size_t) c] = v;
            io[c][i] = (float) (0.5 * (x + v));
        }
    }
}

// =============================================================== EXCITER

namespace exc { enum P { pFreq, pDrive, pChar, pAmount, pOut }; }

ExciterModule::ExciterModule()
{
    specs = {
        ParamSpec::num ("freq_hz", "Frequency", "Hz", 1000, 16000, 3500, 4000, "Only content above this generates new harmonics.", true, "Exciter"),
        ParamSpec::num ("drive_db", "Drive", "dB", 0, 24, 6, 0, "How strongly the high band is driven.", false, "Exciter"),
        ParamSpec::choice ("character", "Character", { "Even (sweet)", "Odd (crisp)", "Mixed" }, 2, "Type of added harmonics.", false, "Exciter"),
        ParamSpec::num ("amount_db", "Amount", "dB", -40, 0, -12, -15, "Level of the added harmonics.", true, "Exciter"),
        ParamSpec::num ("output_db", "Output", "dB", -24, 12, 0, 0, "Output level.", false, "Exciter"),
    };
    finaliseParams();
}

std::string ExciterModule::topology() const
{
    return "High band = 4th-order Butterworth high-pass at Frequency. Generator: u = drive x band; even: 0.5 u^2 (square law), "
           "odd: tanh(u) - u, mixed: both at half level; generated harmonics / drive -> 2nd-order HPF at Frequency (removes low "
           "intermodulation) -> added to the dry signal at Amount.";
}

void ExciterModule::prepare (double sampleRate, int, int) { fs = sampleRate; reset(); }
void ExciterModule::reset() { for (auto& c : ch) { c.hp1.reset(); c.hp2.reset(); c.post.reset(); } }

void ExciterModule::process (float* const* io, int numCh, int n)
{
    using namespace exc;
    const double f = getParam (pFreq);
    const double drive = dbToGain (getParam (pDrive));
    const int character = (int) getParam (pChar);
    const double amount = dbToGain (getParam (pAmount));
    const double out = dbToGain (getParam (pOut));
    const auto qs = butterworthQs (4);
    for (auto& c : ch)
    {
        c.hp1.setCoeffs (BiquadCoeffs::highPass (fs, f, qs[0]));
        c.hp2.setCoeffs (BiquadCoeffs::highPass (fs, f, qs[1]));
        c.post.setCoeffs (BiquadCoeffs::highPass (fs, f, 0.7071));
    }
    for (int c = 0; c < std::min (numCh, 2); ++c)
    {
        Chan& st = ch[(size_t) c];
        for (int i = 0; i < n; ++i)
        {
            const double x = io[c][i];
            const double u = st.hp2.process (st.hp1.process (x)) * drive;
            double gen = 0.0;
            if (character == 0 || character == 2) gen += 0.5 * u * u * (character == 2 ? 0.5 : 1.0);
            if (character == 1 || character == 2) gen += (std::tanh (u) - u) * (character == 2 ? 0.5 : 1.0);
            gen = st.post.process (gen / drive);
            io[c][i] = (float) ((x + amount * gen) * out);
        }
    }
}

// =============================================================== TRANSIENT

namespace trn { enum P { pAttack, pSustain, pAttTime, pRelTime, pOut }; }

TransientModule::TransientModule()
{
    specs = {
        ParamSpec::num ("attack_pct", "Attack", "%", -100, 100, 0, 0, "Boost (+) or soften (-) the start of each hit.", true, "Shape"),
        ParamSpec::num ("sustain_pct", "Sustain", "%", -100, 100, 0, 0, "Boost (+) or shorten (-) the tail of each hit.", true, "Shape"),
        ParamSpec::num ("attack_time_ms", "Attack window", "ms", 0.5f, 50, 8, 8, "How long the attack portion lasts.", false, "Timing"),
        ParamSpec::num ("release_ms", "Release window", "ms", 20, 1000, 200, 200, "How long the sustain detector holds.", false, "Timing"),
        ParamSpec::num ("output_db", "Output", "dB", -24, 12, 0, 0, "Output level.", false, "Shape"),
    };
    finaliseParams();
}

std::string TransientModule::topology() const
{
    return "Level-independent transient designer. Stereo-linked detector (max |L|,|R|). Fast envelope: 0.1 ms attack, "
           "0.5 x release window. Slow envelope: attack window, release window. Differences in dB: d = 20log(fast/slow). "
           "Gain (dB) = (attack% x max(d,0) + sustain% x max(-d,0)) x 0.02 (100 % doubles the transient in dB), limited to +-18 dB, "
           "applied identically to both channels.";
}

void TransientModule::prepare (double sampleRate, int, int) { fs = sampleRate; reset(); }
void TransientModule::reset() { fast.reset(); slow.reset(); }

void TransientModule::process (float* const* io, int numCh, int n)
{
    using namespace trn;
    fast.setTimes (fs, 0.0001, getParam (pRelTime) * 0.001 * 0.5);
    slow.setTimes (fs, getParam (pAttTime) * 0.001, getParam (pRelTime) * 0.001);
    const double att = getParam (pAttack), sus = getParam (pSustain);
    const double out = dbToGain (getParam (pOut));
    for (int i = 0; i < n; ++i)
    {
        double a = std::abs (io[0][i]);
        if (numCh > 1) a = std::max (a, (double) std::abs (io[1][i]));
        const double ef = fast.process (a), es = slow.process (a);
        const double d = gainToDb (ef + 1e-9) - gainToDb (es + 1e-9);
        const double gDb = std::clamp ((att * std::max (d, 0.0) + sus * std::max (-d, 0.0)) * 0.02, -18.0, 18.0);
        const double g = dbToGain (gDb) * out;
        for (int c = 0; c < std::min (numCh, 2); ++c)
            io[c][i] = (float) (io[c][i] * g);
    }
}

// =============================================================== STEREO

namespace st { enum P { pWidth, pMid, pSide, pPan, pBassMono, pRotate, pInvL, pInvR, pSwap }; }

StereoModule::StereoModule()
{
    specs = {
        ParamSpec::num ("width_pct", "Width", "%", 0, 200, 100, 0, "0 % = mono, 100 % = unchanged, 200 % = extra wide.", true, "Image"),
        ParamSpec::num ("mid_db", "Mid", "dB", -24, 12, 0, 0, "Level of the centre (mid) signal.", false, "Image"),
        ParamSpec::num ("side_db", "Side", "dB", -24, 12, 0, 0, "Level of the sides.", false, "Image"),
        ParamSpec::num ("pan", "Balance", "", -1, 1, 0, 0, "Left / right balance (constant power).", true, "Image"),
        ParamSpec::num ("bass_mono_hz", "Bass mono", "Hz", 20, 500, 20, 120, "Makes everything below this mono. 20 Hz = off.", false, "Image"),
        ParamSpec::num ("rotate_deg", "Rotation", "deg", -45, 45, 0, 0, "Rotates the stereo image.", false, "Image"),
        ParamSpec::toggle ("invert_l", "Invert left", false, "Polarity of the left channel.", false, "Polarity"),
        ParamSpec::toggle ("invert_r", "Invert right", false, "Polarity of the right channel.", false, "Polarity"),
        ParamSpec::toggle ("swap", "Swap L/R", false, "Swaps left and right.", false, "Polarity"),
    };
    finaliseParams();
}

std::string StereoModule::topology() const
{
    return "Swap -> polarity -> M/S (M = (L+R)/2, S = (L-R)/2) -> S x width -> bass mono: S through a 4th-order Linkwitz-Riley high-pass "
           "-> mid/side gains -> back to L/R -> rotation (2-D rotation of the L/R vector) -> constant-power balance.";
}

void StereoModule::prepare (double sampleRate, int, int) { fs = sampleRate; reset(); }
void StereoModule::reset() { sideHp1.reset(); sideHp2.reset(); }

void StereoModule::process (float* const* io, int numCh, int n)
{
    using namespace st;
    if (numCh < 2)
        return;
    const double width = getParam (pWidth) * 0.01;
    const double gm = dbToGain (getParam (pMid)), gs = dbToGain (getParam (pSide));
    const double bassHz = getParam (pBassMono);
    const bool bassMono = bassHz > 21.0;
    sideHp1.setCoeffs (BiquadCoeffs::highPass (fs, std::max (bassHz, 21.0), 0.70710678));
    sideHp2.setCoeffs (BiquadCoeffs::highPass (fs, std::max (bassHz, 21.0), 0.70710678));
    const double rot = getParam (pRotate) * kPi / 180.0;
    const double cr = std::cos (rot), sr = std::sin (rot);
    const double pan = getParam (pPan);
    const double pl = std::cos ((pan + 1.0) * kPi / 4.0) * std::sqrt (2.0);
    const double pr = std::sin ((pan + 1.0) * kPi / 4.0) * std::sqrt (2.0);
    const bool swap = getParam (pSwap) > 0.5f;
    const double il = getParam (pInvL) > 0.5f ? -1.0 : 1.0, ir = getParam (pInvR) > 0.5f ? -1.0 : 1.0;
    for (int i = 0; i < n; ++i)
    {
        double l = io[0][i], r = io[1][i];
        if (swap) std::swap (l, r);
        l *= il; r *= ir;
        const double m = 0.5 * (l + r);
        double s = 0.5 * (l - r) * width;
        if (bassMono) s = sideHp2.process (sideHp1.process (s));
        l = m * gm + s * gs;
        r = m * gm - s * gs;
        const double lr = cr * l - sr * r, rr = sr * l + cr * r;
        io[0][i] = (float) (lr * pl);
        io[1][i] = (float) (rr * pr);
    }
}

// =============================================================== FILTER

namespace flt { enum P { pType, pSlope, pCutoff, pRes, pDrive, pLfoRate, pLfoDepth, pEnv, pOut }; }

FilterModule::FilterModule()
{
    specs = {
        ParamSpec::choice ("type", "Type", { "Low-pass", "High-pass", "Band-pass", "Notch" }, 0, "Filter response.", true, "Filter"),
        ParamSpec::choice ("slope", "Slope", { "12 dB/oct", "24 dB/oct" }, 0, "Steepness.", false, "Filter"),
        ParamSpec::num ("cutoff_hz", "Cutoff", "Hz", 20, 20000, 2000, 1000, "Cutoff frequency.", true, "Filter"),
        ParamSpec::num ("resonance", "Resonance", "", 0, 1, 0.2f, 0, "Peak at the cutoff.", true, "Filter"),
        ParamSpec::num ("drive_db", "Drive", "dB", 0, 24, 0, 0, "Saturation before the filter.", false, "Filter"),
        ParamSpec::num ("lfo_rate_hz", "LFO rate", "Hz", 0, 10, 0, 1, "Cutoff modulation speed (0 = off).", false, "Movement"),
        ParamSpec::num ("lfo_depth_oct", "LFO depth", "oct", 0, 4, 0, 0, "Cutoff modulation depth.", false, "Movement"),
        ParamSpec::num ("env_oct", "Envelope", "oct", -4, 4, 0, 0, "Cutoff follows the input level (auto-wah).", false, "Movement"),
        ParamSpec::num ("output_db", "Output", "dB", -24, 12, 0, 0, "Output level.", false, "Filter"),
    };
    finaliseParams();
}

std::string FilterModule::topology() const
{
    return "Topology-preserving-transform state-variable filter (Zavalishin), 1 or 2 cascaded stages, Q = 0.5 + 9.5 x resonance "
           "(24 dB: second stage Q 0.707). Pre-drive: tanh(u)/drive-normalised. Cutoff = base x 2^(LFO depth x sine + env x envelope), "
           "envelope follower 5 ms / 120 ms on max |L|,|R|, coefficients updated every 8 samples.";
}

void FilterModule::prepare (double sampleRate, int, int)
{
    fs = sampleRate;
    env.setTimes (fs, 0.005, 0.12);
    reset();
}

void FilterModule::reset()
{
    for (auto& stage : svf) for (auto& s : stage) s.reset();
    env.reset();
}

void FilterModule::process (float* const* io, int numCh, int n)
{
    using namespace flt;
    const int type = (int) getParam (pType);
    const bool two = getParam (pSlope) > 0.5f;
    const double base = getParam (pCutoff);
    const double q = 0.5 + 9.5 * getParam (pRes);
    const double drive = dbToGain (getParam (pDrive));
    lfo.set (fs, getParam (pLfoRate));
    const double lfoDepth = getParam (pLfoRate) > 0.0f ? getParam (pLfoDepth) : 0.0;
    const double envOct = getParam (pEnv);
    const double out = dbToGain (getParam (pOut));

    for (int i = 0; i < n; ++i)
    {
        double a = std::abs (io[0][i]);
        if (numCh > 1) a = std::max (a, (double) std::abs (io[1][i]));
        const double e = env.process (a);
        const double l = lfo.next();
        if ((i & 7) == 0)
        {
            const double f = base * std::pow (2.0, lfoDepth * l + envOct * std::min (1.0, e * 4.0));
            for (int c = 0; c < 2; ++c)
            {
                svf[0][(size_t) c].set (fs, f, q);
                svf[1][(size_t) c].set (fs, f, two ? 0.7071 : q);
            }
        }
        for (int c = 0; c < std::min (numCh, 2); ++c)
        {
            double v = io[c][i];
            if (drive > 1.0001) v = std::tanh (v * drive) / drive;
            for (int stg = 0; stg < (two ? 2 : 1); ++stg)
            {
                Svf& s = svf[(size_t) stg][(size_t) c];
                s.process (v);
                switch (type)
                {
                    case 0: v = s.lp; break;
                    case 1: v = s.hp; break;
                    case 2: v = s.bp * s.k; break;    // unity peak gain
                    default: v = s.lp + s.hp; break;  // notch
                }
            }
            io[c][i] = (float) (v * out);
        }
    }
}

// =============================================================== UTILITY

namespace gn { enum P { pGain, pInvL, pInvR, pMono }; }

GainModule::GainModule()
{
    specs = {
        ParamSpec::num ("gain_db", "Gain", "dB", -48, 24, 0, 0, "Level change.", true, "Utility"),
        ParamSpec::toggle ("invert_l", "Invert left", false, "Polarity of the left channel.", false, "Utility"),
        ParamSpec::toggle ("invert_r", "Invert right", false, "Polarity of the right channel.", false, "Utility"),
        ParamSpec::toggle ("mono", "Mono", false, "Sums to mono.", false, "Utility"),
    };
    finaliseParams();
}

std::string GainModule::topology() const { return "Gain, per-channel polarity and mono sum."; }
void GainModule::prepare (double, int, int) {}
void GainModule::reset() {}

void GainModule::process (float* const* io, int numCh, int n)
{
    using namespace gn;
    const float g = (float) dbToGain (getParam (pGain));
    const float il = getParam (pInvL) > 0.5f ? -g : g, ir = getParam (pInvR) > 0.5f ? -g : g;
    const bool mono = getParam (pMono) > 0.5f && numCh > 1;
    for (int i = 0; i < n; ++i)
    {
        float l = io[0][i] * il;
        float r = numCh > 1 ? io[1][i] * ir : l;
        if (mono) l = r = 0.5f * (l + r);
        io[0][i] = l;
        if (numCh > 1) io[1][i] = r;
    }
}

// =============================================================== PITCH

namespace pit { enum P { pSemi, pCents, pWindow, pSpread, pFb, pTone }; }

PitchModule::PitchModule()
{
    specs = {
        ParamSpec::num ("semitones", "Pitch", "st", -24, 24, 0, 0, "Transposition in semitones (12 = one octave up).", true, "Pitch"),
        ParamSpec::num ("cents", "Fine", "ct", -100, 100, 0, 0, "Fine tuning in cents (100 cents = 1 semitone).", true, "Pitch"),
        ParamSpec::num ("window_ms", "Grain", "ms", 10, 150, 50, 50,
            "Length of the overlapping grains. Short = tighter timing but buzzier; long = smoother but smeared.", false, "Character"),
        ParamSpec::num ("spread_cents", "Stereo detune", "ct", 0, 50, 0, 0,
            "Left goes down and right goes up by this amount: a wide doubler.", false, "Character"),
        ParamSpec::num ("feedback", "Feedback", "", 0, 0.9f, 0, 0,
            "Feeds the shifted signal back in: each repeat is shifted again (shimmer cascades).", false, "Character"),
        ParamSpec::num ("tone_hz", "Tone", "Hz", 1000, 20000, 20000, 6000, "Low-pass on the shifted signal.", false, "Character"),
    };
    finaliseParams();
}

std::string PitchModule::topology() const
{
    return "Time-domain pitch shifter (rotating-tap delay): per channel a cubic-interpolated delay line is read by two taps whose delay "
           "ramps at rate (1 - ratio) over a window W (Grain) and wraps; the taps are half a window apart and cross-faded with "
           "sin^2 weights that sum to 1. ratio = 2^((semitones + cents/100 -/+ spread/100) / 12) for L/R. Optional feedback of the "
           "shifted output into the line, then a 1-pole low-pass (Tone). No latency compensation: the effective delay is about W/2.";
}

void PitchModule::prepare (double sampleRate, int, int)
{
    fs = sampleRate;
    for (auto& l : line) l.allocate ((int) (fs * 0.32));
    reset();
}

void PitchModule::reset()
{
    for (auto& l : line) l.clear();
    phase = { 0.0, 0.0 };
    toneZ = { 0.0, 0.0 };
    fbZ = { 0.0, 0.0 };
}

int PitchModule::latencySamples() const
{
    const double W = std::max (16.0, getParam (pit::pWindow) * 0.001 * fs);
    return (int) std::lround (4.0 + 0.5 * W);
}

double PitchModule::ratio (int c) const
{
    using namespace pit;
    const double spread = getParam (pSpread) * (c == 0 ? -1.0 : 1.0);
    return std::pow (2.0, (getParam (pSemi) + (getParam (pCents) + spread) * 0.01) / 12.0);
}

void PitchModule::process (float* const* io, int numCh, int n)
{
    using namespace pit;
    const double W = std::max (16.0, getParam (pWindow) * 0.001 * fs);
    const double minD = 4.0;
    const double fb = getParam (pFb);
    const double toneA = getParam (pTone) >= 19999.0f ? 0.0 : std::exp (-kTwoPi * getParam (pTone) / fs);
    for (int c = 0; c < std::min (numCh, 2); ++c)
    {
        const double step = (1.0 - ratio (c)) / W;   // phase change per sample (delay grows when pitching down)
        auto& l = line[(size_t) c];
        double ph = phase[(size_t) c];
        for (int i = 0; i < n; ++i)
        {
            l.push ((float) (io[c][i] + fb * fbZ[(size_t) c]));
            double y = 0.0;
            for (int t = 0; t < 2; ++t)
            {
                double p = ph + 0.5 * t;
                p -= std::floor (p);
                const double w = std::sin (kPi * p);
                y += w * w * l.read (minD + p * W);
            }
            ph += step;
            ph -= std::floor (ph);
            fbZ[(size_t) c] = y;
            toneZ[(size_t) c] = y + toneA * (toneZ[(size_t) c] - y);
            io[c][i] = (float) toneZ[(size_t) c];
        }
        phase[(size_t) c] = ph;
    }
}

} // namespace sonic
