#include "Bridge.h"

#include <sonic/ModuleRegistry.h>
#include <sonic/modules/ParametricEqModule.h>

using sonic::Graph;

namespace
{
    constexpr int kHeader = 42;
    constexpr int kPaletteH = 46;
    constexpr float kIoW = 140.0f, kIoH = 128.0f;
    constexpr float kMidY = 180.0f;         // world y of the main signal line
    constexpr float kInputX = 16.0f;
    constexpr float kMinZoom = 0.35f, kMaxZoom = 2.0f;

    const sonic::ModuleInfo* infoFor (const std::string& type)
    {
        for (auto& m : sonic::availableModules())
            if (m.typeId == type)
                return &m;
        return nullptr;
    }

    juce::String typeLabel (const std::string& type)
    {
        if (auto* i = infoFor (type)) return juce::String (i->displayName).toUpperCase();
        return juce::String (type).toUpperCase();
    }

    juce::PopupMenu::Item withKey (juce::PopupMenu::Item i, const juce::String& key)
    {
        i.shortcutKeyDescription = key;
        return i;
    }

    juce::String stageText (sonic::Graph& g, int id)
    {
        const auto order = g.activeOrder();
        const auto it = std::find (order.begin(), order.end(), id);
        return it == order.end() ? juce::String ("--") : juce::String ((int) (it - order.begin()) + 1).paddedLeft ('0', 2);
    }

    juce::Colour typeColour (const std::string& type)
    {
        if (auto* i = infoFor (type)) return theme::categoryColour (i->category);
        return theme::textDim;
    }
}

// ------------------------------------------------------------------ palette

BridgePanel::Palette::Palette (BridgePanel& o) : owner (o)
{
    for (auto& m : sonic::availableModules())
    {
        juce::String label (m.displayName);
        if (m.typeId == "tube.stage") label = "Tube";
        else if (m.typeId == "tone.eq") label = "EQ";
        else if (m.typeId == "dynamics.compressor") label = "Comp";
        else if (m.typeId == "character.transformer") label = "Iron";
        else if (m.typeId == "character.saturation") label = "Saturate";
        auto* b = chips.add (new juce::TextButton (label));
        b->setTooltip (juce::String (m.displayName) + " - " + juce::String (m.summary)
                       + "\nClick: add to the end of the main path. Drag: place it, or drop it onto a line to insert it there.");
        const std::string type = m.typeId;
        b->onClick = [this, type] { owner.addBlock (type); };
        b->addMouseListener (this, false);
        b->getProperties().set ("type", juce::String (type));
        b->getProperties().set ("chip", true);
        b->getProperties().set ("dot", (int) theme::categoryColour (m.category).getARGB());
        addAndMakeVisible (b);
    }
}

void BridgePanel::Palette::paint (juce::Graphics& g)
{
    g.setColour (theme::textFaint);
    g.setFont (theme::mono (9.5f));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("ADD â")), getLocalBounds().withWidth (48), juce::Justification::centredLeft);
}

void BridgePanel::Palette::mouseDrag (const juce::MouseEvent& e)
{
    if (e.eventComponent == this || e.getDistanceFromDragStart() < 8)
        return;
    dragging = true;
    owner.pendingType = e.eventComponent->getProperties()["type"].toString();
    owner.nowScreen = e.getEventRelativeTo (&owner).position;
    owner.nowWorld = owner.toWorld (owner.nowScreen);
    owner.hoverWire = owner.nearestWire (owner.nowWorld, 16.0f);
    owner.setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    owner.repaint();
}

void BridgePanel::Palette::mouseUp (const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    dragging = false;
    const auto p = e.getEventRelativeTo (&owner).position;
    const auto type = owner.pendingType.toStdString();
    owner.pendingType.clear();
    owner.hoverWire = { -1, -1 };
    owner.setMouseCursor (juce::MouseCursor::NormalCursor);
    if (owner.canvas().contains (p))
        owner.addBlockAt (type, p);
    owner.repaint();
}

void BridgePanel::Palette::resized()
{
    auto r = getLocalBounds().withTrimmedLeft (52);
    int x = r.getX();
    for (auto* b : chips)
    {
        const int w = juce::GlyphArrangement::getStringWidthInt (theme::heading (15.5f), b->getButtonText()) + 20;
        b->setBounds (x, r.getCentreY() - 14, w, 28);
        x += w + 4;
    }
}

// ------------------------------------------------------------------ geometry

BridgePanel::BridgePanel (Ui& u) : ui (u)
{
    addAndMakeVisible (palette);
    for (auto* b : { &zoomOut, &zoomIn, &fitButton, &tidyButton })
    {
        b->getProperties().set ("small", true);
        addAndMakeVisible (b);
    }
    zoomOut.setTooltip ("Zoom out (or scroll / pinch on the canvas).");
    zoomIn.setTooltip ("Zoom in.");
    fitButton.setTooltip ("Fit every block into view (F).");
    tidyButton.setTooltip ("Arrange the blocks left-to-right in signal order.");
    zoomOut.onClick = [this] { setZoom (zoom / 1.2f, canvas().getCentre()); };
    zoomIn.onClick = [this] { setZoom (zoom * 1.2f, canvas().getCentre()); };
    fitButton.onClick = [this] { fitView(); };
    tidyButton.onClick = [this] { tidyLayout(); };
    setWantsKeyboardFocus (true);
    startTimerHz (15);
}

void BridgePanel::resized()
{
    palette.setBounds (getLocalBounds().removeFromBottom (kPaletteH).reduced (14, 6));
    auto h = getLocalBounds().removeFromTop (kHeader).reduced (12, 8);
    tidyButton.setBounds (h.removeFromRight (52));
    h.removeFromRight (10);
    fitButton.setBounds (h.removeFromRight (42));
    h.removeFromRight (4);
    zoomIn.setBounds (h.removeFromRight (26));
    h.removeFromRight (48);   // zoom % is painted here
    zoomOut.setBounds (h.removeFromRight (26));
    if (! fittedOnce && getWidth() > 0)
    {
        fittedOnce = true;
        fitView();
    }
}

juce::Rectangle<float> BridgePanel::canvas() const
{
    return getLocalBounds().toFloat().withTrimmedTop ((float) kHeader).withTrimmedBottom ((float) kPaletteH).reduced (1.0f, 0.0f);
}

juce::AffineTransform BridgePanel::view() const
{
    const auto c = canvas();
    return juce::AffineTransform::translation (-pan.x, -pan.y).scaled (zoom).translated (c.getX(), c.getY());
}

juce::Point<float> BridgePanel::toWorld (juce::Point<float> s) const
{
    return s.transformedBy (view().inverted());
}

juce::Rectangle<float> BridgePanel::cardRect (int id) const
{
    if (id == Graph::kInputId)
        return { kInputX, kMidY - kIoH * 0.5f, kIoW, kIoH };
    if (id == Graph::kOutputId)
    {
        // Output sits to the right of the right-most block (and never closer than the default spot)
        float right = 1064.0f;
        for (int n : ui.proc.graph().nodeIds())
            if (auto* nd = ui.proc.graph().node (n))
                right = juce::jmax (right, nd->x + (float) kCardW + 70.0f);
        return { right, kMidY - kIoH * 0.5f, kIoW, kIoH };
    }
    if (auto* n = ui.proc.graph().node (id))
        return { n->x, n->y, (float) kCardW, (float) kCardH };
    return {};
}

juce::Point<float> BridgePanel::inPort (int id) const
{
    const auto r = cardRect (id);
    return { r.getX(), r.getCentreY() };
}

juce::Point<float> BridgePanel::outPort (int id) const
{
    const auto r = cardRect (id);
    return { r.getRight(), r.getCentreY() };
}

juce::Rectangle<float> BridgePanel::contentBounds() const
{
    auto b = cardRect (Graph::kInputId).getUnion (cardRect (Graph::kOutputId));
    for (int id : ui.proc.graph().nodeIds())
        b = b.getUnion (cardRect (id));
    return b.expanded (24.0f, 30.0f);
}

void BridgePanel::setZoom (float z, juce::Point<float> anchor)
{
    const auto before = toWorld (anchor);
    zoom = juce::jlimit (kMinZoom, kMaxZoom, z);
    const auto c = canvas();
    pan = before - (anchor - c.getPosition()) / zoom;
    repaint();
}

void BridgePanel::fitView()
{
    const auto c = canvas();
    if (c.isEmpty()) return;
    const auto b = contentBounds();
    zoom = juce::jlimit (kMinZoom, 1.0f, juce::jmin (c.getWidth() / b.getWidth(), c.getHeight() / b.getHeight()));
    pan = { b.getCentreX() - c.getWidth() * 0.5f / zoom, b.getCentreY() - c.getHeight() * 0.5f / zoom };
    repaint();
}

void BridgePanel::ensureVisible (int id)
{
    const auto c = canvas();
    const auto r = cardRect (id).transformedBy (view());
    if (c.reduced (10.0f).contains (r))
        return;
    if (contentBounds().getWidth() * zoom > c.getWidth() || contentBounds().getHeight() * zoom > c.getHeight())
        fitView();
    else
    {
        const auto w = cardRect (id).getCentre();
        pan = { w.x - c.getWidth() * 0.5f / zoom, w.y - c.getHeight() * 0.5f / zoom };
        repaint();
    }
}

juce::Path BridgePanel::wirePath (juce::Point<float> a, juce::Point<float> b) const
{
    juce::Path p;
    const float dx = juce::jmax (40.0f, std::abs (b.x - a.x) * 0.45f);
    p.startNewSubPath (a);
    p.cubicTo (a.x + dx, a.y, b.x - dx, b.y, b.x, b.y);
    return p;
}

float BridgePanel::wireDistance (const Graph::Edge& e, juce::Point<float> p) const
{
    const auto path = wirePath (outPort (e.from), inPort (e.to));
    juce::Point<float> nearest;
    path.getNearestPoint (p, nearest);
    return nearest.getDistanceFrom (p);
}

Graph::Edge BridgePanel::nearestWire (juce::Point<float> p, float maxScreenDist, int ignoreNode) const
{
    Graph::Edge best { -1, -1 };
    float bestD = maxScreenDist / zoom;
    for (auto& e : ui.proc.graph().edges())
    {
        if (e.from == ignoreNode || e.to == ignoreNode) continue;
        const float d = wireDistance (e, p);
        if (d < bestD)
        {
            bestD = d;
            best = e;
        }
    }
    return best;
}

BridgePanel::Hit BridgePanel::hitTest (juce::Point<float> p) const
{
    Hit h;
    auto& g = ui.proc.graph();
    auto ids = g.nodeIds();
    ids.push_back (Graph::kInputId);
    ids.push_back (Graph::kOutputId);
    const float portR = 11.0f / juce::jmin (1.0f, zoom);
    for (auto it = ids.rbegin(); it != ids.rend(); ++it)
    {
        const int id = *it;
        if (id != Graph::kOutputId && outPort (id).getDistanceFrom (p) < portR) { h.kind = Hit::outPort; h.id = id; return h; }
        if (id != Graph::kInputId && inPort (id).getDistanceFrom (p) < portR) { h.kind = Hit::inPort; h.id = id; return h; }
    }
    for (auto it = ids.rbegin(); it != ids.rend(); ++it)
    {
        const int id = *it;
        const auto r = cardRect (id);
        if (! r.contains (p))
            continue;
        h.id = id;
        h.kind = Hit::node;
        if (id >= 2)
        {
            if (juce::Rectangle<float> (r.getRight() - 24, r.getY() + 4, 20, 20).contains (p)) h.kind = Hit::enableDot;
            else if (juce::Rectangle<float> (r.getX() + 8, r.getBottom() - 36, 30, 30).contains (p)) h.kind = Hit::mixRing;
        }
        return h;
    }
    const auto e = nearestWire (p, 7.0f);
    if (e.from >= 0)
    {
        h.kind = Hit::wire;
        h.edge = e;
    }
    return h;
}

juce::Point<float> BridgePanel::freeSpot (juce::Point<float> want, int ignore) const
{
    for (int attempt = 0; attempt < 24; ++attempt)
    {
        bool clash = false;
        const juce::Rectangle<float> r (want.x, want.y, (float) kCardW, (float) kCardH);
        for (int id : ui.proc.graph().nodeIds())
            if (id != ignore && cardRect (id).expanded (10.0f).intersects (r))
                clash = true;
        if (cardRect (Graph::kInputId).expanded (10.0f).intersects (r)) clash = true;
        if (! clash) break;
        // try below, then above, alternating outward
        want.y += (attempt % 2 == 0 ? 1.0f : -1.0f) * (kCardH + 18.0f) * (float) (attempt + 1);
    }
    return want;
}

// ------------------------------------------------------------------ editing

void BridgePanel::changed (bool structural)
{
    ui.proc.checkpoint();
    if (structural && ui.onBridgeChanged)
        ui.onBridgeChanged();
    repaint();
}

void BridgePanel::showFlash (const juce::String& text)
{
    flash = text;
    flashTicks = 45;
    repaint();
}

bool BridgePanel::isConnected (int id) const
{
    for (auto& e : ui.proc.graph().edges())
        if (e.from == id || e.to == id)
            return true;
    return false;
}

void BridgePanel::detachAndHeal (int id)
{
    auto& g = ui.proc.graph();
    std::vector<int> ins, outs;
    for (auto& e : g.edges())
    {
        if (e.to == id) ins.push_back (e.from);
        if (e.from == id) outs.push_back (e.to);
    }
    for (int a : ins) g.disconnect (a, id);
    for (int b : outs) g.disconnect (id, b);
    for (int a : ins)
        for (int b : outs)
            g.connect (a, b);   // silently skips duplicates
}

void BridgePanel::removeAndHeal (int id)
{
    detachAndHeal (id);
    ui.proc.graph().removeNode (id);
}

void BridgePanel::insertIntoWire (int id, Graph::Edge e)
{
    auto& g = ui.proc.graph();
    if (e.from < 0 || e.from == id || e.to == id) return;
    g.disconnect (e.from, e.to);
    if (! g.connect (e.from, id) || ! g.connect (id, e.to))
    {
        g.disconnect (e.from, id);
        g.disconnect (id, e.to);
        g.connect (e.from, e.to);
        showFlash ("That would create a loop - feedback loops are not allowed.");
    }
}

void BridgePanel::addBlock (const std::string& type)
{
    auto& g = ui.proc.graph();
    // insert before Output on the main path when there is exactly one wire into Output
    std::vector<Graph::Edge> intoOut;
    for (auto& e : g.edges())
        if (e.to == Graph::kOutputId)
            intoOut.push_back (e);

    juce::Point<float> pos { 220.0f, kMidY - kCardH * 0.5f };
    if (intoOut.size() == 1)
    {
        const auto a = outPort (intoOut[0].from);
        pos = { a.x + 44.0f, a.y - kCardH * 0.5f };
    }
    pos = freeSpot (pos);
    const int id = g.addNode (type, pos.x, pos.y);
    if (id < 0)
        return;
    if (intoOut.size() == 1)
    {
        g.disconnect (intoOut[0].from, Graph::kOutputId);
        g.connect (intoOut[0].from, id);
        g.connect (id, Graph::kOutputId);
    }
    else if (intoOut.empty())
    {
        g.connect (Graph::kInputId, id);
        g.connect (id, Graph::kOutputId);
    }
    ui.select (id);
    changed (true);
    ensureVisible (Graph::kOutputId);
}

void BridgePanel::addBlockAt (const std::string& type, juce::Point<float> screen)
{
    auto& g = ui.proc.graph();
    const auto w = toWorld (screen);
    const auto wire = nearestWire (w, 16.0f);
    const auto spot = freeSpot ({ w.x - kCardW * 0.5f, w.y - kCardH * 0.5f });
    const int id = g.addNode (type, spot.x, spot.y);
    if (id < 0)
        return;
    if (wire.from >= 0)
        insertIntoWire (id, wire);
    ui.select (id);
    changed (true);
}

void BridgePanel::duplicate (int id)
{
    auto& gr = ui.proc.graph();
    auto* node = gr.node (id);
    if (node == nullptr) return;
    const auto spot = freeSpot ({ node->x + 24.0f, node->y + kCardH + 18.0f });
    const int copy = gr.addNode (node->type, spot.x, spot.y);
    if (auto* c = gr.node (copy))
    {
        for (int i = 0; i < node->module->numParams(); ++i)
            c->module->setParam (i, node->module->getParam (i));
        c->mix = node->mix.load();
        c->outGainDb = node->outGainDb.load();
        c->name = node->name + " copy";
        ui.select (copy);
    }
    changed (true);
}

void BridgePanel::tidyLayout()
{
    auto& g = ui.proc.graph();
    const auto ids = g.nodeIds();
    const auto edges = g.edges();
    // depth = longest path from Input (blocks with no path get their own column at the end)
    std::map<int, int> depth;
    depth[Graph::kInputId] = 0;
    for (size_t pass = 0; pass < ids.size() + 2; ++pass)
        for (auto& e : edges)
            if (depth.count (e.from) && e.to != Graph::kOutputId)
                depth[e.to] = juce::jmax (depth.count (e.to) ? depth[e.to] : 0, depth[e.from] + 1);
    int maxDepth = 0;
    for (auto& [id, d] : depth) maxDepth = juce::jmax (maxDepth, d);
    std::map<int, std::vector<int>> columns;
    for (int id : ids)
        columns[depth.count (id) ? depth[id] : maxDepth + 1].push_back (id);
    for (auto& [col, members] : columns)
    {
        const float x = 210.0f + (float) (col - 1) * ((float) kCardW + 56.0f);
        const float total = (float) members.size() * (kCardH + 22.0f) - 22.0f;
        float y = kMidY - total * 0.5f;
        for (int id : members)
            if (auto* n = g.node (id)) { n->x = x; n->y = y; y += kCardH + 22.0f; }
    }
    changed (false);
    fitView();
}

// ------------------------------------------------------------------ menus

void BridgePanel::showNodeMenu (int id)
{
    auto& g = ui.proc.graph();
    auto* n = g.node (id);
    if (n == nullptr)
        return;
    const bool hasEditor = n->type == "tone.eq" || n->type == "character.saturation";
    const bool connected = isConnected (id);
    bool hasIn = false, hasOut = false;
    for (auto& e : g.edges()) { hasIn |= e.to == id; hasOut |= e.from == id; }

    juce::PopupMenu m;
    m.addSectionHeader (stageText (g, id) + "  " + theme::q (typeLabel (n->type).toLowerCase()));
    m.addItem (withKey (juce::PopupMenu::Item (hasEditor ? "Open editor" : "Show all controls").setID (1), "Double-click"));
    m.addItem (withKey (juce::PopupMenu::Item (n->enabled.load() ? "Bypass" : "Enable").setID (4), "B"));
    m.addItem (withKey (juce::PopupMenu::Item ("Duplicate").setID (3), "Ctrl D"));
    m.addItem (juce::PopupMenu::Item ("Rename...").setID (2));
    m.addSeparator();
    m.addItem (withKey (juce::PopupMenu::Item ("Pull out of the chain").setID (10).setEnabled (connected), "Shift-drag"));
    m.addItem (juce::PopupMenu::Item ("Disconnect inputs").setID (11).setEnabled (hasIn));
    m.addItem (juce::PopupMenu::Item ("Disconnect outputs").setID (12).setEnabled (hasOut));
    m.addItem (juce::PopupMenu::Item ("Send to Output too (parallel)").setID (13).setEnabled (! g.wouldCreateCycle (id, Graph::kOutputId)));
    m.addItem (juce::PopupMenu::Item ("Feed from Input too (parallel)").setID (14).setEnabled (! g.wouldCreateCycle (Graph::kInputId, id)));
    m.addSeparator();
    m.addItem (withKey (juce::PopupMenu::Item ("Remove").setID (5).setColour (theme::danger), "Del"));

    juce::Component::SafePointer<BridgePanel> safe (this);
    m.showMenuAsync (ui.menuOptions(), [safe, id] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& ui = safe->ui;
        auto& gr = ui.proc.graph();
        auto* node = gr.node (id);
        if (node == nullptr) return;
        switch (r)
        {
            case 1: if (ui.openFullEditor) ui.openFullEditor (id); break;
            case 2:
            {
                auto* w = new juce::AlertWindow ("Rename block", "", juce::MessageBoxIconType::NoIcon);
                w->setLookAndFeel (&dialogLookAndFeel());
                w->addTextEditor ("name", juce::String (node->name));
                w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
                w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, id, w] (int res)
                {
                    if (safe != nullptr && res == 1)
                        if (auto* nn = safe->ui.proc.graph().node (id))
                        {
                            nn->name = w->getTextEditorContents ("name").trim().toStdString();
                            safe->changed (true);
                        }
                }), true);
                break;
            }
            case 3: safe->duplicate (id); break;
            case 4: node->enabled = ! node->enabled.load(); safe->changed (false); break;
            case 5: safe->removeAndHeal (id); ui.select (-1); safe->changed (true); break;
            case 10: safe->detachAndHeal (id); safe->changed (true); break;
            case 11: for (auto& e : gr.edges()) if (e.to == id) gr.disconnect (e.from, id); safe->changed (true); break;
            case 12: for (auto& e : gr.edges()) if (e.from == id) gr.disconnect (id, e.to); safe->changed (true); break;
            case 13: gr.connect (id, Graph::kOutputId); safe->changed (true); break;
            case 14: gr.connect (Graph::kInputId, id); safe->changed (true); break;
            default: break;
        }
    });
}

void BridgePanel::showWireMenu (Graph::Edge e)
{
    juce::PopupMenu m;
    m.addSectionHeader (theme::q ("connection"));
    juce::PopupMenu insert;
    std::string lastCat;
    int idx = 0;
    for (auto& info : sonic::availableModules())
    {
        if (info.category != lastCat && ! lastCat.empty()) insert.addSeparator();
        lastCat = info.category;
        insert.addItem (juce::PopupMenu::Item (juce::String (info.displayName)).setID (100 + idx++).setColour (theme::categoryColour (info.category)));
    }
    m.addSubMenu ("Insert a block here", insert);
    m.addSeparator();
    m.addItem (withKey (juce::PopupMenu::Item ("Remove connection").setID (1).setColour (theme::danger), "Del"));
    juce::Component::SafePointer<BridgePanel> safe (this);
    m.showMenuAsync (ui.menuOptions(), [safe, e] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& g = safe->ui.proc.graph();
        if (r == 1)
        {
            g.disconnect (e.from, e.to);
            safe->changed (true);
        }
        else if (r >= 100)
        {
            const auto& info = sonic::availableModules()[(size_t) (r - 100)];
            const auto a = safe->outPort (e.from), b = safe->inPort (e.to);
            const auto spot = safe->freeSpot ({ (a.x + b.x) * 0.5f - kCardW * 0.5f, (a.y + b.y) * 0.5f - kCardH * 0.5f });
            const int id = g.addNode (info.typeId, spot.x, spot.y);
            if (id >= 0)
            {
                safe->insertIntoWire (id, e);
                safe->ui.select (id);
                safe->changed (true);
            }
        }
    });
}

void BridgePanel::showCanvasMenu (juce::Point<float> world)
{
    juce::PopupMenu m;
    m.addSectionHeader (theme::q ("bridge"));
    juce::PopupMenu add;
    std::string lastCat;
    int idx = 0;
    for (auto& info : sonic::availableModules())
    {
        if (info.category != lastCat && ! lastCat.empty()) add.addSeparator();
        lastCat = info.category;
        add.addItem (juce::PopupMenu::Item (juce::String (info.displayName)).setID (100 + idx++).setColour (theme::categoryColour (info.category)));
    }
    m.addSubMenu ("Add a block here", add);
    m.addSeparator();
    m.addItem (withKey (juce::PopupMenu::Item ("Fit view").setID (1), "F"));
    m.addItem (juce::PopupMenu::Item ("Tidy layout").setID (2));
    m.addItem (withKey (juce::PopupMenu::Item ("Reset zoom to 100 %").setID (3), "Ctrl 0"));
    juce::Component::SafePointer<BridgePanel> safe (this);
    m.showMenuAsync (ui.menuOptions(), [safe, world] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r == 1) safe->fitView();
        else if (r == 2) safe->tidyLayout();
        else if (r == 3) safe->setZoom (1.0f, safe->canvas().getCentre());
        else if (r >= 100)
        {
            const auto& info = sonic::availableModules()[(size_t) (r - 100)];
            safe->addBlockAt (info.typeId, world.transformedBy (safe->view()));
        }
    });
}

// ------------------------------------------------------------------ mouse

void BridgePanel::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    pressScreen = nowScreen = e.position;
    nowWorld = toWorld (e.position);
    moved = false;
    dragMode = Drag::none;
    if (! canvas().contains (e.position))
        return;
    pressed = hitTest (nowWorld);

    if (e.mods.isPopupMenu())
    {
        if (pressed.kind == Hit::wire) { selectedWire = pressed.edge; showWireMenu (pressed.edge); }
        else if (pressed.id >= 2 && pressed.kind != Hit::wire) { ui.select (pressed.id); showNodeMenu (pressed.id); }
        else if (pressed.kind == Hit::none) showCanvasMenu (nowWorld);
        pressed = {};
        repaint();
        return;
    }
    if (e.mods.isMiddleButtonDown())
    {
        dragMode = Drag::pan;
        panStart = pan;
        return;
    }

    selectedWire = { -1, -1 };
    switch (pressed.kind)
    {
        case Hit::node:
        case Hit::mixRing:
            ui.select (pressed.id);
            if (auto* n = ui.proc.graph().node (pressed.id))
            {
                nodeStart = { n->x, n->y };
                mixStart = n->mix.load();
                dragMode = pressed.kind == Hit::mixRing ? Drag::mixRing : Drag::moveNode;
            }
            break;
        case Hit::enableDot:
            if (auto* n = ui.proc.graph().node (pressed.id))
            {
                n->enabled = ! n->enabled.load();
                changed (false);
            }
            break;
        case Hit::wire:
            selectedWire = pressed.edge;
            break;
        case Hit::outPort:
            dragMode = Drag::newWireFromOut;
            break;
        case Hit::inPort:
        {
            // grabbing the end of an existing wire re-routes it; an empty input starts a new wire backwards
            Graph::Edge grab { -1, -1 };
            for (auto& ed : ui.proc.graph().edges())
                if (ed.to == pressed.id) grab = ed;
            if (grab.from >= 0 && ! e.mods.isAltDown())
            {
                dragMode = Drag::rewire;
                rewireFrom = grab.from;
                rewireOriginal = grab;
                ui.proc.graph().disconnect (grab.from, grab.to);
            }
            else
                dragMode = Drag::newWireFromIn;
            break;
        }
        case Hit::none:
        default:
            ui.select (-1);
            dragMode = Drag::pan;
            panStart = pan;
            break;
    }
    repaint();
}

void BridgePanel::mouseDrag (const juce::MouseEvent& e)
{
    nowScreen = e.position;
    nowWorld = toWorld (e.position);
    if (nowScreen.getDistanceFrom (pressScreen) > 3.0f)
        moved = true;
    switch (dragMode)
    {
        case Drag::pan:
            pan = panStart - (nowScreen - pressScreen) / zoom;
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            break;
        case Drag::moveNode:
            if (auto* n = ui.proc.graph().node (pressed.id))
            {
                n->x = nodeStart.x + (nowScreen.x - pressScreen.x) / zoom;
                n->y = nodeStart.y + (nowScreen.y - pressScreen.y) / zoom;
                // an unconnected block (or Shift = pulled out) can be dropped onto a wire
                const bool canInsert = ! isConnected (pressed.id) || e.mods.isShiftDown();
                hoverWire = canInsert ? nearestWire (cardRect (pressed.id).getCentre(), 18.0f, pressed.id) : Graph::Edge { -1, -1 };
            }
            break;
        case Drag::mixRing:
            if (auto* n = ui.proc.graph().node (pressed.id))
                n->mix = juce::jlimit (0.0f, 1.0f, mixStart - (nowScreen.y - pressScreen.y) / 150.0f);
            break;
        default: break;
    }
    repaint();
}

void BridgePanel::mouseUp (const juce::MouseEvent& e)
{
    auto& g = ui.proc.graph();
    const auto target = hitTest (toWorld (e.position));
    auto targetIsBlock = [&] { return target.kind == Hit::node || target.kind == Hit::mixRing || target.kind == Hit::enableDot; };

    switch (dragMode)
    {
        case Drag::newWireFromOut:
        case Drag::newWireFromIn:
        {
            int from = -1, to = -1;
            if (dragMode == Drag::newWireFromOut)
            {
                from = pressed.id;
                if (target.kind == Hit::inPort || targetIsBlock()) to = target.id;
            }
            else
            {
                to = pressed.id;
                if (target.kind == Hit::outPort || targetIsBlock()) from = target.id;
            }
            if (from >= 0 && to >= 0 && from != to && from != Graph::kOutputId && to != Graph::kInputId)
            {
                if (g.connect (from, to)) changed (true);
                else showFlash (g.wouldCreateCycle (from, to) ? "That connection would create a loop - feedback loops are not allowed."
                                                              : "Those blocks are already connected.");
            }
            break;
        }
        case Drag::rewire:
        {
            int to = -1;
            if (target.kind == Hit::inPort || targetIsBlock()) to = target.id;
            if (to == rewireOriginal.to || ! moved)
                g.connect (rewireOriginal.from, rewireOriginal.to);           // dropped back: nothing changes
            else if (to >= 0 && to != Graph::kInputId && to != rewireFrom)
            {
                if (g.connect (rewireFrom, to)) changed (true);
                else
                {
                    g.connect (rewireOriginal.from, rewireOriginal.to);
                    showFlash (g.wouldCreateCycle (rewireFrom, to) ? "That would create a loop - feedback loops are not allowed."
                                                                   : "Those blocks are already connected.");
                }
            }
            else
            {
                changed (true);                                               // dropped on empty space: disconnected
                showFlash ("Connection removed. Undo (Ctrl Z) brings it back.");
            }
            rewireFrom = -1;
            break;
        }
        case Drag::moveNode:
            if (moved)
            {
                const int id = pressed.id;
                if (e.mods.isShiftDown() && isConnected (id))
                {
                    detachAndHeal (id);
                    if (hoverWire.from >= 0) insertIntoWire (id, hoverWire);
                    changed (true);
                }
                else if (hoverWire.from >= 0)
                {
                    insertIntoWire (id, hoverWire);
                    changed (true);
                }
                else
                    changed (false);
            }
            break;
        case Drag::mixRing:
            if (moved) changed (false);
            break;
        case Drag::pan:
            setMouseCursor (juce::MouseCursor::NormalCursor);
            break;
        default: break;
    }
    dragMode = Drag::none;
    pressed = {};
    hoverWire = { -1, -1 };
    repaint();
}

void BridgePanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto h = hitTest (toWorld (e.position));
    if (h.kind == Hit::mixRing)
    {
        if (auto* n = ui.proc.graph().node (h.id))
        {
            n->mix = 1.0f;
            changed (false);
        }
    }
    else if (h.kind == Hit::node && h.id >= 2 && ui.openFullEditor)
        ui.openFullEditor (h.id);
    else if (h.kind == Hit::none && canvas().contains (e.position))
        fitView();
}

void BridgePanel::mouseMove (const juce::MouseEvent& e)
{
    if (! canvas().contains (e.position)) { setMouseCursor (juce::MouseCursor::NormalCursor); return; }
    const auto h = hitTest (toWorld (e.position));
    setMouseCursor (h.kind == Hit::outPort || h.kind == Hit::inPort ? juce::MouseCursor::CrosshairCursor
                    : h.kind == Hit::mixRing ? juce::MouseCursor::UpDownResizeCursor
                    : h.kind == Hit::node ? juce::MouseCursor::DraggingHandCursor
                    : h.kind == Hit::wire || h.kind == Hit::enableDot ? juce::MouseCursor::PointingHandCursor
                    : juce::MouseCursor::NormalCursor);
}

void BridgePanel::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! canvas().contains (e.position)) return;
    if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        // horizontal scroll pans
        pan.x -= (w.deltaX != 0.0f ? w.deltaX : w.deltaY) * 300.0f / zoom;
        repaint();
        return;
    }
    setZoom (zoom * std::pow (1.0f + 0.9f * std::abs (w.deltaY), w.deltaY > 0 ? 1.0f : -1.0f), e.position);
}

void BridgePanel::mouseMagnify (const juce::MouseEvent& e, float scale)
{
    setZoom (zoom * scale, e.position);
}

bool BridgePanel::keyPressed (const juce::KeyPress& k)
{
    auto& g = ui.proc.graph();
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
    {
        if (selectedWire.from >= 0)
        {
            g.disconnect (selectedWire.from, selectedWire.to);
            selectedWire = { -1, -1 };
            changed (true);
            return true;
        }
        if (ui.selected >= 2)
        {
            removeAndHeal (ui.selected);
            ui.select (-1);
            changed (true);
            return true;
        }
    }
    if (k == juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0) && ui.selected >= 2) { duplicate (ui.selected); return true; }
    if (k == juce::KeyPress ('b', 0, 0) && ui.selected >= 2)
        if (auto* n = g.node (ui.selected)) { n->enabled = ! n->enabled.load(); changed (false); return true; }
    if (k == juce::KeyPress ('f', 0, 0)) { fitView(); return true; }
    if (k == juce::KeyPress ('0', juce::ModifierKeys::commandModifier, 0)) { setZoom (1.0f, canvas().getCentre()); return true; }
    if (k == juce::KeyPress ('=', juce::ModifierKeys::commandModifier, 0) || k == juce::KeyPress ('+', juce::ModifierKeys::commandModifier, 0)) { setZoom (zoom * 1.2f, canvas().getCentre()); return true; }
    if (k == juce::KeyPress ('-', juce::ModifierKeys::commandModifier, 0)) { setZoom (zoom / 1.2f, canvas().getCentre()); return true; }
    return false;
}

void BridgePanel::timerCallback()
{
    if (flashTicks > 0 && --flashTicks == 0)
        flash.clear();
    repaint();
}

// ------------------------------------------------------------------ painting

void BridgePanel::drawPort (juce::Graphics& g, juce::Point<float> p, juce::Colour c, bool hot)
{
    const float r = hot ? 6.0f : 4.5f;
    g.setColour (theme::bg);
    g.fillEllipse (p.x - r, p.y - r, r * 2, r * 2);
    g.setColour (c);
    g.drawEllipse (p.x - r, p.y - r, r * 2, r * 2, 1.2f);
}

void BridgePanel::drawIo (juce::Graphics& g, bool input)
{
    const int id = input ? Graph::kInputId : Graph::kOutputId;
    auto r = cardRect (id);
    const bool sel = ui.selected == id;
    g.setColour (theme::bg);
    g.fillRect (r);
    g.setColour (sel ? theme::accent : theme::ink);
    g.drawRect (r, sel ? 1.5f : 1.0f);
    auto t = r.reduced (12.0f, 10.0f);
    g.setColour (theme::accent);
    g.setFont (theme::mono (9.0f));
    g.drawText (input ? "INPUT" : "OUTPUT", t.removeFromTop (14.0f), juce::Justification::centredLeft);
    g.setColour (theme::ink);
    g.setFont (theme::heading (21.0f));
    juce::String title = "To DAW";
    if (input)
    {
        const auto src = ui.proc.source();
        title = src == AtelierProcessor::Source::generator ? "Generator" : src == AtelierProcessor::Source::file ? "Audio file" : "DAW track";
    }
    g.drawText (title, t.removeFromTop (26.0f), juce::Justification::centredLeft);
    g.setFont (theme::mono (9.0f));
    g.setColour (theme::textDim);
    if (input)
    {
        const char* names[] = { "DAW", "GEN", "FILE" };
        juce::String line;
        for (int i = 0; i < 3; ++i)
            line << ((int) ui.proc.source() == i ? juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8f ")) : juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8b "))) << names[i] << "  ";
        g.drawText (line, t.removeFromTop (14.0f), juce::Justification::centredLeft);
    }
    else
    {
        g.drawText ("TRIM " + ui.proc.apvts.getParameter ("io.out_trim_db")->getCurrentValueAsText().toUpperCase(), t.removeFromTop (14.0f), juce::Justification::centredLeft);
        g.drawText (ui.proc.isBypassed() ? "LISTENING: B (DRY)" : "LISTENING: A", t.removeFromTop (14.0f), juce::Justification::centredLeft);
    }
    auto& m = input ? ui.proc.inputMeter : ui.proc.outputMeter;
    auto meter = r.reduced (12.0f, 0).withY (r.getBottom() - 13.0f).withHeight (4.0f);
    g.setColour (theme::grid);
    g.fillRect (meter);
    const float lvl = juce::jmax (m.rms[0].load(), m.rms[1].load()) * 1.414f;
    const float db = juce::jlimit (-60.0f, 0.0f, juce::Decibels::gainToDecibels (lvl, -60.0f));
    g.setColour (lvl >= 1.0f ? theme::oxblood : theme::ink);
    g.fillRect (meter.withWidth (meter.getWidth() * (db + 60.0f) / 60.0f));
}

void BridgePanel::drawCard (juce::Graphics& g, int id)
{
    auto* n = ui.proc.graph().node (id);
    if (n == nullptr)
        return;
    const auto r = cardRect (id);
    const bool sel = ui.selected == id;
    const bool on = n->enabled.load();
    const bool loose = ! isConnected (id);
    const float alpha = on ? 1.0f : 0.45f;

    theme::drawCard (g, r, theme::ink, sel, juce::Colours::transparentBlack, loose);

    auto t = r.reduced (12.0f, 10.0f);
    auto top = t.removeFromTop (13.0f);
    g.setColour (theme::textDim.withMultipliedAlpha (alpha));
    g.setFont (theme::mono (9.0f));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("N\xc2\xb0")) + stageText (ui.proc.graph(), id) + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + typeLabel (n->type),
                top, juce::Justification::centredLeft, true);
    // enable dot: filled = on, hollow = bypassed
    const auto dot = juce::Rectangle<float> (r.getRight() - 18.0f, r.getY() + 10.0f, 7.0f, 7.0f);
    g.setColour (theme::ink);
    if (on) g.fillEllipse (dot); else g.drawEllipse (dot, 1.0f);

    t.removeFromTop (2.0f);
    g.setColour (theme::ink.withMultipliedAlpha (alpha));
    g.setFont (theme::heading (22.0f));
    g.drawText (juce::String (n->name), t.removeFromTop (26.0f), juce::Justification::centredLeft, true);

    // bottom row: mix ring + info
    auto bottom = juce::Rectangle<float> (r.getX() + 10.0f, r.getBottom() - 34.0f, r.getWidth() - 18.0f, 28.0f);
    auto ring = bottom.removeFromLeft (26.0f).reduced (3.0f);
    const float mix = n->mix.load();
    const float a0 = juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 2.75f;
    juce::Path val;
    val.addCentredArc (ring.getCentreX(), ring.getCentreY(), 8.0f, 8.0f, 0.0f, a0, a0 + mix * (a1 - a0), true);
    g.setColour (theme::edge);
    g.drawEllipse (ring.getCentreX() - 8.0f, ring.getCentreY() - 8.0f, 16.0f, 16.0f, 1.0f);
    g.setColour (theme::accent.withMultipliedAlpha (alpha));
    g.strokePath (val, juce::PathStrokeType (2.0f));
    g.setColour (theme::textDim.withMultipliedAlpha (alpha));
    g.setFont (theme::mono (9.5f));
    g.drawText ("MIX " + juce::String (juce::roundToInt (mix * 100)), bottom.removeFromLeft (56.0f).withTrimmedLeft (4.0f), juce::Justification::centredLeft);

    auto info = bottom;
    if (! on)
        g.drawText ("BYPASSED", info, juce::Justification::centredRight);
    else if (loose)
    {
        g.setColour (theme::oxblood);
        g.drawText ("NOT CONNECTED", info, juce::Justification::centredRight);
    }
    else if (n->type == "dynamics.compressor")
    {
        const float gr = n->module->meter (0);
        auto bar = info.withSizeKeepingCentre (info.getWidth() - 6.0f, 5.0f);
        g.setColour (theme::grid);
        g.fillRect (bar);
        const float w = juce::jlimit (0.0f, 1.0f, -gr / 20.0f) * bar.getWidth();
        g.setColour (theme::oxblood);
        g.fillRect (bar.withLeft (bar.getRight() - w));
    }
    else if (n->type == "tone.eq")
    {
        auto& eq = static_cast<sonic::ParametricEqModule&> (*n->module);
        juce::Path p;
        for (int i = 0; i <= 40; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, i / 40.0);
            const float x = info.getX() + info.getWidth() * (float) i / 40.0f;
            const float y = info.getCentreY() - (float) juce::jlimit (-12.0, 12.0, eq.responseDb (f)) / 12.0f * info.getHeight() * 0.45f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (theme::edge);
        g.drawHorizontalLine ((int) info.getCentreY(), info.getX(), info.getRight());
        g.setColour (theme::accent);
        g.strokePath (p, juce::PathStrokeType (1.3f));
    }
    else
    {
        for (int i = 0; i < n->module->numParams(); ++i)
        {
            const auto& s = n->module->params()[(size_t) i];
            if (s.simple)
            {
                g.drawText (juce::String (s.name).toUpperCase() + " " + formatParam (s, n->module->getParam (i)).toUpperCase(),
                            info, juce::Justification::centredRight, true);
                break;
            }
        }
    }

    drawPort (g, inPort (id), theme::ink, false);
    drawPort (g, outPort (id), theme::ink, false);
}

void BridgePanel::paint (juce::Graphics& g)
{
    theme::drawPanel (g, getLocalBounds(), "Bridge", juce::String (juce::CharPointer_UTF8 ("Fig. 02 \xe2\x80\x94 signal path")));
    {
        auto hint = getLocalBounds().withHeight (kHeader).reduced (16, 0).withTrimmedRight (236).withTrimmedLeft (330);
        g.setColour (theme::textFaint);
        g.setFont (theme::sans (12.0f));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("Scroll to zoom \xc2\xb7 drag a wire's end to re-route \xc2\xb7 Shift-drag pulls a block out")),
                    hint, juce::Justification::centredLeft, true);
        g.setColour (theme::textDim);
        g.setFont (theme::mono (10.0f));
        g.drawText (juce::String (juce::roundToInt (zoom * 100)) + " %", zoomOut.getBounds().translated (26, 0).withWidth (48), juce::Justification::centred);
    }

    const auto c = canvas();
    g.saveState();
    g.reduceClipRegion (c.toNearestInt());

    // pattern paper: small crosses that follow the pan and zoom
    {
        const float step = 24.0f * zoom;
        if (step >= 9.0f)
        {
            const auto origin = juce::Point<float> (0.0f, 0.0f).transformedBy (view());
            const float x0 = c.getX() + std::fmod (origin.x - c.getX(), step) - step;
            const float y0 = c.getY() + std::fmod (origin.y - c.getY(), step) - step;
            const float k = 2.5f * juce::jmin (1.0f, zoom);
            g.setColour (theme::edge);
            for (float y = y0; y < c.getBottom() + step; y += step)
                for (float x = x0; x < c.getRight() + step; x += step)
                {
                    g.fillRect (x - k, y, k * 2 + 1.0f, 0.8f);
                    g.fillRect (x, y - k, 0.8f, k * 2 + 1.0f);
                }
        }
    }

    g.addTransform (view());
    auto& gr = ui.proc.graph();
    const auto edges = gr.edges();

    // wires: ink; parallel paths stitched in camel
    for (auto& e : edges)
    {
        int outs = 0, ins = 0, outIdx = 0, inIdx = 0;
        for (auto& o : edges)
        {
            if (o.from == e.from) { if (&o < &e) ++outIdx; ++outs; }
            if (o.to == e.to) { if (&o < &e) ++inIdx; ++ins; }
        }
        const bool parallel = (outs > 1 && outIdx > 0) || (ins > 1 && inIdx > 0);
        const bool sel = selectedWire == e;
        const bool target = hoverWire == e;
        auto path = wirePath (outPort (e.from), inPort (e.to));
        if (sel || target)
        {
            g.setColour (theme::oxblood);
            g.strokePath (path, juce::PathStrokeType (2.2f));
        }
        else if (parallel)
        {
            juce::Path stitched;
            const float dash[] = { 4.0f, 3.0f };
            juce::PathStrokeType (1.3f).createDashedStroke (stitched, path, dash, 2);
            g.setColour (theme::accent);
            g.fillPath (stitched);
        }
        else
        {
            g.setColour (theme::ink);
            g.strokePath (path, juce::PathStrokeType (1.2f));
        }
    }

    // wire being drawn / re-routed
    if (dragMode == Drag::newWireFromOut || dragMode == Drag::newWireFromIn || dragMode == Drag::rewire)
    {
        const auto a = dragMode == Drag::newWireFromOut ? outPort (pressed.id) : dragMode == Drag::rewire ? outPort (rewireFrom) : nowWorld;
        const auto b = dragMode == Drag::newWireFromIn ? inPort (pressed.id) : nowWorld;
        g.setColour (theme::oxblood);
        g.strokePath (wirePath (a, b), juce::PathStrokeType (1.8f));
        g.fillEllipse (nowWorld.x - 3.5f, nowWorld.y - 3.5f, 7.0f, 7.0f);
    }

    drawIo (g, true);
    drawIo (g, false);
    for (int id : gr.nodeIds())
        drawCard (g, id);
    drawPort (g, outPort (Graph::kInputId), theme::ink, false);
    drawPort (g, inPort (Graph::kOutputId), theme::ink, false);

    if (pendingType.isNotEmpty() && canvas().contains (nowScreen))
    {
        auto ghost = juce::Rectangle<float> (nowWorld.x - kCardW * 0.5f, nowWorld.y - kCardH * 0.5f, (float) kCardW, (float) kCardH);
        juce::Path outline, d;
        outline.addRectangle (ghost);
        const float dash[] = { 4.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (d, outline, dash, 2);
        g.setColour (theme::accent);
        g.fillPath (d);
        g.setFont (theme::heading (20.0f));
        g.drawText (infoFor (pendingType.toStdString()) ? juce::String (infoFor (pendingType.toStdString())->displayName) : pendingType, ghost, juce::Justification::centred);
    }
    g.restoreState();

    if (gr.nodeIds().empty())
    {
        g.setColour (theme::textDim);
        g.setFont (theme::heading (18.0f));
        g.drawText ("Click a block below to add it, or drag it onto the line.", c.withTrimmedTop (c.getHeight() * 0.72f).toNearestInt(),
                    juce::Justification::centredTop);
    }
    if (flash.isNotEmpty())
    {
        auto box = juce::Rectangle<float> (c.getCentreX() - 250, c.getY() + 10, 500, 30);
        g.setColour (theme::card);
        g.fillRect (box);
        g.setColour (theme::oxblood);
        g.drawRect (box, 1.0f);
        g.setFont (theme::sans (12.5f));
        g.setColour (theme::ink);
        g.drawText (flash, box, juce::Justification::centred);
    }
}
