#pragma once
// Second-order IIR section (RBJ "Audio EQ Cookbook" designs), double precision.
// The same coefficients drive both the audio path and magnitudeAt()/phaseAt(),
// so every displayed curve is the exact response of the implemented filter.

#include <complex>

namespace sonic
{

struct BiquadCoeffs
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; // normalised (a0 = 1)

    static BiquadCoeffs identity() { return {}; }
    static BiquadCoeffs peaking (double fs, double freq, double q, double gainDb);
    static BiquadCoeffs lowShelf (double fs, double freq, double slope, double gainDb);
    static BiquadCoeffs highShelf (double fs, double freq, double slope, double gainDb);
    static BiquadCoeffs highPass (double fs, double freq, double q);
    static BiquadCoeffs lowPass (double fs, double freq, double q);

    std::complex<double> responseAt (double fs, double freq) const;
    double magnitudeAt (double fs, double freq) const { return std::abs (responseAt (fs, freq)); }
    double phaseAt (double fs, double freq) const { return std::arg (responseAt (fs, freq)); }
};

class Biquad
{
public:
    void setCoeffs (const BiquadCoeffs& c) { k = c; }
    const BiquadCoeffs& coeffs() const { return k; }
    void reset() { z1 = z2 = 0.0; }

    double process (double x)
    {
        // Transposed direct form II
        const double y = k.b0 * x + z1;
        z1 = k.b1 * x - k.a1 * y + z2;
        z2 = k.b2 * x - k.a2 * y;
        if (std::abs (z1) < 1e-30) z1 = 0.0;
        if (std::abs (z2) < 1e-30) z2 = 0.0;
        return y;
    }

private:
    BiquadCoeffs k;
    double z1 = 0.0, z2 = 0.0;
};

} // namespace sonic
