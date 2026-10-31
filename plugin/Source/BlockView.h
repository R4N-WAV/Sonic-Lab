#pragma once
// The centre of Atelier while something is selected on the Bridge: the selected block
// (or Input / Output) with its picture and every control, right where the spectrum
// usually is. Nothing selected = the spectrum comes back.

#include "BlockEditors.h"

class BlockView : public juce::Component, private juce::Timer
{
public:
    BlockView (Ui& ui, PreviewEngine& engine);
    void showFor (int id);              // block id, Input, Output (rebuilds)
    int shownId() const { return shown; }
    // true when the shown block was replaced (undo, template, preset) and the controls must be rebuilt
    bool isStale() const;
    void analysisUpdated();
    void paint (juce::Graphics&) override;
    void resized() override;
    BlockBody* body() { return content.get(); }
    void pumpForTests();   // runs what the timers would (no message loop in headless tools)

private:
    void timerCallback() override;
    void showPresetMenu();
    void savePresetDialog();
    juce::String titleText() const;
    juce::String noteText() const;

    Ui& ui;
    PreviewEngine& engine;
    int shown = -2;
    const sonic::Module* shownModule = nullptr;
    unsigned presetVersion = 0;
    juce::String presetName;
    std::unique_ptr<BlockBody> content;
    juce::TextButton presets { "PRESETS" }, bypass { "BYPASS" }, close { "" };
    int lastSource = -1;
};
