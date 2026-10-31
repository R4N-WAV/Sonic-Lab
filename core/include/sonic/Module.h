#pragma once
// Atelier core - processing module interface.
// The core library has no JUCE dependency so the exact same DSP can later be
// compiled into a stand-alone VST3 and validated against Atelier measurements.

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace sonic
{

// Describes one parameter. This descriptor is the single source of truth used
// by the UI, by presets and by the blueprint exporter, so names, units and
// ranges can never drift apart.
struct ParamSpec
{
    std::string id;          // stable machine id, e.g. "drive_db"
    std::string name;        // display name
    std::string unit;        // "dB", "Hz", "ms", "%", "x", "" ...
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float defaultValue = 0.0f;
    float skewCentre = 0.0f; // if > 0, the control is skewed so this value sits mid-travel
    std::vector<std::string> choices; // non-empty => discrete choice parameter (value = index)
    std::string description;
    bool simple = false;     // shown in the block's SIMPLE view
    std::string group;       // section in the ADVANCED view

    bool isChoice() const { return ! choices.empty(); }

    static ParamSpec num (std::string id, std::string name, std::string unit, float mn, float mx, float def,
                          float skew, std::string desc, bool simple = false, std::string group = "Main")
    {
        ParamSpec p;
        p.id = std::move (id); p.name = std::move (name); p.unit = std::move (unit);
        p.minValue = mn; p.maxValue = mx; p.defaultValue = def; p.skewCentre = skew;
        p.description = std::move (desc); p.simple = simple; p.group = std::move (group);
        return p;
    }
    static ParamSpec choice (std::string id, std::string name, std::vector<std::string> items, int def,
                             std::string desc, bool simple = false, std::string group = "Main")
    {
        ParamSpec p;
        p.id = std::move (id); p.name = std::move (name);
        p.minValue = 0.0f; p.maxValue = (float) (items.size() > 0 ? items.size() - 1 : 0);
        p.defaultValue = (float) def; p.choices = std::move (items);
        p.description = std::move (desc); p.simple = simple; p.group = std::move (group);
        return p;
    }
    static ParamSpec toggle (std::string id, std::string name, bool def, std::string desc,
                             bool simple = false, std::string group = "Main")
    {
        return choice (std::move (id), std::move (name), { "Off", "On" }, def ? 1 : 0, std::move (desc), simple, std::move (group));
    }
};

// Information from the host, passed to every module once per block.
struct HostInfo
{
    double bpm = 120.0;
    bool playing = false;
};

class Module
{
public:
    virtual ~Module() = default;

    virtual const char* typeId() const = 0;
    virtual const char* displayName() const = 0;
    // One-paragraph technical description of the implemented topology (used in blueprints).
    virtual std::string topology() const { return {}; }

    const std::vector<ParamSpec>& params() const { return specs; }
    int numParams() const { return (int) specs.size(); }
    int paramIndex (const std::string& id) const;

    // Thread-safe: may be called from the UI thread while audio runs.
    void setParam (int index, float value);
    float getParam (int index) const;
    void setParam (const std::string& id, float value) { setParam (paramIndex (id), value); }
    float getParam (const std::string& id) const { return getParam (paramIndex (id)); }
    void resetParamsToDefault();
    // Incremented on every setParam; lets UIs/analysers notice changes cheaply.
    unsigned paramVersion() const { return version.load (std::memory_order_relaxed); }

    // Called before processing starts (not real-time safe).
    virtual void prepare (double sampleRate, int maxBlockSize, int numChannels) = 0;
    virtual void reset() = 0;

    // Real-time safe. Processes stereo audio in place.
    virtual void process (float* const* channels, int numChannels, int numSamples) = 0;

    virtual void setHostInfo (const HostInfo&) {}

    // Processing latency in samples at the current settings.
    virtual int latencySamples() const { return 0; }

    // Initial Mix of a newly created block (effects default to a parallel blend).
    virtual float defaultMix() const { return 1.0f; }

    // Live meter for the UI / analysis (thread-safe). 0 = gain reduction in dB (<= 0).
    virtual float meter (int /*which*/) const { return 0.0f; }

protected:
    void finaliseParams(); // call at the end of the derived constructor

    std::vector<ParamSpec> specs;
    std::unique_ptr<std::atomic<float>[]> values;
    std::atomic<unsigned> version { 0 };
};

} // namespace sonic
