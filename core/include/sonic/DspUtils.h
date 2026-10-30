#pragma once

#include <cmath>

namespace sonic
{

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

inline double dbToGain (double db) { return std::pow (10.0, db / 20.0); }
inline double gainToDb (double g, double floorDb = -200.0)
{
    return g > 0.0 ? std::fmax (20.0 * std::log10 (g), floorDb) : floorDb;
}

// Removes denormals cheaply in feedback paths.
inline double flushDenormal (double x) { return std::fabs (x) < 1.0e-30 ? 0.0 : x; }

// One-pole parameter smoother (time constant in seconds).
class Smoother
{
public:
    void prepare (double sampleRate, double timeSeconds)
    {
        coeff = timeSeconds > 0.0 ? std::exp (-1.0 / (timeSeconds * sampleRate)) : 0.0;
    }
    void snap (double v) { current = v; }
    double next (double target)
    {
        current = target + coeff * (current - target);
        return current;
    }
    double value() const { return current; }

private:
    double coeff = 0.0, current = 0.0;
};

// First-order DC blocking high-pass (models a coupling capacitor).
class DcBlocker
{
public:
    void prepare (double sampleRate, double cutoffHz)
    {
        r = std::exp (-kTwoPi * cutoffHz / sampleRate);
        reset();
    }
    void reset() { x1 = y1 = 0.0; }
    double process (double x)
    {
        const double y = x - x1 + r * y1;
        x1 = x;
        y1 = flushDenormal (y);
        return y1;
    }

private:
    double r = 0.999, x1 = 0.0, y1 = 0.0;
};

} // namespace sonic
