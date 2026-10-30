#pragma once
// Linear-phase polyphase FIR oversampler (1x / 2x / 4x / 8x).
// Kaiser-windowed sinc (beta 9, ~-90 dB stopband), 64 taps per polyphase
// branch. Cutoff is placed at 0.46 * base sample rate: at 48 kHz the
// passband is flat to ~20 kHz; at 44.1 kHz it starts rolling off ~18 kHz.
// Latency is exactly kLatency base-rate samples for every factor > 1.

#include <array>
#include <vector>

namespace sonic
{

class Oversampler
{
public:
    static constexpr int kTapsPerPhase = 64;
    static constexpr int kLatency = kTapsPerPhase; // base-rate samples (up + down)

    // Allocates kernels for all factors and buffers for 8x. Not real-time safe.
    void prepare (int maxBlockSize);
    // Switch factor (1, 2, 4 or 8). Real-time safe (no allocation); resets state.
    void setFactor (int factor);
    void reset();

    int factor() const { return osFactor; }
    int latency() const { return osFactor > 1 ? kLatency : 0; }

    // Upsamples n base-rate samples. Returns pointer to n * factor samples.
    double* upsample (const float* in, int n);
    // Downsamples the buffer returned by upsample() back into out (n samples).
    void downsample (float* out, int n);

    const std::vector<double>& kernel (int factor) const;

private:
    static int kernelIndex (int factor) { return factor == 2 ? 0 : factor == 4 ? 1 : 2; }

    int osFactor = 1;
    std::array<std::vector<double>, 3> kernels; // 2x, 4x, 8x
    const std::vector<double>* taps = nullptr;
    std::vector<double> upHistory;   // doubled circular buffer
    int upLen = 0, upPos = 0;
    std::vector<double> downHistory; // doubled circular buffer
    int downLen = 0, downPos = 0;
    std::vector<double> osBuffer;
};

} // namespace sonic
