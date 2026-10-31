#include "sonic/EqMatch.h"
#include "sonic/Biquad.h"

#include <algorithm>
#include <cmath>

namespace sonic::eqmatch
{

double levelAt (const Spectrum& s, double f)
{
    if (s.empty()) return -200.0;
    const double k = std::pow (2.0, 1.0 / 6.0);   // +- 1/6 octave = 1/3 octave band
    const int a = std::max (1, (int) std::floor (f / k / s.binHz));
    const int b = std::min ((int) s.power.size() - 1, (int) std::ceil (f * k / s.binHz));
    double sum = 0.0;
    int n = 0;
    for (int i = a; i <= b; ++i) { sum += s.power[(size_t) i]; ++n; }
    return n > 0 ? 10.0 * std::log10 (sum / (n * (double) s.frames) + 1e-30) : -200.0;
}

Fit fit (const Spectrum& yours, const Spectrum& reference, double amount, int maxBands, double fs)
{
    Fit r;
    if (yours.empty() || reference.empty())
        return r;
    const int G = 240;
    r.gridHz.resize (G);
    std::vector<double> diff (G);
    double mean = 0.0;
    int meanN = 0;
    for (int i = 0; i < G; ++i)
    {
        const double f = 25.0 * std::pow (18000.0 / 25.0, (double) i / (G - 1));
        r.gridHz[(size_t) i] = f;
        const double a = levelAt (yours, f), b = levelAt (reference, f);
        diff[(size_t) i] = (a < -190.0 || b < -190.0) ? 0.0 : b - a;
        if (f > 100.0 && f < 10000.0 && a > -190.0 && b > -190.0) { mean += diff[(size_t) i]; ++meanN; }
    }
    if (meanN == 0)
        return r;
    mean /= meanN;
    for (auto& d : diff)
        d = std::clamp (d - mean, -12.0, 12.0) * std::clamp (amount, 0.0, 1.0);
    r.wantedDb = diff;

    std::vector<double> rest = diff, fitted ((size_t) G, 0.0);
    for (int it = 0; it < maxBands; ++it)
    {
        int peak = 0;
        for (int i = 0; i < G; ++i)
            if (std::abs (rest[(size_t) i]) > std::abs (rest[(size_t) peak])) peak = i;
        const double gain = rest[(size_t) peak];
        if (std::abs (gain) < 0.5)
            break;
        int lo = peak, hi = peak;
        while (lo > 0 && std::abs (rest[(size_t) lo]) > std::abs (gain) * 0.5 && rest[(size_t) lo] * gain > 0) --lo;
        while (hi < G - 1 && std::abs (rest[(size_t) hi]) > std::abs (gain) * 0.5 && rest[(size_t) hi] * gain > 0) ++hi;
        const double bw = std::max (0.25, std::log2 (r.gridHz[(size_t) hi] / r.gridHz[(size_t) lo]));
        const double q = std::clamp (std::sqrt (std::pow (2.0, bw)) / (std::pow (2.0, bw) - 1.0), 0.3, 6.0);
        Band b { r.gridHz[(size_t) peak], gain, q };
        const auto c = BiquadCoeffs::peaking (fs, b.freq, b.q, b.gainDb);
        for (int i = 0; i < G; ++i)
        {
            const double d = 20.0 * std::log10 (c.magnitudeAt (fs, r.gridHz[(size_t) i]));
            rest[(size_t) i] -= d;
            fitted[(size_t) i] += d;
        }
        r.bands.push_back (b);
    }
    r.fittedDb = fitted;
    for (int i = 0; i < G; ++i)
        r.largestRemainingDb = std::max (r.largestRemainingDb, std::abs (rest[(size_t) i]));
    return r;
}

} // namespace sonic::eqmatch
