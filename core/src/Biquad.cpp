#include "sonic/Biquad.h"
#include "sonic/DspUtils.h"

#include <algorithm>
#include <cmath>

namespace sonic
{

namespace
{
    BiquadCoeffs normalise (double b0, double b1, double b2, double a0, double a1, double a2)
    {
        BiquadCoeffs c;
        c.b0 = b0 / a0;
        c.b1 = b1 / a0;
        c.b2 = b2 / a0;
        c.a1 = a1 / a0;
        c.a2 = a2 / a0;
        return c;
    }

    double clampFreq (double fs, double f) { return std::clamp (f, 1.0, fs * 0.4999); }
}

BiquadCoeffs BiquadCoeffs::peaking (double fs, double freq, double q, double gainDb)
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = kTwoPi * clampFreq (fs, freq) / fs;
    const double alpha = std::sin (w0) / (2.0 * std::max (q, 0.01));
    const double c = std::cos (w0);
    return normalise (1 + alpha * A, -2 * c, 1 - alpha * A,
                      1 + alpha / A, -2 * c, 1 - alpha / A);
}

BiquadCoeffs BiquadCoeffs::lowShelf (double fs, double freq, double slope, double gainDb)
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = kTwoPi * clampFreq (fs, freq) / fs;
    const double c = std::cos (w0);
    const double alpha = std::sin (w0) / 2.0 * std::sqrt ((A + 1 / A) * (1 / std::max (slope, 0.05) - 1) + 2);
    const double sA = 2 * std::sqrt (A) * alpha;
    return normalise (A * ((A + 1) - (A - 1) * c + sA),
                      2 * A * ((A - 1) - (A + 1) * c),
                      A * ((A + 1) - (A - 1) * c - sA),
                      (A + 1) + (A - 1) * c + sA,
                      -2 * ((A - 1) + (A + 1) * c),
                      (A + 1) + (A - 1) * c - sA);
}

BiquadCoeffs BiquadCoeffs::highShelf (double fs, double freq, double slope, double gainDb)
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = kTwoPi * clampFreq (fs, freq) / fs;
    const double c = std::cos (w0);
    const double alpha = std::sin (w0) / 2.0 * std::sqrt ((A + 1 / A) * (1 / std::max (slope, 0.05) - 1) + 2);
    const double sA = 2 * std::sqrt (A) * alpha;
    return normalise (A * ((A + 1) + (A - 1) * c + sA),
                      -2 * A * ((A - 1) + (A + 1) * c),
                      A * ((A + 1) + (A - 1) * c - sA),
                      (A + 1) - (A - 1) * c + sA,
                      2 * ((A - 1) - (A + 1) * c),
                      (A + 1) - (A - 1) * c - sA);
}

BiquadCoeffs BiquadCoeffs::highPass (double fs, double freq, double q)
{
    const double w0 = kTwoPi * clampFreq (fs, freq) / fs;
    const double c = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * std::max (q, 0.01));
    return normalise ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
}

BiquadCoeffs BiquadCoeffs::lowPass (double fs, double freq, double q)
{
    const double w0 = kTwoPi * clampFreq (fs, freq) / fs;
    const double c = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * std::max (q, 0.01));
    return normalise ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + alpha, -2 * c, 1 - alpha);
}

BiquadCoeffs BiquadCoeffs::highPass1 (double fs, double freq)
{
    const double k = std::tan (kPi * clampFreq (fs, freq) / fs);
    BiquadCoeffs c;
    c.b0 = 1.0 / (1.0 + k);
    c.b1 = -c.b0;
    c.b2 = 0.0;
    c.a1 = (k - 1.0) / (k + 1.0);
    c.a2 = 0.0;
    return c;
}

BiquadCoeffs BiquadCoeffs::lowPass1 (double fs, double freq)
{
    const double k = std::tan (kPi * clampFreq (fs, freq) / fs);
    BiquadCoeffs c;
    c.b0 = k / (1.0 + k);
    c.b1 = c.b0;
    c.b2 = 0.0;
    c.a1 = (k - 1.0) / (k + 1.0);
    c.a2 = 0.0;
    return c;
}

BiquadCoeffs BiquadCoeffs::notch (double fs, double freq, double q)
{
    const double w0 = kTwoPi * clampFreq (fs, freq) / fs;
    const double c = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * std::max (q, 0.01));
    return normalise (1, -2 * c, 1, 1 + alpha, -2 * c, 1 - alpha);
}

BiquadCoeffs BiquadCoeffs::bandPass (double fs, double freq, double q)
{
    const double w0 = kTwoPi * clampFreq (fs, freq) / fs;
    const double c = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * std::max (q, 0.01));
    return normalise (alpha, 0, -alpha, 1 + alpha, -2 * c, 1 - alpha);
}

namespace
{
    double slopeFromQ (double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double inv = (1.0 / (q * q) - 2.0) / (A + 1.0 / A) + 1.0;
        return inv > 0.0 ? std::clamp (1.0 / inv, 0.05, 10.0) : 10.0;
    }
}

BiquadCoeffs BiquadCoeffs::lowShelfQ (double fs, double freq, double q, double gainDb)
{
    return lowShelf (fs, freq, slopeFromQ (std::max (q, 0.05), gainDb), gainDb);
}

BiquadCoeffs BiquadCoeffs::highShelfQ (double fs, double freq, double q, double gainDb)
{
    return highShelf (fs, freq, slopeFromQ (std::max (q, 0.05), gainDb), gainDb);
}

std::complex<double> BiquadCoeffs::responseAt (double fs, double freq) const
{
    const double w = kTwoPi * freq / fs;
    const std::complex<double> z1 = std::polar (1.0, -w);
    const std::complex<double> z2 = z1 * z1;
    return (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
}

} // namespace sonic
