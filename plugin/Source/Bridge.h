#pragma once

#include "Ui.h"

// The bridge editor: blocks between Input and Output, connected by wires.
class BridgePanel : public juce::Component, private juce::Timer
{
public:
    explicit BridgePanel (Ui& ui);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    // Adds a block of the given type; inserts it before Output on the main path.
    void addBlock (const std::string& type);
    void addBlockAt (const std::string& type, juce::Point<float> pos);

    static constexpr int kCardW = 188, kCardH = 96;

private:
    struct Hit
    {
        enum Kind { none, node, outPort, inPort, mixRing, enableDot, wire } kind = none;
        int id = -1;
        sonic::Graph::Edge edge;
    };

    juce::Rectangle<float> cardRect (int id) const;
    juce::Point<float> inPort (int id) const;
    juce::Point<float> outPort (int id) const;
    juce::Rectangle<float> canvas() const;
    Hit hitTest (juce::Point<float> p) const;
    float wireDistance (const sonic::Graph::Edge& e, juce::Point<float> p) const;
    juce::Path wirePath (juce::Point<float> a, juce::Point<float> b) const;
    sonic::Graph::Edge nearestWire (juce::Point<float> p, float maxDist) const;
    void changed (bool structural);
    void showNodeMenu (int id);
    void timerCallback() override;
    void drawCard (juce::Graphics&, int id);
    void drawIo (juce::Graphics&, bool input);
    juce::Point<float> freeSpot (juce::Point<float> want) const;

    class Palette : public juce::Component
    {
    public:
        explicit Palette (BridgePanel& o);
        void resized() override;
        void paint (juce::Graphics&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
    private:
        bool dragging = false;
        BridgePanel& owner;
        juce::OwnedArray<juce::TextButton> chips;
        friend class BridgePanel;
    };

    Ui& ui;
    Palette palette { *this };
    Hit drag;
    juce::Point<float> dragStart, dragNow, nodeStart;
    bool moved = false;
    float mixStart = 1.0f;
    sonic::Graph::Edge selectedWire { -1, -1 };
    juce::String flash;
    int flashTicks = 0;
    juce::String pendingType;   // palette chip being dragged
};
