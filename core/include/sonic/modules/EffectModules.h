#pragma once
// Time-based, modulation and utility blocks.

#include "sonic/DspBlocks.h"
#include "sonic/Module.h"

#include <array>

namespace sonic
{

#define SONIC_MODULE_BOILERPLATE(Type, Id, Name)                                          \
public:                                                                                 \
    Type();                                                                             \
    const char* typeId() const override { return Id; }                                 \
    const char* displayName() const override { return Name; }                           \
    std::string topology() const override;                                              \
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;       \
    void reset() override;                                                              \
    void process (float* const* channels, int numChannels, int numSamples) override;

class DelayModule final : public Module
{
    SONIC_MODULE_BOILERPLATE (DelayModule, "space.delay", "Delay")
    void setHostInfo (const HostInfo& h) override { bpm = h.bpm > 1.0 ? h.bpm : 120.0; }
    float defaultMix() const override { return 0.3f; }
    double delayTimeMs() const; // current target time (sync resolved)

private:
    std::array<FracDelay, 2> line;
    std::array<Biquad, 2> lp, hp;
    std::array<double, 2> smoothedDelay { 0.0, 0.0 };
    Lfo wobble;
    double fs = 48000.0, bpm = 120.0, duckEnv = 0.0;
};

class ReverbModule final : public Module
{
    SONIC_MODULE_BOILERPLATE (ReverbModule, "space.reverb", "Reverb")
    float defaultMix() const override { return 0.25f; }

private:
    static constexpr int kLines = 8;
    FracDelay pre;
    std::array<FracDelay, 4> diff;
    std::array<int, 4> diffLen {};
    std::array<FracDelay, kLines> lines;
    std::array<double, kLines> baseLen {};
    std::array<OnePoleLP, kLines> damp, split;
    std::array<Lfo, 4> mods;
    Biquad inHp;
    double fs = 48000.0;
};

class ChorusModule final : public Module
{
    SONIC_MODULE_BOILERPLATE (ChorusModule, "mod.chorus", "Chorus")
    float defaultMix() const override { return 0.5f; }
private:
    std::array<FracDelay, 2> line;
    std::array<Lfo, 4> lfo;
    std::array<double, 2> fb { 0.0, 0.0 }, toneZ { 0.0, 0.0 };
    double fs = 48000.0;
};

class FlangerModule final : public Module
{
    SONIC_MODULE_BOILERPLATE (FlangerModule, "mod.flanger", "Flanger")
    float defaultMix() const override { return 0.5f; }
private:
    std::array<FracDelay, 2> line;
    Lfo lfo;
    std::array<double, 2> fb { 0.0, 0.0 };
    double fs = 48000.0;
};

class PhaserModule final : public Module
{
    SONIC_MODULE_BOILERPLATE (PhaserModule, "mod.phaser", "Phaser")
    float defaultMix() const override { return 0.5f; }
private:
    static constexpr int kMaxStages = 12;
    std::array<std::array<double, kMaxStages>, 2> z {};
    std::array<double, 2> fb { 0.0, 0.0 };
    Lfo lfo;
    double fs = 48000.0;
};

class ExciterModule final : public Module
{
    SONIC_MODULE_BOILERPLATE (ExciterModule, "tone.exciter", "Exciter")
private:
    struct Chan { Biquad hp1, hp2, post; };
    std::array<Chan, 2> ch;
    double fs = 48000.0;
};

class TransientModule final : public Module
{
    SONIC_MODULE_BOILERPLATE (TransientModule, "dynamics.transient", "Transient")
private:
    EnvFollower fast, slow;
    double fs = 48000.0;
};

class StereoModule final : public Module
{
    SONIC_MODULE_BOILERPLATE (StereoModule, "utility.stereo", "Stereo")
private:
    Biquad sideHp1, sideHp2;
    double fs = 48000.0;
};

class FilterModule final : public Module
{
    SONIC_MODULE_BOILERPLATE (FilterModule, "tone.filter", "Filter")
private:
    std::array<std::array<Svf, 2>, 2> svf; // [stage][channel]
    EnvFollower env;
    Lfo lfo;
    double fs = 48000.0;
};

class GainModule final : public Module
{
    SONIC_MODULE_BOILERPLATE (GainModule, "utility.gain", "Utility")
};

#undef SONIC_MODULE_BOILERPLATE

} // namespace sonic
