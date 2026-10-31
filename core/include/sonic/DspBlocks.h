#pragma once
// Small reusable DSP building blocks shared by the modules.

#include "sonic/Biquad.h"
#include "sonic/DspUtils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sonic
{

// One-pole low-pass (6 dB/oct), matched pole.
struct OnePoleLP
{
    double a = 0.0, z = 0.0;
    void setCutoff (double fs, double hz) { a = std::exp (-kTwoPi * std::clamp (hz, 1.0, fs * 0.49) / fs); }
    void reset (double v = 0.0) { z = v; }
    double process (double x) { z = x + a * (z - x); return z = flushDenormal (z); }
};

// Envelope follower with separate attack/release (time constants in seconds).
struct EnvFollower
{
    double att = 0.0, rel = 0.0, env = 0.0;
    void setTimes (double fs, double attackS, double releaseS)
    {
        att = attackS > 0.0 ? std::exp (-1.0 / (attackS * fs)) : 0.0;
        rel = releaseS > 0.0 ? std::exp (-1.0 / (releaseS * fs)) : 0.0;
    }
    void reset() { env = 0.0; }
    double process (double x) // x >= 0
    {
        const double c = x > env ? att : rel;
        env = x + c * (env - x);
        return env = flushDenormal (env);
    }
};

// Topology-preserving-transform state-variable filter (Zavalishin). Stable under
// fast modulation; gives LP/BP/HP simultaneously.
struct Svf
{
    double g = 0.0, k = 1.0, a1 = 0.0, a2 = 0.0, a3 = 0.0;
    double ic1 = 0.0, ic2 = 0.0;
    double lp = 0.0, bp = 0.0, hp = 0.0;

    void set (double fs, double hz, double q)
    {
        g = std::tan (kPi * std::clamp (hz, 5.0, fs * 0.49) / fs);
        k = 1.0 / std::max (q, 0.05);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0.0; }
    void process (double v0)
    {
        const double v3 = v0 - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = flushDenormal (2.0 * v1 - ic1);
        ic2 = flushDenormal (2.0 * v2 - ic2);
        lp = v2;
        bp = v1;
        hp = v0 - k * v1 - v2;
    }
};

// Fractional delay line with 4-point Lagrange (cubic) interpolation.
class FracDelay
{
public:
    void allocate (int maxSamples)
    {
        int size = 1;
        while (size < maxSamples + 8)
            size <<= 1;
        buf.assign ((size_t) size, 0.0f);
        mask = size - 1;
        w = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    void push (float x)
    {
        buf[(size_t) w] = x;
        w = (w + 1) & mask;
    }
    // Delay measured from the most recently pushed sample (>= 1.0 recommended).
    float read (double delay) const
    {
        delay = std::clamp (delay, 1.0, (double) (mask - 4));
        const double pos = (double) w - 1.0 - delay;
        const int i = (int) std::floor (pos);
        const double f = pos - i;
        const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
        // Lagrange 3rd order around y1..y2
        const double c0 = y1;
        const double c1 = y2 - y0 / 3.0 - y1 / 2.0 - y3 / 6.0;
        const double c2 = (y0 + y2) / 2.0 - y1;
        const double c3 = (y3 - y0) / 6.0 + (y1 - y2) / 2.0;
        return (float) (((c3 * f + c2) * f + c1) * f + c0);
    }
    float readInt (int delay) const { return at (w - 1 - delay); }
    int capacity() const { return mask - 4; }

private:
    float at (int i) const { return buf[(size_t) (i & mask)]; }
    std::vector<float> buf;
    int mask = 0, w = 0;
};

// Deterministic white noise (xorshift), uniform -1..1.
struct Noise
{
    uint64_t s = 0x2545F4914F6CDD1Dull;
    explicit Noise (uint64_t seed = 0x2545F4914F6CDD1Dull) : s (seed ? seed : 1) {}
    double next()
    {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return (double) ((s * 0x2545F4914F6CDD1Dull) >> 11) * (2.0 / 9007199254740992.0) - 1.0;
    }
};

// Sine LFO with phase offset per channel.
struct Lfo
{
    double phase = 0.0, inc = 0.0;
    void set (double fs, double hz) { inc = hz / fs; }
    double next (double offset = 0.0)
    {
        const double v = std::sin (kTwoPi * (phase + offset));
        phase += inc;
        phase -= std::floor (phase);
        return v;
    }
};

// Chebyshev polynomials of the first kind: T_n(cos t) = cos(n t). Fills t[0..N].
inline void chebyshev (double x, double* t, int N)
{
    t[0] = 1.0;
    if (N >= 1) t[1] = x;
    for (int n = 2; n <= N; ++n)
        t[n] = 2.0 * x * t[n - 1] - t[n - 2];
}

// Butterworth section Qs for an order-N cascade (N even: N/2 biquads; odd adds a 1st-order section).
inline std::vector<double> butterworthQs (int order)
{
    std::vector<double> q;
    for (int k = 0; k < order / 2; ++k)
        q.push_back (1.0 / (2.0 * std::sin (kPi * (2.0 * k + 1.0) / (2.0 * order))));
    return q;
}

} // namespace sonic
