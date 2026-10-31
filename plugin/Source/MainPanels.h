#pragma once

#include "Ui.h"

// ------------------------------------------------------------------ header

class HeaderBar : public juce::Component, private juce::Timer
{
public:
    explicit HeaderBar (Ui& ui);
    std::function<void()> onExport;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showTemplateMenu();
    void saveTemplateDialog();
    void loadTemplate (const juce::ValueTree& bridge, const juce::String& name);
    Ui& ui;
    juce::Label study;
    juce::TextButton navButton { "BRIDGE" }, templatesButton { "TEMPLATES" };
    juce::String location;
    juce::TextButton aButton { "A  WET" }, bButton { "B  DRY" }, matchButton { "MATCH" };
    juce::TextButton undoButton { "UNDO" }, redoButton { "REDO" }, exportButton { "EXPORT \"BLUEPRINT\"" };
    std::unique_ptr<ButtonAttachment> matchAtt;
    juce::RangedAudioParameter* bypass = nullptr;
};

// ------------------------------------------------------------------ spectrum

class SpectrumPanel : public juce::Component
{
public:
    explicit SpectrumPanel (Ui& ui);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void repaintPlot() { repaint (plotArea().toNearestInt().expanded (4)); }

private:
    juce::Rectangle<float> plotArea() const;
    void drawCurve (juce::Graphics&, const std::vector<float>& s, juce::Rectangle<float> r, juce::Colour c, float w, bool fill);
    sonic::Graph::Node* overlayEq();

    Ui& ui;
    juce::TextButton inOut { "IN / OUT" }, diff { "DIFFERENCE" }, freeze { "FREEZE" };
    juce::OwnedArray<juce::TextButton> scaleButtons;
    double eqRange = 6.0;   // +- dB, 0 = auto
    bool autoScale = false;
    bool showDiff = false;
    juce::Point<float> hover { -1, -1 };
};

// ------------------------------------------------------------------ readout

class ReadoutPanel : public juce::Component
{
public:
    explicit ReadoutPanel (Ui& ui);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // Calm harmonics display: values must be valid for a few frames before they show,
    // are smoothed while shown, and are held (dimmed) for a while when the signal stops
    // being measurable, so the panel never flickers between numbers and messages.
    struct HarmonicsView
    {
        bool showing = false;
        bool held = false;
        bool estimated = false;
        double f0 = 0.0, thdPct = 0.0;
        std::array<double, 6> dbc {};
        int validStreak = 0;
        double lastValidMs = 0.0, invalidSinceMs = 0.0;
        juce::String note;
        int lastSerial = -1;
        void update (const AnalysisWorker::Results& r, double nowMs);
    };

    Ui& ui;
    HarmonicsView hv;
    juce::TextButton chainMode { "CHAIN" }, refMode { "REFERENCE" };
};
