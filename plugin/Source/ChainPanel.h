#pragma once

#include "Widgets.h"

#include <functional>

// Centre column: processing chain (order, enable, selection), latency-matched
// A/B bypass, input/output trims and meters.
class ChainPanel : public juce::Component, private juce::Timer
{
public:
    explicit ChainPanel (AtelierProcessor& p);

    std::function<void (int moduleIndex)> onSelect;
    int selectedModule() const { return selected; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Row : public juce::Component
    {
    public:
        Row (ChainPanel& owner, int moduleIndex, int slot, int numSlots);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;

        const int moduleIndex, slot;

    private:
        ChainPanel& owner;
        juce::ToggleButton enable;
        std::unique_ptr<ButtonAttachment> enableAttachment;
        juce::TextButton up { "Up" }, down { "Dn" };
    };

    void rebuildRows();
    void timerCallback() override;

    AtelierProcessor& proc;
    juce::OwnedArray<Row> rows;
    int selected = 0;
    std::vector<int> shownOrder;

    juce::TextButton abButton { "A  Processed" };
    std::unique_ptr<ButtonAttachment> abAttachment;
    ParamSlider inTrim { proc.apvts, "io.in_trim_db", "Input Trim", "Gain after the source, before the chain." };
    ParamSlider outTrim { proc.apvts, "io.out_trim_db", "Output Trim", "Gain after the chain." };
    LevelMeter inMeter { proc.inputMeter, "IN" }, outMeter { proc.outputMeter, "OUT" }, scMeter { proc.sidechainMeter, "SC" };
    juce::Label latencyLabel;
};
