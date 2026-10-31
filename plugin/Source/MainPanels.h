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
    Ui& ui;
    juce::Label study;
    juce::TextButton aButton { "A  PROCESSED" }, bButton { "B  DRY" }, matchButton { "GAIN MATCH" };
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
    Ui& ui;
    juce::TextButton chainMode { "CHAIN" }, refMode { "REFERENCE" };
};
