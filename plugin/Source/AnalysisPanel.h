#pragma once

#include "AnalysisWorker.h"
#include "Widgets.h"

// Bottom-right: live analysis. Two measurement modes:
//   Chain      - ATELIER's own chain, input -> output
//   Reference  - a reference plugin in the DAW: sidechain (unprocessed) -> main input (processed)
// Views: spectrum or linear transfer function; harmonics/THD box always visible.
class AnalysisPanel : public juce::Component, private juce::Timer
{
public:
    explicit AnalysisPanel (AtelierProcessor& p);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawSpectrum (juce::Graphics&, juce::Rectangle<float>);
    void drawTransfer (juce::Graphics&, juce::Rectangle<float>);
    void drawHarmonics (juce::Graphics&, juce::Rectangle<int>);
    void drawTag (juce::Graphics&, juce::Rectangle<int>, const juce::String& text, juce::Colour c);

    AtelierProcessor& proc;
    AnalysisWorker worker { proc };
    AnalysisWorker::Results res;

    juce::ComboBox modeCombo, viewCombo, fftCombo, windowCombo;
    juce::TextButton resetButton { "Reset avg" };
    juce::Rectangle<int> graphArea, harmArea;
};
