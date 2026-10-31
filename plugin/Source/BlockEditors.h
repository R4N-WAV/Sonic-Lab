#pragma once
// The contents of the centre view when a block is selected: a picture of what the
// block does next to all of its controls. The EQ and Saturation get their own
// interactive pictures (curve with bands, harmonic designer).

#include "Figures.h"

#include <sonic/EqMatch.h>
#include <sonic/modules/ParametricEqModule.h>
#include <sonic/modules/SaturationModule.h>

// Base of everything shown under the block view's header.
class BlockBody : public juce::Component
{
public:
    virtual void sync() {}              // ~15 Hz: follow parameter changes
    virtual void analysisUpdated() {}   // new live analysis (display refresh)
};

// A row for the block's Mix or Output (they live on the bridge node, not in the module).
class NodeRow : public juce::Component, public juce::SettableTooltipClient
{
public:
    NodeRow (Ui& ui, int nodeId, bool mix);
    void sync();
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    int labelW = 112, valueW = 70;

private:
    Ui& ui;
    int id;
    bool mix;
    AtelierSlider slider;
};

// All controls of a block, grouped, in as many columns as fit (scrolls if needed).
class ControlsPanel : public BlockBody
{
public:
    // 'skip' = parameter indices drawn elsewhere (e.g. EQ bands, harmonic bars)
    ControlsPanel (Ui& ui, int nodeId, std::function<bool (int)> skip = {}, bool nodeRows = true);
    void sync() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    int preferredColumns = 0;   // 0 = automatic

private:
    struct Item { juce::String header; juce::Component* comp = nullptr; };
    Ui& ui;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<Item> items;
    juce::OwnedArray<ModuleParamRow> rows;
    juce::OwnedArray<NodeRow> nodeRows;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> headers;
};

// Picture + controls: every block without a dedicated view.
class GenericBody : public BlockBody
{
public:
    GenericBody (Ui& ui, int nodeId, PreviewEngine& engine);
    void sync() override { controls.sync(); }
    void resized() override;

private:
    BlockFigure figure;
    ControlsPanel controls;
};

// ------------------------------------------------------------------ EQ

class EqView : public BlockBody, private juce::Timer
{
public:
    EqView (Ui& ui, int nodeId);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void sync() override;
    void analysisUpdated() override;

    // for tests
    int numBands() const;
    void addBandAt (double freq, double gainDb);
    void selectAllBands();
    void deleteSelectedBands();
    bool applyMatch (const sonic::eqmatch::Spectrum& yours, const sonic::eqmatch::Spectrum& reference);

private:
    using EQ = sonic::ParametricEqModule;
    void timerCallback() override;
    EQ& eq();
    sonic::Graph::Node* node();
    juce::Rectangle<float> plot() const;
    juce::Rectangle<int> side() const;
    double range() const;
    int bandAt (juce::Point<float> p) const;
    juce::Point<float> bandPoint (int b) const;
    void selectBand (int b, bool additive = false);
    void buildBandControls();
    void bandMenu (int b);
    void canvasMenu (juce::Point<float> p);
    void showTab (int t);
    void loadReference();
    void removeBands (std::vector<int> list);
    void updateMatchStatus();

    int nodeId;
    Ui& ui;
    juce::OwnedArray<juce::TextButton> rangeButtons, tabs;
    double fixedRange = 12.0;
    bool autoRange = false;
    int tab = 0;

    // selection and dragging
    int selected = -1;                      // band whose controls are shown
    std::vector<int> multi;                 // all selected bands (contains 'selected')
    int dragBand = -1;
    bool lasso = false, pressOnEmpty = false;
    juce::Point<float> dragStart, lassoEnd;
    std::vector<std::pair<float, float>> dragOrigin;   // freq, gain per band in 'multi'
    juce::Point<float> hover { -1, -1 };

    // BAND tab
    juce::TextButton prevBand { "<" }, nextBand { ">" }, addBand { "+ ADD" }, removeBand { "REMOVE" };
    juce::OwnedArray<ModuleParamRow> bandRows;
    // EQ tab
    std::unique_ptr<ControlsPanel> eqControls;
    juce::TextButton clearAll { "REMOVE ALL BANDS" };
    // MATCH tab
    juce::TextButton refFile { "LOAD FILE..." }, refCapture { "CAPTURE" }, learnButton { "CAPTURE" },
                     applyButton { "APPLY MATCH" }, undoMatch { "REMOVE MATCH BANDS" };
    AtelierSlider amount, maxBands;
    sonic::eqmatch::Spectrum reference, yours;
    sonic::eqmatch::Fit lastFit;
    std::vector<int> matchBands;
    int capturing = 0;                      // 0 none, 1 reference, 2 yours
    juce::String refName, matchStatus;
    std::unique_ptr<juce::FileChooser> chooser;
    unsigned lastVersion = ~0u;
    int lastSerial = -1;
};

// ------------------------------------------------------------------ Saturation

class SaturationView : public BlockBody
{
public:
    SaturationView (Ui& ui, int nodeId, PreviewEngine& engine);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void sync() override;

private:
    using Sat = sonic::SaturationModule;
    Sat& sat();
    juce::Rectangle<int> designerArea() const;
    juce::Rectangle<float> column (int k) const;
    void setHarmonicFromMouse (juce::Point<float> p);

    Ui& ui;
    int nodeId;
    juce::OwnedArray<juce::TextButton> recipes;
    BlockFigure figure;
    ControlsPanel controls;
    int dragHarmonic = -1;
    unsigned lastVersion = ~0u;
};
