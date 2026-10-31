#pragma once

#include "Ui.h"

// The bridge editor: blocks between Input and Output, connected by wires.
//
// Blocks live in "world" coordinates; the view can be zoomed (wheel, +/-, FIT) and panned
// (drag empty space). Routing:
//   - drag from a block's right port to another block: new wire
//   - drag the left end of an existing wire (a block's input port) away: re-route it,
//     or drop it on empty space to disconnect
//   - Shift-drag a block: pull it out of its chain (its neighbours are joined back together)
//   - drop an unconnected block (or a palette chip) onto a wire: insert it there
//   - right-click a block / wire / empty space for every routing action
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
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scale) override;
    bool keyPressed (const juce::KeyPress&) override;

    // Adds a block of the given type; inserts it before Output on the main path.
    void addBlock (const std::string& type);
    // Adds a block at a screen position (inserted into a wire when dropped on one).
    void addBlockAt (const std::string& type, juce::Point<float> screenPos);
    void fitView();
    void tidyLayout();

    static constexpr int kCardW = 188, kCardH = 96;

private:
    struct Hit
    {
        enum Kind { none, node, outPort, inPort, mixRing, enableDot, wire } kind = none;
        int id = -1;
        sonic::Graph::Edge edge { -1, -1 };
    };
    enum class Drag { none, pan, moveNode, mixRing, newWireFromOut, newWireFromIn, rewire };

    // geometry (world coordinates unless named otherwise)
    juce::Rectangle<float> cardRect (int id) const;
    juce::Point<float> inPort (int id) const;
    juce::Point<float> outPort (int id) const;
    juce::Rectangle<float> canvas() const;                 // screen area of the canvas
    juce::AffineTransform view() const;                    // world -> screen
    juce::Point<float> toWorld (juce::Point<float> screen) const;
    juce::Rectangle<float> contentBounds() const;
    Hit hitTest (juce::Point<float> world) const;
    float wireDistance (const sonic::Graph::Edge& e, juce::Point<float> p) const;
    juce::Path wirePath (juce::Point<float> a, juce::Point<float> b) const;
    sonic::Graph::Edge nearestWire (juce::Point<float> world, float maxScreenDist, int ignoreNode = -1) const;
    juce::Point<float> freeSpot (juce::Point<float> want, int ignore = -1) const;
    void setZoom (float z, juce::Point<float> screenAnchor);
    void ensureVisible (int id);

    // routing helpers
    void changed (bool structural);
    void detachAndHeal (int id);          // remove a block's wires, join its neighbours
    void removeAndHeal (int id);
    void insertIntoWire (int id, sonic::Graph::Edge e);
    bool isConnected (int id) const;
    void duplicate (int id);
    void showFlash (const juce::String& text);

    void showNodeMenu (int id);
    void showWireMenu (sonic::Graph::Edge e);
    void showCanvasMenu (juce::Point<float> world);
    void timerCallback() override;
    void drawCard (juce::Graphics&, int id);
    void drawIo (juce::Graphics&, bool input);
    void drawPort (juce::Graphics&, juce::Point<float>, juce::Colour, bool hot);

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
    juce::TextButton zoomOut { "-" }, zoomIn { "+" }, fitButton { "FIT" }, tidyButton { "TIDY" };

    float zoom = 1.0f;
    juce::Point<float> pan { 0.0f, 0.0f };   // world point shown at the canvas' top-left
    bool fittedOnce = false;

    Drag dragMode = Drag::none;
    Hit pressed;
    juce::Point<float> pressScreen, nowScreen, nowWorld, nodeStart;
    juce::Point<float> panStart;
    bool moved = false;
    float mixStart = 1.0f;
    int rewireFrom = -1;                      // fixed source while re-routing a wire end
    sonic::Graph::Edge rewireOriginal { -1, -1 };
    sonic::Graph::Edge hoverWire { -1, -1 };  // wire a dragged block would be inserted into
    sonic::Graph::Edge selectedWire { -1, -1 };
    juce::String flash;
    int flashTicks = 0;
    juce::String pendingType;                 // palette chip being dragged
};
