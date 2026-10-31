#pragma once

#include "BlockEditors.h"
#include "Bridge.h"
#include "Inspector.h"
#include "MainPanels.h"

class AtelierEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AtelierEditor (AtelierProcessor&);
    ~AtelierEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    static constexpr int kW = 1600, kH = 1000;

private:
    void timerCallback() override;
    void openEditor (int nodeId);
    void closeEditor();
    void showExport();

    AtelierProcessor& proc;
    AtelierLookAndFeel lnf; // must outlive all children
    AnalysisWorker worker { proc };
    Ui ui { proc, worker };

    struct Content : public juce::Component
    {
        void paint (juce::Graphics& g) override;
    } content;

    HeaderBar header { ui };
    SpectrumPanel spectrum { ui };
    ReadoutPanel readout { ui };
    BridgePanel bridge { ui };
    InspectorPanel inspector { ui };
    std::unique_ptr<BlockEditor> overlay;
    std::unique_ptr<juce::Component> exportDialog;
    juce::TooltipWindow tooltips { this, 700 };
};
