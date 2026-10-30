#pragma once

#include "AnalysisPanel.h"
#include "ChainPanel.h"
#include "ModuleEditor.h"
#include "SourcePanel.h"

class AtelierEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AtelierEditor (AtelierProcessor&);
    ~AtelierEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    AtelierProcessor& proc;
    AtelierLookAndFeel lnf; // must outlive the child components below
    juce::TooltipWindow tooltips { this, 600 };

    SourcePanel sourcePanel { proc };
    ChainPanel chainPanel { proc };
    ModuleEditor moduleEditor { proc };
    AnalysisPanel analysisPanel { proc };

    juce::Rectangle<int> header;
};
