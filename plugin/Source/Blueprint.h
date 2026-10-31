#pragma once
// Blueprint Studio: exports a complete, self-contained engineering package that
// describes the exact DSP configuration of the bridge, plus measurements, graphs
// and audio references, for implementing a stand-alone VST3 from it.

#include "Ui.h"

namespace Blueprint
{
    struct Options
    {
        juce::String projectName, objective;
        double sampleRate = 48000.0;
        bool includeAudio = true, includePdf = true;
        juce::File baseFolder;
    };

    // Overlay dialog (form, progress, result). onClose is called when it should be removed.
    std::unique_ptr<juce::Component> createDialog (Ui& ui, std::function<void()> onClose);

    // Runs the export synchronously (call from a background thread). Returns the package folder.
    juce::File exportPackage (const juce::ValueTree& bridge, const Options& opt, const juce::File& userAudio,
                              std::function<void (double, const juce::String&)> progress, std::function<bool()> shouldStop,
                              juce::String& error);
}
