#include "sonic/Analysis.h"
#include "sonic/DspUtils.h"

#include <algorithm>
#include <cmath>

namespace sonic
{

const char* windowName (WindowType w)
{
    return w == WindowType::hann ? "Hann" : "Blackman-Harris (4-term)";
}

void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -kTwoPi / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

std::vector<double> makeWindow (WindowType type, int size)
{
    std::vector<double> w ((size_t) size);
    for (int i = 0; i < size; ++i)
    {
        const double x = kTwoPi * i / size; // periodic window
        if (type == WindowType::hann)
            w[(size_t) i] = 0.5 - 0.5 * std::cos (x);
        else
            w[(size_t) i] = 0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2 * x) - 0.01168 * std::cos (3 * x);
    }
    return w;
}

Spectrum computeSpectrum (const float* samples, int numSamples, int fftSize, double sampleRate, WindowType window)
{
    Spectrum s;
    s.sampleRate = sampleRate;
    s.fftSize = fftSize;
    s.window = window;

    const auto w = makeWindow (window, fftSize);
    double sumW = 0.0;
    for (double v : w)
    {
        sumW += v;
        s.windowSumSq += v * v;
    }

    std::vector<std::complex<double>> buf ((size_t) fftSize);
    const int offset = std::max (0, numSamples - fftSize);
    for (int i = 0; i < fftSize; ++i)
    {
        const int idx = offset + i;
        const double x = idx < numSamples ? samples[idx] : 0.0;
        buf[(size_t) i] = { x * w[(size_t) i], 0.0 };
    }
    fft (buf);

    const int half = fftSize / 2;
    s.amplitude.resize ((size_t) half + 1);
    s.power.resize ((size_t) half + 1);
    for (int k = 0; k <= half; ++k)
    {
        const double mag = std::abs (buf[(size_t) k]);
        s.power[(size_t) k] = mag * mag;
        s.amplitude[(size_t) k] = (k == 0 || k == half ? 1.0 : 2.0) * mag / sumW;
    }
    return s;
}

double Spectrum::toneAmplitude (double freq, int halfWidth) const
{
    const int centre = (int) std::lround (freq / binHz());
    const int lo = std::max (1, centre - halfWidth);
    const int hi = std::min ((int) power.size() - 1, centre + halfWidth);
    double sum = 0.0;
    for (int k = lo; k <= hi; ++k)
        sum += power[(size_t) k];
    return std::sqrt (4.0 * sum / ((double) fftSize * windowSumSq));
}

HarmonicResult analyseHarmonics (const Spectrum& spec, double f0, int maxHarmonics, double maxFreqHz)
{
    HarmonicResult r;
    r.fundamentalHz = f0;
    r.fftSize = spec.fftSize;
    r.sampleRate = spec.sampleRate;
    r.window = spec.window;

    const double nyq = spec.sampleRate * 0.5;
    const double limit = std::min (maxFreqHz, nyq * 0.98);
    const int hw = spec.window == WindowType::hann ? 3 : 5;

    if (f0 < spec.binHz() * (hw + 2))
    {
        r.invalidReason = "Fundamental too low for this FFT size (increase FFT size).";
        return r;
    }
    if (f0 > limit)
    {
        r.invalidReason = "Fundamental above analysis band.";
        return r;
    }

    const double a1 = spec.toneAmplitude (f0, hw);
    if (a1 < 1e-7)
    {
        r.invalidReason = "No signal at the fundamental frequency.";
        return r;
    }

    double sumSq = 0.0;
    for (int h = 1; h <= maxHarmonics; ++h)
    {
        const double fh = f0 * h;
        if (fh > limit)
            break;
        const double a = spec.toneAmplitude (fh, hw);
        r.levelDbfs.push_back (gainToDb (a, -200.0));
        r.levelDbc.push_back (gainToDb (a / a1, -200.0));
        if (h > 1)
            sumSq += a * a;
        r.numHarmonics = h;
    }

    r.thdRatio = std::sqrt (sumSq) / a1;
    r.thdPercent = r.thdRatio * 100.0;
    r.thdDb = gainToDb (r.thdRatio, -200.0);

    // THD+N: all energy from 20 Hz to the band limit except the fundamental lobe.
    const double scale = 4.0 / ((double) spec.fftSize * spec.windowSumSq);
    const int kLo = std::max (1, (int) std::ceil (20.0 / spec.binHz()));
    const int kHi = std::min ((int) spec.power.size() - 1, (int) (limit / spec.binHz()));
    const int fc = (int) std::lround (f0 / spec.binHz());
    double total = 0.0;
    std::vector<double> levels;
    for (int k = kLo; k <= kHi; ++k)
    {
        if (std::abs (k - fc) > hw)
            total += spec.power[(size_t) k];
        levels.push_back (spec.amplitude[(size_t) k]);
    }
    r.thdNRatio = std::sqrt (total * scale) / a1;
    r.thdNDb = gainToDb (r.thdNRatio, -200.0);

    if (! levels.empty())
    {
        std::nth_element (levels.begin(), levels.begin() + (long) levels.size() / 2, levels.end());
        r.noiseFloorDbfs = gainToDb (levels[levels.size() / 2], -200.0);
    }

    r.valid = true;
    return r;
}

} // namespace sonic
