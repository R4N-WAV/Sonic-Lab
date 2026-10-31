#pragma once
// Templates: complete bridges (blocks, wiring and every setting) to start from.
// Factory templates are built in code; personal templates are files in
//   <user application data>/Atelier/Templates/*.atelier-template
// (an XML document holding the BRIDGE tree plus name, category and description).

#include <JuceHeader.h>

#include <functional>
#include <vector>

namespace sonic { class Graph; }

namespace templates
{
    struct Template
    {
        juce::String name, category, description;
        bool factory = true;
        juce::File file;                                  // personal templates
        std::function<void (sonic::Graph&)> build;        // factory templates
    };

    const std::vector<Template>& factory();
    std::vector<Template> personal();                     // scanned from disk, sorted by name
    juce::File folder();

    // The bridge (as a BRIDGE tree) a template describes; invalid tree on failure.
    juce::ValueTree bridgeFor (const Template&);

    bool save (const juce::String& name, const juce::String& category, const juce::String& description,
               const juce::ValueTree& bridge, juce::String& error);
    bool remove (const Template&);
}
