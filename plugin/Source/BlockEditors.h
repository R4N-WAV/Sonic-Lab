#pragma once

#include "Ui.h"

#include <sonic/modules/ParametricEqModule.h>
#include <sonic/modules/SaturationModule.h>

// Full-window editors opened from the bridge ("<- BRIDGE" returns).
class BlockEditor : public juce::Component, protected juce::Timer
{
public:
    BlockEditor (Ui& ui, int nodeId) : ui (ui), nodeId (nodeId) {}
    std::function<void()> onClose;
    int node() const { return nodeId; }
    // Called on every display refresh that brought new analysis data.
    virtual void analysisUpdated() {}
protected:
    sonic::Graph::Node* nodePtr() { return ui.proc.graph().node (nodeId); }
    Ui& ui;
    int nodeId;
};

// ------------------------------------------------------------------ EQ

class EqEditor : public BlockEditor
{
public:
    EqEditor (Ui& ui, int nodeId);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void analysisUpdated() override;

private:
    using EQ = sonic::ParametricEqModule;
    void timerCallback() override;
    EQ& eq();
    juce::Rectangle<float> plot() const;
    double range() const;
    int bandAt (juce::Point<float> p) const;
    juce::Point<float> bandPoint (int b) const;
    void selectBand (int b);
    void buildBandControls();
    void bandMenu (int b);
    void loadReference();
    void computeMatch();

    juce::TextButton back { juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x90 Bridge")) };
    juce::OwnedArray<juce::TextButton> rangeButtons;
    double fixedRange = 12.0;
    bool autoRange = false;
    int selected = -1, dragBand = -1;
    juce::Point<float> dragStart;
    float startFreq = 0, startGain = 0;
    juce::Point<float> hover { -1, -1 };

    // band controls
    juce::OwnedArray<juce::TextButton> bandChips;
    juce::OwnedArray<ModuleParamRow> bandRows;
    juce::TextButton addBand { "+ ADD BAND" }, removeBand { "REMOVE BAND" };
    // processing
    std::unique_ptr<ModuleChoice> phaseMode, linQuality;
    std::unique_ptr<ModuleParamRow> outputRow;
    // EQ match
    juce::TextButton refButton { "LOAD REFERENCE..." }, learnButton { "LEARN INPUT" }, applyButton { "APPLY MATCH" };
    AtelierSlider amount, maxBands;
    std::vector<double> refFreq, refDb;         // reference long-term spectrum
    std::vector<double> learnPow;               // accumulated input power per analysis bin
    double learnBinHz = 0.0;
    int learnFrames = 0;
    juce::String refName, matchStatus;
    std::unique_ptr<juce::FileChooser> chooser;
    unsigned lastVersion = ~0u;
};

// ------------------------------------------------------------------ Saturation

class SaturationEditor : public BlockEditor
{
public:
    SaturationEditor (Ui& ui, int nodeId);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    using Sat = sonic::SaturationModule;
    void timerCallback() override;
    Sat& sat();
    void renderPreview();
    juce::Rectangle<int> cardRect (int i) const;
    juce::Rectangle<int> designerArea() const;
    void setHarmonicFromMouse (juce::Point<float> p);

    juce::TextButton back { juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x90 Bridge")) };
    juce::OwnedArray<ModuleParamRow> shapeRows, stageRows;
    juce::OwnedArray<ModuleKnob> knobs;
    std::unique_ptr<juce::Component> mixKnob;
    int dragHarmonic = -1;
    unsigned lastVersion = ~0u;

    // preview (offline render of a copy of the block)
    std::vector<double> previewDbc;   // H2..H10
    double previewThd = 0.0, previewEven = 0.0, previewAlias = -200.0;
    bool previewValid = false;
};
