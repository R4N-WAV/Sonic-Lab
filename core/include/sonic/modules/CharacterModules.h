#pragma once
// Analog character blocks: Tape, Transformer, Console.

#include "sonic/DspBlocks.h"
#include "sonic/Module.h"
#include "sonic/Oversampler.h"

#include <array>

namespace sonic
{

// Magnetic tape: record pre-emphasis -> hysteresis nonlinearity (play-operator
// model blended with anhysteretic saturation) -> playback de-emphasis -> head
// bump + gap loss (speed-dependent) -> wow & flutter (modulated delay) -> hiss.
class TapeModule final : public Module
{
public:
    TapeModule();
    const char* typeId() const override { return "character.tape"; }
    const char* displayName() const override { return "Tape"; }
    std::string topology() const override;
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void process (float* const* channels, int numChannels, int numSamples) override;
    int latencySamples() const override;

private:
    struct Chan
    {
        Oversampler os;
        Biquad pre, de, bump, gap1, gap2;
        double play = 0.0;
        FracDelay wf;
        DcBlocker dc;
        OnePoleLP hissLp;
    };
    std::array<Chan, 2> ch;
    Lfo wow, flutter, flutter2;
    Noise noise { 0x51ED };
    OnePoleLP wowSmooth;
    double fs = 48000.0, driftPhase = 0.0;
    int maxBlock = 512, factor = 2;
    std::vector<float> tmp;
};

// Audio transformer: core saturation in the flux domain (flux = integral of the
// voltage), so low frequencies saturate first; LF corner, HF resonance + roll-off.
class TransformerModule final : public Module
{
public:
    TransformerModule();
    const char* typeId() const override { return "character.transformer"; }
    const char* displayName() const override { return "Transformer"; }
    std::string topology() const override;
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void process (float* const* channels, int numChannels, int numSamples) override;
    int latencySamples() const override;

private:
    struct Chan
    {
        Oversampler os;
        double flux = 0.0, errPrev = 0.0, hyst = 0.0;
        Biquad lfCut, hfRes, hfLp;
        DcBlocker dc;
    };
    std::array<Chan, 2> ch;
    double fs = 48000.0;
    int maxBlock = 512, factor = 2;
};

// Console channel / bus: subtle saturation, voicing, inter-channel crosstalk,
// noise floor and per-channel component tolerance.
class ConsoleModule final : public Module
{
public:
    ConsoleModule();
    const char* typeId() const override { return "character.console"; }
    const char* displayName() const override { return "Console"; }
    std::string topology() const override;
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void process (float* const* channels, int numChannels, int numSamples) override;

private:
    struct Chan
    {
        Biquad lo, mid, hi, hp, xtalk;
        DcBlocker dc;
        double tolGain = 1.0, tolShelf = 0.0, tolHp = 0.0;
    };
    std::array<Chan, 2> ch;
    Noise noise { 0xC0501E };
    double fs = 48000.0;
    unsigned lastVersion = ~0u;
    void design();
};

} // namespace sonic
