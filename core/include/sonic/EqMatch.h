#pragma once
// EQ Match: fits a small set of bell bands that move one long-term spectrum (yours)
// toward another (the reference). Spectra are power-averaged and smoothed to 1/3
// octave first, and the overall level difference is ignored, so only tone counts.

#include <vector>

namespace sonic::eqmatch
{

struct Band
{
    double freq = 1000.0, gainDb = 0.0, q = 1.0;
};

// A long-term spectrum: power (linear, any consistent scale) per linear frequency bin.
struct Spectrum
{
    double binHz = 0.0;
    std::vector<double> power;   // index k = k * binHz
    int frames = 0;              // how many frames were averaged (0 = empty)
    bool empty() const { return frames == 0 || power.empty() || binHz <= 0.0; }
};

struct Fit
{
    std::vector<Band> bands;
    std::vector<double> gridHz;       // 25 Hz .. 18 kHz, log spaced
    std::vector<double> wantedDb;     // smoothed target-minus-yours curve (after Amount), what the bands aim at
    std::vector<double> fittedDb;     // what the fitted bands actually do
    double largestRemainingDb = 0.0;
};

// Smoothed level (dB, 1/3 octave power average) of a spectrum at f.
double levelAt (const Spectrum& s, double f);

// amount 0..1 scales the correction; maxBands caps the number of bells.
Fit fit (const Spectrum& yours, const Spectrum& reference, double amount, int maxBands, double sampleRate);

} // namespace sonic::eqmatch
