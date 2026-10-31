#pragma once
// Harmonic saturation with designable harmonic recipe.
//
// The shaper is a sum of Chebyshev polynomials: for a full-scale sine at the
// shaper input, T_n produces exactly the n-th harmonic, so each "H_n" control is
// the harmonic's level in dBc at that reference. "Level dependence" blends
// between a natural polynomial (harmonics grow with level) and an
// envelope-normalised one (harmonic balance stays constant with level).

#include "sonic/DspBlocks.h"
#include "sonic/Module.h"
#include "sonic/Oversampler.h"

#include <array>

namespace sonic
{

class SaturationModule final : public Module
{
public:
    static constexpr int kMaxHarmonic = 10; // H2..H10
    static constexpr int kNumRecipes = 8;

    struct Recipe
    {
        const char* name;
        const char* description;
        float levelsDbc[kMaxHarmonic - 1]; // H2..H10
        float softness, levelDependence;
    };
    static const Recipe& recipe (int index);
    // Writes the recipe into this module's parameters (UI action).
    void applyRecipe (int index);

    SaturationModule();

    const char* typeId() const override { return "character.saturation"; }
    const char* displayName() const override { return "Saturation"; }
    std::string topology() const override;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void process (float* const* channels, int numChannels, int numSamples) override;
    int latencySamples() const override;

    // Static transfer curve at the current settings (input = shaper input, full scale).
    double staticCurve (double x) const;

protected:
    void paramChanged (int index) override;
private:
    bool linking = false;
    struct Coeffs
    {
        double c[kMaxHarmonic + 1] {};
        double y0 = 0.0, slope = 1.0, v0 = 0.0, knee = 1.0;
    };
    Coeffs makeCoeffs() const;
    static double shaperIn (double a, double knee);
    static double poly (const Coeffs& k, double v);

    struct Chan
    {
        Oversampler os;
        Biquad lo, hi;          // focus split
        EnvFollower env;
        DcBlocker dc;
        Biquad tiltLo, tiltHi, lp;
        FracDelay dLo, dHi, dG; // align the clean paths with the oversampler latency
    };
    std::array<Chan, 2> ch;
    double fs = 48000.0;
    int maxBlock = 512, factor = 4;
    std::vector<float> lowBuf, highBuf, bandBuf;
};

} // namespace sonic
