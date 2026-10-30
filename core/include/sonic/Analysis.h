#pragma once
// Measurement primitives: FFT, windows, amplitude spectrum, harmonic analysis.
// Amplitudes are calibrated so a full-scale sine reads 0 dBFS.

#include <complex>
#include <string>
#include <vector>

namespace sonic
{

enum class WindowType { hann = 0, blackmanHarris4 };
const char* windowName (WindowType w);

// In-place radix-2 complex FFT. size must be a power of two.
void fft (std::vector<std::complex<double>>& data);

std::vector<double> makeWindow (WindowType type, int size);

struct Spectrum
{
    double sampleRate = 0.0;
    int fftSize = 0;
    WindowType window = WindowType::blackmanHarris4;
    std::vector<double> amplitude;  // linear peak amplitude per bin (0..fftSize/2)
    std::vector<double> power;      // |X|^2 per bin, for energy summation
    double windowSumSq = 0.0;

    double binHz() const { return sampleRate / fftSize; }
    // Sinusoid amplitude found by summing energy over +-halfWidth bins around freq.
    double toneAmplitude (double freq, int halfWidth) const;
};

// Computes the one-sided amplitude spectrum of the LAST fftSize samples.
Spectrum computeSpectrum (const float* samples, int numSamples, int fftSize,
                          double sampleRate, WindowType window);

struct HarmonicResult
{
    bool valid = false;
    std::string invalidReason;
    double fundamentalHz = 0.0;
    int numHarmonics = 0;              // including the fundamental
    std::vector<double> levelDbfs;     // index 0 = fundamental (H1)
    std::vector<double> levelDbc;      // relative to fundamental
    double thdRatio = 0.0;             // sqrt(sum H2..Hn^2) / H1
    double thdPercent = 0.0;
    double thdDb = 0.0;
    double thdNRatio = 0.0;            // (total - fundamental) / fundamental, within analysis band
    double thdNDb = 0.0;
    double noiseFloorDbfs = 0.0;       // median bin level (indicative)
    // test conditions
    int fftSize = 0;
    double sampleRate = 0.0;
    WindowType window = WindowType::blackmanHarris4;
};

// Measures harmonics of a known fundamental. Harmonics above maxFreqHz (or
// Nyquist) are excluded and reported via numHarmonics.
HarmonicResult analyseHarmonics (const Spectrum& spec, double fundamentalHz,
                                 int maxHarmonics = 10, double maxFreqHz = 20000.0);

} // namespace sonic
