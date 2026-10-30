#pragma once
// SONIC LAB core - processing module interface.
// The core library has no JUCE dependency so the exact same DSP can later be
// compiled into a VST3 plugin and validated against SONIC LAB measurements.

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace sonic
{

// Describes one parameter. This descriptor is the single source of truth used
// by the UI, by presets and (later) by the blueprint exporter, so names, units
// and ranges can never drift apart.
struct ParamSpec
{
    std::string id;          // stable machine id, e.g. "drive_db"
    std::string name;        // display name
    std::string unit;        // "dB", "Hz", "%", "" ...
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float defaultValue = 0.0f;
    float skewCentre = 0.0f; // if > 0, the slider is skewed so this value sits mid-travel
    std::vector<std::string> choices; // non-empty => discrete choice parameter (value = index)
    std::string description;

    bool isChoice() const { return ! choices.empty(); }
};

class Module
{
public:
    virtual ~Module() = default;

    virtual const char* typeId() const = 0;
    virtual const char* displayName() const = 0;

    const std::vector<ParamSpec>& params() const { return specs; }
    int numParams() const { return (int) specs.size(); }
    int paramIndex (const std::string& id) const;

    // Thread-safe: may be called from the UI thread while audio runs.
    void setParam (int index, float value);
    float getParam (int index) const;
    void setParam (const std::string& id, float value) { setParam (paramIndex (id), value); }
    float getParam (const std::string& id) const { return getParam (paramIndex (id)); }

    // Called before processing starts (not real-time safe).
    virtual void prepare (double sampleRate, int maxBlockSize, int numChannels) = 0;
    virtual void reset() = 0;

    // Real-time safe. Processes audio in place.
    virtual void process (float* const* channels, int numChannels, int numSamples) = 0;

    // Processing latency in samples at the current settings.
    virtual int latencySamples() const { return 0; }

protected:
    void finaliseParams(); // call at the end of the derived constructor

    std::vector<ParamSpec> specs;
    std::unique_ptr<std::atomic<float>[]> values;
};

} // namespace sonic
