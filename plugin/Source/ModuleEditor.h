#pragma once

#include "Widgets.h"

// Editor for the selected chain module: controls generated from the module's
// parameter specs plus a graph computed from the SAME DSP code the audio uses
// (Tube: static transfer curve; EQ: magnitude + phase from the filter coefficients).
class ModuleEditor : public juce::Component, private juce::Timer
{
public:
    explicit ModuleEditor (AtelierProcessor& p);

    void setModule (int moduleIndex);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawTubeGraph (juce::Graphics&, juce::Rectangle<float>);
    void drawEqGraph (juce::Graphics&, juce::Rectangle<float>);

    AtelierProcessor& proc;
    int moduleIndex = 0;
    juce::OwnedArray<juce::Component> controls;
    juce::Rectangle<int> graphArea;
    juce::Label description;
};
