#include "sonic/TransferAnalyzer.h"
#include "sonic/Analysis.h"
#include "sonic/DspUtils.h"

#include <algorithm>
#include <cmath>

namespace sonic
{

void TransferAnalyzer::prepare (double sampleRate, int segmentSize, double averagingFrames)
{
    fs = sampleRate;
    n = segmentSize;
    decay = averagingFrames > 0.0 ? std::exp (-1.0 / averagingFrames) : 0.0;
    window = makeWindow (WindowType::hann, n);
    bx.assign ((size_t) n, {});
    by.assign ((size_t) n, {});
    reset();
}

void TransferAnalyzer::reset()
{
    const size_t bins = (size_t) n / 2 + 1;
    sxx.assign (bins, 0.0);
    syy.assign (bins, 0.0);
    sxy.assign (bins, {});
    effectiveSegments = 0.0;
}

void TransferAnalyzer::addBlock (const float* x, const float* y, int numSamples)
{
    const size_t bins = (size_t) n / 2 + 1;
    std::vector<double> ax (bins, 0.0), ay (bins, 0.0);
    std::vector<std::complex<double>> axy (bins);
    int segs = 0;

    for (int start = 0; start + n <= numSamples; start += n / 2) // 50 % overlap
    {
        for (int i = 0; i < n; ++i)
        {
            bx[(size_t) i] = { x[start + i] * window[(size_t) i], 0.0 };
            by[(size_t) i] = { y[start + i] * window[(size_t) i], 0.0 };
        }
        fft (bx);
        fft (by);
        for (size_t k = 0; k < bins; ++k)
        {
            ax[k] += std::norm (bx[k]);
            ay[k] += std::norm (by[k]);
            axy[k] += std::conj (bx[k]) * by[k];
        }
        ++segs;
    }
    if (segs == 0)
        return;

    for (size_t k = 0; k < bins; ++k)
    {
        sxx[k] = decay * sxx[k] + ax[k];
        syy[k] = decay * syy[k] + ay[k];
        sxy[k] = decay * sxy[k] + axy[k];
    }
    effectiveSegments = decay * effectiveSegments + segs;
}

TransferAnalyzer::Result TransferAnalyzer::result() const
{
    Result r;
    r.sampleRate = fs;
    r.segmentSize = n;
    r.binHz = fs / n;
    r.segmentsAveraged = (int) std::lround (effectiveSegments);

    const size_t bins = (size_t) n / 2 + 1;
    if (effectiveSegments < 1.0)
        return r;

    // Energy threshold: bins with input power 70 dB below the strongest bin are unusable.
    double maxSxx = 0.0;
    for (size_t k = 1; k < bins; ++k)
        maxSxx = std::max (maxSxx, sxx[k]);
    if (maxSxx <= 0.0)
        return r;
    const double thresh = maxSxx * 1e-7;

    // Bulk delay via PHAT-weighted cross-correlation.
    std::vector<std::complex<double>> g ((size_t) n);
    for (size_t k = 0; k < bins; ++k)
    {
        const double m = std::abs (sxy[k]);
        const auto v = (sxx[k] > thresh && m > 0.0) ? sxy[k] / m : std::complex<double> (0.0, 0.0);
        g[k] = v;
        if (k > 0 && k < bins - 1)
            g[(size_t) n - k] = std::conj (v);
    }
    // Inverse FFT via conjugation
    for (auto& v : g) v = std::conj (v);
    fft (g);
    int best = 0;
    double bestVal = -1.0;
    for (int i = 0; i < n; ++i)
    {
        const double v = g[(size_t) i].real();
        if (v > bestVal)
        {
            bestVal = v;
            best = i;
        }
    }
    r.estimatedDelaySamples = best <= n / 2 ? best : best - n; // negative = y leads x

    r.magnitudeDb.resize (bins);
    r.phaseRad.resize (bins);
    r.coherence.resize (bins);
    r.usable.resize (bins);
    double cohSum = 0.0;
    int cohCount = 0;
    for (size_t k = 0; k < bins; ++k)
    {
        const bool ok = sxx[k] > thresh && syy[k] > 0.0;
        r.usable[k] = ok;
        if (! ok)
        {
            r.magnitudeDb[k] = -200.0;
            r.phaseRad[k] = 0.0;
            r.coherence[k] = 0.0;
            continue;
        }
        const auto h = sxy[k] / sxx[k];
        r.magnitudeDb[k] = gainToDb (std::abs (h), -200.0);
        // remove bulk delay: multiply by e^{+j w d}
        const double w = kTwoPi * (double) k / n;
        const auto hd = h * std::polar (1.0, w * r.estimatedDelaySamples);
        r.phaseRad[k] = std::arg (hd);
        r.coherence[k] = std::clamp (std::norm (sxy[k]) / (sxx[k] * syy[k]), 0.0, 1.0);

        const double f = k * r.binHz;
        if (f >= 20.0 && f <= 20000.0)
        {
            cohSum += r.coherence[k];
            ++cohCount;
        }
    }
    r.meanCoherence = cohCount > 0 ? cohSum / cohCount : 0.0;
    r.valid = cohCount > 0;
    return r;
}

} // namespace sonic
