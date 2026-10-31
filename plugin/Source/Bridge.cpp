#include "Bridge.h"

#include <sonic/ModuleRegistry.h>
#include <sonic/modules/ParametricEqModule.h>

using sonic::Graph;

namespace
{
    constexpr int kHeader = 40;
    constexpr int kPaletteH = 44;
    constexpr int kIoW = 140, kIoH = 140;

    juce::String typeLabel (const std::string& type)
    {
        for (auto& m : sonic::availableModules())
            if (m.typeId == type)
                return juce::String (m.displayName).toUpperCase();
        return juce::String (type).toUpperCase();
    }
}

// ------------------------------------------------------------------ palette

BridgePanel::Palette::Palette (BridgePanel& o) : owner (o)
{
    for (auto& m : sonic::availableModules())
    {
        auto* b = chips.add (new juce::TextButton (juce::String (m.displayName)));
        b->setTooltip (juce::String (m.displayName) + " - " + juce::String (m.summary)
                       + "\nClick: add to the end of the main path. Drag: place it, or drop it onto a line to insert it there.");
        const std::string type = m.typeId;
        b->onClick = [this, type] { owner.addBlock (type); };
        b->addMouseListener (this, false);
        b->getProperties().set ("type", juce::String (type));
        addAndMakeVisible (b);
    }
}

void BridgePanel::Palette::paint (juce::Graphics& g)
{
    g.setColour (theme::textDim);
    g.setFont (theme::mono (10.0f));
    g.drawText ("+ ADD BLOCK", getLocalBounds().withWidth (96), juce::Justification::centredLeft);
}

void BridgePanel::Palette::mouseDrag (const juce::MouseEvent& e)
{
    if (e.eventComponent == this || e.getDistanceFromDragStart() < 8)
        return;
    dragging = true;
    owner.pendingType = e.eventComponent->getProperties()["type"].toString();
    owner.dragNow = e.getEventRelativeTo (&owner).position;
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
    owner.setMouseCursor (juce::MouseCursor::NormalCursor);
    if (owner.canvas().contains (p))
        owner.addBlockAt (type, p);
    owner.repaint();
}

void BridgePanel::Palette::resized()
{
    auto r = getLocalBounds().withTrimmedLeft (100);
    int x = r.getX();
    for (auto* b : chips)
    {
        const int w = juce::GlyphArrangement::getStringWidthInt (theme::mono (11.0f), b->getButtonText()) + 20;
        b->setBounds (x, r.getCentreY() - 13, w, 26);
        x += w + 5;
    }
}

// ------------------------------------------------------------------ geometry

BridgePanel::BridgePanel (Ui& u) : ui (u)
{
    addAndMakeVisible (palette);
    setWantsKeyboardFocus (true);
    startTimerHz (15);
}

void BridgePanel::resized()
{
    palette.setBounds (getLocalBounds().removeFromBottom (kPaletteH).reduced (16, 6));
    // keep blocks inside the canvas after a resize
}

juce::Rectangle<float> BridgePanel::canvas() const
{
    return getLocalBounds().toFloat().withTrimmedTop ((float) kHeader).withTrimmedBottom ((float) kPaletteH);
}

juce::Rectangle<float> BridgePanel::cardRect (int id) const
{
    const auto c = canvas();
    if (id == Graph::kInputId)
        return { 16.0f, c.getCentreY() - kIoH * 0.5f, (float) kIoW, (float) kIoH };
    if (id == Graph::kOutputId)
        return { c.getRight() - 16.0f - kIoW, c.getCentreY() - kIoH * 0.5f, (float) kIoW, (float) kIoH };
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

Graph::Edge BridgePanel::nearestWire (juce::Point<float> p, float maxDist) const
{
    Graph::Edge best { -1, -1 };
    float bestD = maxDist;
    for (auto& e : ui.proc.graph().edges())
    {
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
    for (auto it = ids.rbegin(); it != ids.rend(); ++it)
    {
        const int id = *it;
        if (id != Graph::kOutputId && outPort (id).getDistanceFrom (p) < 10.0f) { h.kind = Hit::outPort; h.id = id; return h; }
        if (id != Graph::kInputId && inPort (id).getDistanceFrom (p) < 10.0f) { h.kind = Hit::inPort; h.id = id; return h; }
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
            if (juce::Rectangle<float> (r.getRight() - 22, r.getY() + 6, 16, 16).contains (p)) h.kind = Hit::enableDot;
            else if (juce::Rectangle<float> (r.getX() + 8, r.getBottom() - 36, 30, 30).contains (p)) h.kind = Hit::mixRing;
        }
        return h;
    }
    const auto e = nearestWire (p, 6.0f);
    if (e.from >= 0)
    {
        h.kind = Hit::wire;
        h.edge = e;
    }
    return h;
}

juce::Point<float> BridgePanel::freeSpot (juce::Point<float> want) const
{
    auto c = canvas().reduced (8.0f);
    want.x = juce::jlimit (c.getX() + kIoW + 30.0f, c.getRight() - kIoW - 30.0f - kCardW, want.x);
    want.y = juce::jlimit (c.getY(), c.getBottom() - kCardH, want.y);
    for (int attempt = 0; attempt < 12; ++attempt)
    {
        bool clash = false;
        for (int id : ui.proc.graph().nodeIds())
            if (cardRect (id).expanded (8.0f).intersects (juce::Rectangle<float> (want.x, want.y, (float) kCardW, (float) kCardH)))
                clash = true;
        if (! clash) break;
        want.y += kCardH + 14.0f;
        if (want.y + kCardH > c.getBottom())
        {
            want.y = c.getY();
            want.x += 40.0f;
        }
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

void BridgePanel::addBlock (const std::string& type)
{
    auto& g = ui.proc.graph();
    // insert before Output on the main path when there is exactly one wire into Output
    std::vector<Graph::Edge> intoOut;
    for (auto& e : g.edges())
        if (e.to == Graph::kOutputId)
            intoOut.push_back (e);

    juce::Point<float> pos { canvas().getCentreX() - kCardW * 0.5f, canvas().getCentreY() - kCardH * 0.5f };
    if (intoOut.size() == 1)
    {
        const auto a = outPort (intoOut[0].from);
        pos = { a.x + 50.0f, a.y - kCardH * 0.5f };
        if (pos.x + kCardW > cardRect (Graph::kOutputId).getX() - 30.0f)
        {
            // make room: shift everything left of Output a bit, and place at the right end
            pos.x = cardRect (Graph::kOutputId).getX() - 30.0f - kCardW;
        }
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
}

void BridgePanel::addBlockAt (const std::string& type, juce::Point<float> pos)
{
    auto& g = ui.proc.graph();
    const auto wire = nearestWire (pos, 14.0f);
    const auto spot = freeSpot ({ pos.x - kCardW * 0.5f, pos.y - kCardH * 0.5f });
    const int id = g.addNode (type, spot.x, spot.y);
    if (id < 0)
        return;
    if (wire.from >= 0)
    {
        g.disconnect (wire.from, wire.to);
        g.connect (wire.from, id);
        g.connect (id, wire.to);
    }
    ui.select (id);
    changed (true);
}

void BridgePanel::showNodeMenu (int id)
{
    auto& g = ui.proc.graph();
    auto* n = g.node (id);
    if (n == nullptr)
        return;
    juce::PopupMenu m;
    const bool hasEditor = n->type == "tone.eq" || n->type == "character.saturation";
    m.addItem (1, hasEditor ? "Open editor" : "Show advanced controls");
    m.addItem (2, "Rename...");
    m.addItem (3, "Duplicate");
    m.addItem (4, n->enabled.load() ? "Bypass" : "Enable");
    m.addSeparator();
    m.addItem (5, "Remove");
    juce::Component::SafePointer<BridgePanel> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe, id] (int r)
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
            case 3:
            {
                const int copy = gr.addNode (node->type, node->x + 24, node->y + 24);
                if (auto* c = gr.node (copy))
                {
                    for (int i = 0; i < node->module->numParams(); ++i)
                        c->module->setParam (i, node->module->getParam (i));
                    c->mix = node->mix.load();
                    c->outGainDb = node->outGainDb.load();
                    c->name = node->name + " copy";
                    ui.select (copy);
                }
                safe->changed (true);
                break;
            }
            case 4: node->enabled = ! node->enabled.load(); safe->changed (false); break;
            case 5:
                gr.removeNode (id);
                ui.select (-1);
                safe->changed (true);
                break;
            default: break;
        }
    });
}

// ------------------------------------------------------------------ mouse

void BridgePanel::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto p = e.position;
    drag = hitTest (p);
    dragStart = dragNow = p;
    moved = false;

    if (e.mods.isPopupMenu())
    {
        if (drag.kind == Hit::wire)
        {
            juce::PopupMenu m;
            m.addItem (1, "Remove connection");
            const auto edge = drag.edge;
            juce::Component::SafePointer<BridgePanel> safe (this);
            m.showMenuAsync ({}, [safe, edge] (int r)
            {
                if (safe != nullptr && r == 1)
                {
                    safe->ui.proc.graph().disconnect (edge.from, edge.to);
                    safe->changed (true);
                }
            });
        }
        else if (drag.kind == Hit::node || drag.kind == Hit::mixRing || drag.kind == Hit::enableDot)
        {
            if (drag.id >= 2) { ui.select (drag.id); showNodeMenu (drag.id); }
        }
        drag = {};
        return;
    }

    selectedWire = { -1, -1 };
    switch (drag.kind)
    {
        case Hit::node:
        case Hit::mixRing:
            ui.select (drag.id);
            if (auto* n = ui.proc.graph().node (drag.id))
            {
                nodeStart = { n->x, n->y };
                mixStart = n->mix.load();
            }
            break;
        case Hit::enableDot:
            if (auto* n = ui.proc.graph().node (drag.id))
            {
                n->enabled = ! n->enabled.load();
                changed (false);
            }
            drag = {};
            break;
        case Hit::wire:
            selectedWire = drag.edge;
            drag = {};
            break;
        case Hit::outPort:
        case Hit::inPort:
            break;
        case Hit::none:
        default:
            ui.select (-1);
            break;
    }
    repaint();
}

void BridgePanel::mouseDrag (const juce::MouseEvent& e)
{
    dragNow = e.position;
    if (dragNow.getDistanceFrom (dragStart) > 3.0f)
        moved = true;
    if (drag.kind == Hit::node && drag.id >= 2)
    {
        if (auto* n = ui.proc.graph().node (drag.id))
        {
            const auto c = canvas();
            n->x = juce::jlimit (c.getX(), c.getRight() - kCardW, nodeStart.x + dragNow.x - dragStart.x);
            n->y = juce::jlimit (c.getY(), c.getBottom() - kCardH, nodeStart.y + dragNow.y - dragStart.y);
        }
    }
    else if (drag.kind == Hit::mixRing)
    {
        if (auto* n = ui.proc.graph().node (drag.id))
            n->mix = juce::jlimit (0.0f, 1.0f, mixStart - (dragNow.y - dragStart.y) / 150.0f);
    }
    repaint();
}

void BridgePanel::mouseUp (const juce::MouseEvent& e)
{
    auto& g = ui.proc.graph();
    if (drag.kind == Hit::outPort || drag.kind == Hit::inPort)
    {
        auto target = hitTest (e.position);
        int from = -1, to = -1;
        if (drag.kind == Hit::outPort)
        {
            from = drag.id;
            if (target.kind == Hit::inPort || target.kind == Hit::node || target.kind == Hit::mixRing || target.kind == Hit::enableDot)
                to = target.id;
        }
        else
        {
            to = drag.id;
            if (target.kind == Hit::outPort || target.kind == Hit::node || target.kind == Hit::mixRing || target.kind == Hit::enableDot)
                from = target.id;
        }
        if (from >= 0 && to >= 0 && from != to)
        {
            if (g.connect (from, to))
                changed (true);
            else
            {
                flash = g.wouldCreateCycle (from, to) ? "That connection would create a loop - feedback loops are not allowed."
                                                      : "Those blocks are already connected.";
                flashTicks = 45;
            }
        }
    }
    else if ((drag.kind == Hit::node || drag.kind == Hit::mixRing) && moved)
        changed (false);
    drag = {};
    repaint();
}

void BridgePanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto h = hitTest (e.position);
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
}

void BridgePanel::mouseMove (const juce::MouseEvent& e)
{
    const auto h = hitTest (e.position);
    setMouseCursor (h.kind == Hit::outPort || h.kind == Hit::inPort ? juce::MouseCursor::CrosshairCursor
                    : h.kind == Hit::mixRing ? juce::MouseCursor::UpDownResizeCursor
                    : h.kind == Hit::node ? juce::MouseCursor::DraggingHandCursor
                    : h.kind == Hit::wire || h.kind == Hit::enableDot ? juce::MouseCursor::PointingHandCursor
                    : juce::MouseCursor::NormalCursor);
}

bool BridgePanel::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
    {
        auto& g = ui.proc.graph();
        if (selectedWire.from >= 0)
        {
            g.disconnect (selectedWire.from, selectedWire.to);
            selectedWire = { -1, -1 };
            changed (true);
            return true;
        }
        if (ui.selected >= 2)
        {
            g.removeNode (ui.selected);
            ui.select (-1);
            changed (true);
            return true;
        }
    }
    return false;
}

void BridgePanel::timerCallback()
{
    if (flashTicks > 0 && --flashTicks == 0)
        flash.clear();
    repaint();
}

// ------------------------------------------------------------------ painting

void BridgePanel::drawIo (juce::Graphics& g, bool input)
{
    const int id = input ? Graph::kInputId : Graph::kOutputId;
    auto r = cardRect (id);
    const bool sel = ui.selected == id;
    g.setColour (theme::card);
    g.fillRect (r);
    g.setColour (sel ? theme::light : theme::selEdge);
    g.drawRect (r, sel ? 1.5f : 1.0f);
    auto t = r.reduced (12.0f, 10.0f);
    g.setColour (theme::beigeText);
    g.setFont (theme::mono (10.5f));
    g.drawText (theme::q (input ? "input" : "output"), t.removeFromTop (16.0f), juce::Justification::centredLeft);
    g.setColour (theme::text);
    g.setFont (theme::sansMedium (14.0f));
    juce::String title = "To DAW";
    if (input)
    {
        const auto src = ui.proc.source();
        title = src == AtelierProcessor::Source::generator ? "Generator" : src == AtelierProcessor::Source::file ? "Audio file" : "DAW track";
    }
    g.drawText (title, t.removeFromTop (22.0f), juce::Justification::centredLeft);
    g.setFont (theme::mono (9.5f));
    if (input)
    {
        const char* names[] = { "DAW", "GENERATOR", "FILE" };
        for (int i = 0; i < 3; ++i)
        {
            const bool on = (int) ui.proc.source() == i;
            g.setColour (on ? theme::text : theme::textDim);
            g.drawText (juce::String (on ? juce::CharPointer_UTF8 ("\xe2\x97\x8f ") : juce::CharPointer_UTF8 ("\xe2\x97\x8b ")) + names[i],
                        t.removeFromTop (14.0f), juce::Justification::centredLeft);
        }
    }
    else
    {
        g.setColour (theme::textDim);
        g.drawText ("TRIM " + ui.proc.apvts.getParameter ("io.out_trim_db")->getCurrentValueAsText().toUpperCase(), t.removeFromTop (14.0f), juce::Justification::centredLeft);
        g.drawText (ui.proc.isBypassed() ? "LISTENING: B (DRY)" : "LISTENING: A", t.removeFromTop (14.0f), juce::Justification::centredLeft);
        g.drawText ("LATENCY " + juce::String (ui.proc.currentLatency()) + " SMP", t.removeFromTop (14.0f), juce::Justification::centredLeft);
    }
    auto& m = input ? ui.proc.inputMeter : ui.proc.outputMeter;
    drawMiniMeter (g, r.reduced (12.0f, 0).withY (r.getBottom() - 14.0f).withHeight (4.0f),
                   juce::jmax (m.rms[0].load(), m.rms[1].load()) * 1.414f);
}

void BridgePanel::drawCard (juce::Graphics& g, int id)
{
    auto* n = ui.proc.graph().node (id);
    if (n == nullptr)
        return;
    const auto r = cardRect (id);
    const bool sel = ui.selected == id;
    const bool on = n->enabled.load();
    const float alpha = on ? 1.0f : 0.5f;

    if (sel)
    {
        g.setColour (theme::shadow);
        g.fillRect (r.translated (4.0f, 4.0f));
    }
    g.setColour (theme::card);
    g.fillRect (r);
    g.setColour (sel ? theme::light : theme::edge);
    g.drawRect (r, sel ? 1.5f : 1.0f);

    auto t = r.reduced (12.0f, 10.0f);
    auto top = t.removeFromTop (14.0f);
    g.setColour (theme::textDim.withMultipliedAlpha (alpha));
    g.setFont (theme::mono (9.5f));
    g.drawText (juce::String (id - 1).paddedLeft ('0', 2) + "  " + theme::q (typeLabel (n->type).toLowerCase()), top, juce::Justification::centredLeft);
    g.setColour (on ? theme::goodEdge : theme::textDim.withAlpha (0.4f));
    g.fillEllipse (r.getRight() - 18.0f, r.getY() + 10.0f, 8.0f, 8.0f);

    t.removeFromTop (4.0f);
    g.setColour (theme::text.withMultipliedAlpha (alpha));
    g.setFont (theme::sansSemi (15.0f));
    g.drawText (juce::String (n->name), t.removeFromTop (20.0f), juce::Justification::centredLeft, true);

    // bottom row: mix ring + info
    auto bottom = juce::Rectangle<float> (r.getX() + 8.0f, r.getBottom() - 36.0f, r.getWidth() - 16.0f, 30.0f);
    auto ring = bottom.removeFromLeft (30.0f).reduced (3.0f);
    const float mix = n->mix.load();
    juce::Path track, val;
    const float a0 = juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 2.75f;
    track.addCentredArc (ring.getCentreX(), ring.getCentreY(), 10.0f, 10.0f, 0.0f, a0, a1, true);
    val.addCentredArc (ring.getCentreX(), ring.getCentreY(), 10.0f, 10.0f, 0.0f, a0, a0 + mix * (a1 - a0), true);
    g.setColour (theme::grid);
    g.strokePath (track, juce::PathStrokeType (3.0f));
    g.setColour (theme::text.withMultipliedAlpha (alpha));
    g.strokePath (val, juce::PathStrokeType (3.0f));
    g.setFont (theme::mono (10.0f));
    g.drawText ("MIX " + juce::String (juce::roundToInt (mix * 100)) + "%", bottom.removeFromLeft (64.0f).withTrimmedLeft (4.0f), juce::Justification::centredLeft);

    auto info = bottom;
    if (! on)
    {
        g.setColour (theme::textDim);
        g.drawText ("BYPASSED", info, juce::Justification::centredRight);
    }
    else if (n->type == "dynamics.compressor")
    {
        const float gr = n->module->meter (0);
        auto bar = info.withSizeKeepingCentre (info.getWidth() - 6.0f, 6.0f);
        g.setColour (theme::grid);
        g.fillRect (bar);
        const float w = juce::jlimit (0.0f, 1.0f, -gr / 20.0f) * bar.getWidth();
        g.setColour (theme::beige);
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
        g.setColour (theme::grid);
        g.drawHorizontalLine ((int) info.getCentreY(), info.getX(), info.getRight());
        g.setColour (theme::beige);
        g.strokePath (p, juce::PathStrokeType (1.3f));
    }
    else
    {
        // first SIMPLE parameter as a hint
        for (int i = 0; i < n->module->numParams(); ++i)
        {
            const auto& s = n->module->params()[(size_t) i];
            if (s.simple)
            {
                g.setColour (theme::textDim);
                g.drawText (juce::String (s.name).toUpperCase() + " " + formatParam (s, n->module->getParam (i)).toUpperCase(),
                            info, juce::Justification::centredRight, true);
                break;
            }
        }
    }

    if (sel)
        theme::drawTag (g, juce::Rectangle<int> ((int) r.getX(), (int) r.getY() - 22, 70, 16), "SELECTED", theme::beige, theme::beigeEdge, theme::beigeTag);

    // ports
    for (auto p : { inPort (id), outPort (id) })
    {
        g.setColour (theme::panel);
        g.fillEllipse (p.x - 5, p.y - 5, 10, 10);
        g.setColour (theme::text);
        g.drawEllipse (p.x - 5, p.y - 5, 10, 10, 1.4f);
    }
}

void BridgePanel::paint (juce::Graphics& g)
{
    theme::drawPanel (g, getLocalBounds(), "Bridge", "Fig. 02 - signal path");
    g.setColour (theme::textDim);
    g.setFont (theme::sans (12.0f));
    g.drawText ("Drag from a port to connect  -  parallel lines are summed  -  Delete removes the selection",
                getLocalBounds().withHeight (40).reduced (16, 0), juce::Justification::centredRight);

    const auto c = canvas();
    g.setColour (theme::dashed);
    for (float y = c.getY() + 10.0f; y < c.getBottom(); y += 20.0f)
        for (float x = 10.0f; x < (float) getWidth(); x += 20.0f)
            g.fillRect (x, y, 1.0f, 1.0f);

    auto& gr = ui.proc.graph();
    const auto edges = gr.edges();

    // wires: dashed when part of a split/merge (parallel path)
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
        auto path = wirePath (outPort (e.from), inPort (e.to));
        if (parallel && ! sel)
        {
            juce::Path dashedPath;
            const float dash[] = { 6.0f, 4.0f };
            juce::PathStrokeType (1.6f).createDashedStroke (dashedPath, path, dash, 2);
            g.setColour (theme::parallel);
            g.fillPath (dashedPath);
        }
        else
        {
            g.setColour (sel ? theme::beige : theme::text.withAlpha (0.8f));
            g.strokePath (path, juce::PathStrokeType (sel ? 2.2f : 1.6f));
        }
    }

    // wire being dragged
    if (drag.kind == Hit::outPort || drag.kind == Hit::inPort)
    {
        const auto a = drag.kind == Hit::outPort ? outPort (drag.id) : dragNow;
        const auto b = drag.kind == Hit::outPort ? dragNow : inPort (drag.id);
        g.setColour (theme::beige);
        g.strokePath (wirePath (a, b), juce::PathStrokeType (1.6f));
    }

    if (pendingType.isNotEmpty() && canvas().contains (dragNow))
    {
        auto ghost = juce::Rectangle<float> (dragNow.x - kCardW * 0.5f, dragNow.y - kCardH * 0.5f, (float) kCardW, (float) kCardH);
        g.setColour (theme::beige.withAlpha (0.6f));
        const float dash[] = { 5.0f, 4.0f };
        juce::Path outline, d;
        outline.addRectangle (ghost);
        juce::PathStrokeType (1.2f).createDashedStroke (d, outline, dash, 2);
        g.fillPath (d);
        g.setFont (theme::mono (10.5f));
        g.drawText (typeLabel (pendingType.toStdString()), ghost, juce::Justification::centred);
        const auto w = nearestWire (dragNow, 14.0f);
        if (w.from >= 0)
        {
            g.setColour (theme::beige);
            g.strokePath (wirePath (outPort (w.from), inPort (w.to)), juce::PathStrokeType (2.4f));
        }
    }

    drawIo (g, true);
    drawIo (g, false);
    for (int id : gr.nodeIds())
        drawCard (g, id);
    for (auto p : { outPort (Graph::kInputId), inPort (Graph::kOutputId) })
    {
        g.setColour (theme::panel);
        g.fillEllipse (p.x - 5, p.y - 5, 10, 10);
        g.setColour (theme::text);
        g.drawEllipse (p.x - 5, p.y - 5, 10, 10, 1.4f);
    }

    if (gr.nodeIds().empty())
    {
        g.setColour (theme::textDim);
        g.setFont (theme::sans (13.0f));
        g.drawText ("Click a block below to add it - or drag it onto the line.", c.withTrimmedTop (c.getHeight() * 0.62f).toNearestInt(),
                    juce::Justification::centredTop);
    }
    if (flash.isNotEmpty())
    {
        auto box = juce::Rectangle<float> (c.getCentreX() - 230, c.getY() + 10, 460, 28);
        g.setColour (theme::card);
        g.fillRect (box);
        g.setColour (theme::warn);
        g.drawRect (box, 1.0f);
        g.setFont (theme::sans (12.5f));
        g.drawText (flash, box, juce::Justification::centred);
    }
}
