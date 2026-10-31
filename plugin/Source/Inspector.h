#pragma once

#include "Ui.h"

// Right-hand inspector for the current selection (block, Input or Output).
class InspectorPanel : public juce::Component, private juce::Timer
{
public:
    explicit InspectorPanel (Ui& ui);
    void rebuild();
    void showAdvanced() { advanced = true; rebuild(); }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void buildBlock (sonic::Graph::Node& n);
    void buildInput();
    void buildOutput();
    void chooseFile();

    // Mix of a block (lives on the graph node, not the module)
    class NodeKnob : public juce::Component
    {
    public:
        NodeKnob (Ui& ui, int nodeId, bool isMix);
        void resized() override;
        void paint (juce::Graphics&) override;
        void sync();
        AtelierSlider slider;
        void mouseDoubleClick (const juce::MouseEvent&) override { slider.beginTextEntry(); }
    private:
        Ui& ui;
        int id;
        bool mix;
    };

    class Waveform : public juce::Component
    {
    public:
        explicit Waveform (AtelierProcessor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
    private:
        AtelierProcessor& proc;
    };

    Ui& ui;
    bool advanced = false;
    int builtFor = -2;
    juce::String builtRelevance;   // which params were shown; a change triggers a rebuild
    juce::String relevanceKey() const;
    juce::String title, titleNote, subtitle, description;

    juce::TextButton simpleBtn { "SIMPLE" }, advancedBtn { "ADVANCED" };
    juce::OwnedArray<ModuleKnob> knobs;
    juce::OwnedArray<NodeKnob> nodeKnobs;
    juce::OwnedArray<ModuleChoice> choices;
    juce::OwnedArray<ModuleParamRow> rows;
    juce::OwnedArray<juce::Component> extras;   // labels, apvts rows, buttons
    juce::OwnedArray<juce::TextButton> buttons;
    juce::Viewport viewport;
    juce::Component list;
    std::unique_ptr<Waveform> waveform;
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<float> grHistory;
    juce::Rectangle<int> grArea;
};
