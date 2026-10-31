#include "sonic/ModuleRegistry.h"

#include "sonic/modules/CharacterModules.h"
#include "sonic/modules/CompressorModule.h"
#include "sonic/modules/EffectModules.h"
#include "sonic/modules/ParametricEqModule.h"
#include "sonic/modules/SaturationModule.h"
#include "sonic/modules/TubeModule.h"

#include <functional>

namespace sonic
{

namespace
{
    struct Entry
    {
        ModuleInfo info;
        std::function<std::unique_ptr<Module>()> make;
    };

    template <typename T>
    Entry entry (const char* cat, const char* summary)
    {
        T probe;
        return { { probe.typeId(), probe.displayName(), cat, summary }, [] { return std::make_unique<T>(); } };
    }

    const std::vector<Entry>& entries()
    {
        static const std::vector<Entry> list = {
            entry<TubeModule> ("Character", "Valve stage: triode/pentode harmonics, bias and sag."),
            entry<TapeModule> ("Character", "Magnetic tape: hysteresis, head bump, wow and flutter, hiss."),
            entry<TransformerModule> ("Character", "Iron: low-end saturation and gentle top-end resonance."),
            entry<ConsoleModule> ("Character", "Console channel colour, crosstalk and component tolerance."),
            entry<SaturationModule> ("Character", "Designable harmonics: pick a recipe or draw your own."),
            entry<CompressorModule> ("Dynamics", "Opto, FET, VCA, Vari-Mu or diode-bridge compression."),
            entry<TransientModule> ("Dynamics", "Shape the attack and sustain of every hit."),
            entry<ParametricEqModule> ("Tone", "Up to 24 bands, dynamic, mid/side, linear phase."),
            entry<FilterModule> ("Tone", "Resonant filter with LFO and envelope movement."),
            entry<ExciterModule> ("Tone", "Adds harmonics to the top end for presence."),
            entry<DelayModule> ("Space", "Echo with tempo sync, ping-pong and a saturated loop."),
            entry<ReverbModule> ("Space", "Smooth algorithmic reverb (feedback delay network)."),
            entry<ChorusModule> ("Modulation", "Multi-voice chorus."),
            entry<FlangerModule> ("Modulation", "Swept comb flanger."),
            entry<PhaserModule> ("Modulation", "Allpass phaser, 2 to 12 stages."),
            entry<PitchModule> ("Pitch", "Transpose by semitones and cents; doubler, octaver, shimmer."),
            entry<StereoModule> ("Utility", "Width, mid/side, bass mono, balance."),
            entry<GainModule> ("Utility", "Gain, polarity, mono."),
        };
        return list;
    }
}

const std::vector<ModuleInfo>& availableModules()
{
    static const std::vector<ModuleInfo> infos = []
    {
        std::vector<ModuleInfo> v;
        for (auto& e : entries())
            v.push_back (e.info);
        return v;
    }();
    return infos;
}

std::unique_ptr<Module> createModule (const std::string& typeId)
{
    for (auto& e : entries())
        if (e.info.typeId == typeId)
            return e.make();
    return nullptr;
}

} // namespace sonic
