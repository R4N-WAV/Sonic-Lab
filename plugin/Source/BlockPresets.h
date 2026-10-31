#pragma once
// Presets for single blocks (the Templates menu holds whole bridges).
//
// Factory presets are built in. Your own are stored per block type in
//   <user application data>/Atelier/Block presets/<block type>/<name>.atelier-block
// (XML with the parameter values by id, plus the block's Mix and Output).

#include <JuceHeader.h>
#include <sonic/Graph.h>

namespace blockpresets
{
    struct Preset
    {
        juce::String name;
        std::vector<std::pair<std::string, float>> params;   // anything not listed = default
        float mix = -1.0f;                                    // -1 = the block type's default Mix
        float outDb = 0.0f;
        bool factory = true;
        juce::File file;
    };

    const std::vector<Preset>& factory (const std::string& type);
    std::vector<Preset> personal (const std::string& type);
    juce::File folder (const std::string& type);

    // Resets the block to defaults, then applies the preset.
    void apply (const Preset& p, sonic::Graph::Node& node);
    bool save (const sonic::Graph::Node& node, const juce::String& name, juce::String& error);
    bool remove (const Preset& p);
}
