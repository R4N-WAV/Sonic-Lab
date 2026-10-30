#include "sonic/Oversampler.h"
#include "sonic/DspUtils.h"

#include <algorithm>
#include <cmath>

namespace sonic
{

namespace
{
    double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        const double q = x * x / 4.0;
        for (int k = 1; k < 64; ++k)
        {
            term *= q / ((double) k * (double) k);
            sum += term;
            if (term < sum * 1e-16)
                break;
        }
        return sum;
    }

    std::vector<double> designKernel (int factor)
    {
        // L = 64*F + 1 taps -> total (up+down) delay of exactly 64 base samples.
        const int L = Oversampler::kTapsPerPhase * factor + 1;
        const double M = (L - 1) / 2.0;
        const double fc = 0.46 / factor; // cycles per oversampled sample
        const double beta = 9.0;
        const double i0b = besselI0 (beta);

        std::vector<double> h ((size_t) L);
        double sum = 0.0;
        for (int j = 0; j < L; ++j)
        {
            const double t = j - M;
            const double sinc = t == 0.0 ? 2.0 * fc : std::sin (kTwoPi * fc * t) / (kPi * t);
            const double r = t / M;
            const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
            h[(size_t) j] = sinc * w;
            sum += h[(size_t) j];
        }
        for (auto& v : h)
            v /= sum; // unity DC gain
        return h;
    }
}

void Oversampler::prepare (int maxBlockSize)
{
    kernels[0] = designKernel (2);
    kernels[1] = designKernel (4);
    kernels[2] = designKernel (8);

    const int maxL = (int) kernels[2].size();
    upHistory.assign ((size_t) (kTapsPerPhase + 2) * 2, 0.0);
    downHistory.assign ((size_t) maxL * 2, 0.0);
    osBuffer.assign ((size_t) std::max (1, maxBlockSize) * 8 + 8, 0.0);
    setFactor (osFactor);
}

void Oversampler::setFactor (int factor)
{
    osFactor = (factor == 2 || factor == 4 || factor == 8) ? factor : 1;
    taps = osFactor > 1 ? &kernels[(size_t) kernelIndex (osFactor)] : nullptr;
    reset();
}

const std::vector<double>& Oversampler::kernel (int factor) const
{
    return kernels[(size_t) kernelIndex (factor)];
}

void Oversampler::reset()
{
    std::fill (upHistory.begin(), upHistory.end(), 0.0);
    std::fill (downHistory.begin(), downHistory.end(), 0.0);
    upPos = downPos = 0;
    if (taps != nullptr)
    {
        upLen = (int) taps->size() / osFactor + 1; // max taps in any polyphase branch
        downLen = (int) taps->size();
    }
}

double* Oversampler::upsample (const float* in, int n)
{
    if (taps == nullptr)
    {
        for (int i = 0; i < n; ++i)
            osBuffer[(size_t) i] = in[i];
        return osBuffer.data();
    }

    const auto& h = *taps;
    const int L = (int) h.size();
    const double gain = (double) osFactor;

    for (int i = 0; i < n; ++i)
    {
        // newest sample at upPos (mirrored at upPos + upLen); older samples follow
        upPos = (upPos - 1 + upLen) % upLen;
        upHistory[(size_t) upPos] = upHistory[(size_t) (upPos + upLen)] = in[i];
        const double* x = &upHistory[(size_t) upPos];

        for (int p = 0; p < osFactor; ++p)
        {
            double acc = 0.0;
            int k = 0;
            for (int j = p; j < L; j += osFactor, ++k)
                acc += h[(size_t) j] * x[k];
            osBuffer[(size_t) (i * osFactor + p)] = acc * gain;
        }
    }
    return osBuffer.data();
}

void Oversampler::downsample (float* out, int n)
{
    if (taps == nullptr)
    {
        for (int i = 0; i < n; ++i)
            out[i] = (float) osBuffer[(size_t) i];
        return;
    }

    const auto& h = *taps;
    const int L = downLen;

    for (int i = 0; i < n; ++i)
    {
        double acc = 0.0;
        for (int p = 0; p < osFactor; ++p)
        {
            downPos = (downPos - 1 + L) % L;
            downHistory[(size_t) downPos] = downHistory[(size_t) (downPos + L)] = osBuffer[(size_t) (i * osFactor + p)];

            if (p == 0)
            {
                // Decimate at phase 0: out[n] = sum_j h[j] * y[nF - j]
                const double* y = &downHistory[(size_t) downPos];
                for (int j = 0; j < L; ++j)
                    acc += h[(size_t) j] * y[j];
            }
        }
        out[i] = (float) acc;
    }
}

} // namespace sonic
