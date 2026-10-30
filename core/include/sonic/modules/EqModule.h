#pragma once
// Five-section equaliser: HPF, low shelf, bell, high shelf, LPF.
// design() is used by BOTH the audio path and the UI response plot.

#include "sonic/Biquad.h"
#include "sonic/Module.h"

#include <array>

namespace sonic
{

class EqModule final : public Module
{
public:
    static constexpr int kNumSections = 5;
    using Design = std::array<BiquadCoeffs, kNumSections>;

    EqModule();

    const char* typeId() const override { return "eq.parametric5"; }
    const char* displayName() const override { return "Parametric EQ"; }

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void process (float* const* channels, int numChannels, int numSamples) override;

    // Coefficients for the given parameter values (array indexed like params()).
    static Design design (double sampleRate, const float* paramValues);
    // Coefficients for the module's current target parameters.
    Design currentDesign (double sampleRate) const;

    // Combined magnitude (linear) / phase (radians) of a design.
    static double magnitudeAt (const Design& d, double fs, double freq);
    static double phaseAt (const Design& d, double fs, double freq);

private:
    static constexpr int kMaxChannels = 2;
    double fs = 48000.0;
    std::array<std::array<Biquad, kNumSections>, kMaxChannels> filters;
    std::vector<float> smoothed;
    double smoothCoeff = 0.0;
};

} // namespace sonic
