#pragma once
// Tube stage model (v0.1).
//
// Signal path per channel:
//   input -> drive gain -> [oversampled]  grid-input low-pass (Miller capacitance)
//         -> biased asymmetric nonlinearity with dynamic bias shift (grid current
//            charging the coupling capacitor -> "blocking"/sag behaviour)
//         -> [downsample] -> coupling-capacitor DC blocker -> output trim
//
// The static curve is normalised to unity small-signal gain at the operating
// point, so Drive changes the amount of saturation rather than loudness.
// Triode vs pentode character is modelled by the asymmetry of the curve
// (triode: soft, strongly asymmetric -> 2nd harmonic dominant; pentode: harder,
// near-symmetric -> odd harmonics dominant). Bias moves the operating point.

#include "sonic/DspUtils.h"
#include "sonic/Module.h"
#include "sonic/Oversampler.h"

#include <array>

namespace sonic
{

class TubeModule final : public Module
{
public:
    TubeModule();

    const char* typeId() const override { return "tube.stage"; }
    const char* displayName() const override { return "Tube Stage"; }

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void process (float* const* channels, int numChannels, int numSamples) override;
    int latencySamples() const override;

    // Static transfer curve y = f(x) for the given parameter values, with drive
    // applied and normalisation included (dynamic bias and filters excluded).
    // x is the stage input in full-scale units.
    static double staticCurve (const float* paramValues, double x);
    double staticCurve (double x) const;

private:
    struct Shape
    {
        double gainPos = 1.0, gainNeg = 1.0, hardness = 2.0;
    };
    static Shape shapeFor (double character);
    static double saturate (const Shape& s, double u);
    static double slopeAt (const Shape& s, double u);

    struct Channel
    {
        Oversampler os;
        double lpState = 0.0;
        double env = 0.0;
        DcBlocker dc;
    };

    static constexpr int kMaxChannels = 2;
    std::array<Channel, kMaxChannels> chans;
    double fs = 48000.0;
    int maxBlock = 512;
    int activeFactor = 4;
    Smoother driveSm, biasSm, charSm, outSm;
    std::vector<float> scratch;
};

} // namespace sonic
