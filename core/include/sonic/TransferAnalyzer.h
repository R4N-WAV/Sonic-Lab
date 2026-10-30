#pragma once
// Black-box transfer-function estimator (Welch-averaged H1 estimator).
//
// Given the signal entering a device (x) and leaving it (y), estimates the
// LINEAR part of the device's frequency response:
//   H(f) = Sxy(f) / Sxx(f)          (magnitude and phase)
//   coherence(f) = |Sxy|^2 / (Sxx * Syy)   (0..1)
// Coherence near 1 means y is explained by a linear filter of x at that
// frequency; nonlinearity, noise, time variance or misalignment lower it.
// The bulk delay between x and y is estimated with a PHAT-weighted cross
// correlation and removed from the phase (it is reported separately).
//
// These results are ESTIMATES of the linear behaviour only. They do not
// reveal circuit topology or component values.

#include <complex>
#include <vector>

namespace sonic
{

class TransferAnalyzer
{
public:
    // segmentSize: FFT size per Welch segment (power of two).
    // averagingTimeConstantFrames: >0 enables exponential averaging across
    // successive calls to addBlock(); 0 = no memory between calls.
    void prepare (double sampleRate, int segmentSize, double averagingFrames = 8.0);
    void reset();

    // Adds a block of aligned samples (x = device input, y = device output).
    void addBlock (const float* x, const float* y, int numSamples);

    struct Result
    {
        bool valid = false;
        double sampleRate = 0.0;
        int segmentSize = 0;
        double binHz = 0.0;
        std::vector<double> magnitudeDb;   // per bin (0..N/2)
        std::vector<double> phaseRad;      // per bin, bulk delay removed
        std::vector<double> coherence;     // per bin, 0..1
        std::vector<bool> usable;          // enough input energy in this bin
        int estimatedDelaySamples = 0;     // y lags x by this many samples (estimate)
        double meanCoherence = 0.0;        // over usable bins 20 Hz..20 kHz
        int segmentsAveraged = 0;
    };

    Result result() const;

private:
    double fs = 48000.0;
    int n = 4096;
    double decay = 0.0;
    std::vector<double> window;
    std::vector<double> sxx, syy;
    std::vector<std::complex<double>> sxy;
    double effectiveSegments = 0.0;
    std::vector<std::complex<double>> bx, by; // scratch
};

} // namespace sonic
