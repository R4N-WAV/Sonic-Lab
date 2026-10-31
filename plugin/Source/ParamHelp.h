#pragma once
// Plain-language help for every control: what it does to the sound, and what
// turning it up or down does. Shown when you hover a control.

#include <JuceHeader.h>
#include <sonic/Module.h>

namespace help
{
    struct Text
    {
        juce::String what, more, less;
        bool empty() const { return what.isEmpty(); }
    };

    // Help for a block parameter (block type id + parameter id).
    Text forParam (const std::string& blockType, const std::string& paramId);

    // Ready-made tooltip: "Name: what\nMore = ...\nLess = ..." (+ why it is greyed out).
    juce::String tooltip (const sonic::Module& m, int paramIndex);

    // Tooltip from loose text (for controls that are not block parameters).
    juce::String tooltip (const juce::String& name, const Text& t);
}
