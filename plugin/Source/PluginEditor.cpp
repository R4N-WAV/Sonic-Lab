#include "PluginEditor.h"
#include "Blueprint.h"

void AtelierEditor::Content::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
    g.setColour (theme::textDim);
    g.setFont (theme::mono (9.0f));
    g.drawText ("ATELIER - WORK IN PROGRESS - c/o R4N - OSLO", juce::Rectangle<int> (24, kH - 20, 600, 18), juce::Justification::centredLeft);
    g.drawText ("v" JucePlugin_VersionString, juce::Rectangle<int> (kW - 224, kH - 20, 200, 18), juce::Justification::centredRight);
}

AtelierEditor::AtelierEditor (AtelierProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    for (juce::Component* c : { (juce::Component*) &header, (juce::Component*) &spectrum, (juce::Component*) &readout,
                                (juce::Component*) &bridge, (juce::Component*) &inspector })
        content.addAndMakeVisible (c);

    header.setBounds (0, 0, kW, 72);
    spectrum.setBounds (24, 88, 1240, 432);
    readout.setBounds (1280, 88, 296, 432);
    bridge.setBounds (24, 536, 1220, 440);
    inspector.setBounds (1260, 536, 316, 440);

    ui.onSelectionChanged = [this] { inspector.rebuild(); bridge.repaint(); spectrum.repaint(); };
    ui.onBridgeChanged = [this]
    {
        inspector.rebuild();
        bridge.repaint();
        if (overlay != nullptr && proc.graph().node (overlay->node()) == nullptr)
            closeEditor();
    };
    ui.openFullEditor = [this] (int id) { openEditor (id); };
    ui.backToBridge = [this] { exportDialog.reset(); if (overlay != nullptr) closeEditor(); };
    ui.menuParent = &content;
    header.onExport = [this] { showExport(); };

    // size: as large as the screen comfortably allows, fixed 16:10 aspect
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) kW / kH);
    setResizeLimits (960, 600, 3200, 2000);
    int w = kW;
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        w = juce::jmin (kW, (int) (d->userArea.getWidth() * 0.92), (int) (d->userArea.getHeight() * 0.9 * kW / kH));
    setSize (w, w * kH / kW);
    setWantsKeyboardFocus (true);
    startTimerHz (20);
}

AtelierEditor::~AtelierEditor()
{
    overlay.reset();
    exportDialog.reset();
    setLookAndFeel (nullptr);
}

void AtelierEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
}

void AtelierEditor::resized()
{
    const float scale = (float) getWidth() / (float) kW;
    content.setBounds (0, 0, kW, kH);
    content.setTransform (juce::AffineTransform::scale (scale));
}

void AtelierEditor::timerCallback()
{
    // Fallback for hosts/platforms where vertical-blank callbacks do not arrive.
    if (juce::Time::getMillisecondCounterHiRes() - lastVBlank > 100.0)
        onVBlank();
}

void AtelierEditor::onVBlank()
{
    lastVBlank = juce::Time::getMillisecondCounterHiRes();
    if (ui.frozen || ! worker.getResults (ui.res, ui.res.serial))
        return;
    if (overlay != nullptr)
        overlay->analysisUpdated();
    else
        spectrum.repaintPlot();
    // numbers change slowly and are read, not watched: ~10 repaints per second is plenty
    if (lastVBlank - lastReadoutPaint > 100.0)
    {
        readout.repaint();
        lastReadoutPaint = lastVBlank;
    }
}

bool AtelierEditor::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
    {
        if (proc.undo() && ui.onBridgeChanged) ui.onBridgeChanged();
        return true;
    }
    if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)
        || k == juce::KeyPress ('y', juce::ModifierKeys::commandModifier, 0))
    {
        if (proc.redo() && ui.onBridgeChanged) ui.onBridgeChanged();
        return true;
    }
    if (k == juce::KeyPress::escapeKey && overlay != nullptr)
    {
        closeEditor();
        return true;
    }
    return false;
}

void AtelierEditor::openEditor (int nodeId)
{
    auto* n = proc.graph().node (nodeId);
    if (n == nullptr)
        return;
    ui.select (nodeId);
    if (n->type == "tone.eq")
        overlay = std::make_unique<EqEditor> (ui, nodeId);
    else if (n->type == "character.saturation")
        overlay = std::make_unique<SaturationEditor> (ui, nodeId);
    else
    {
        // other blocks: their full controls live in the inspector's ADVANCED view
        inspector.showAdvanced();
        return;
    }
    ui.openEditorId = nodeId;
    juce::Component::SafePointer<AtelierEditor> safe (this);
    overlay->onClose = [safe] { juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closeEditor(); }); };
    content.addAndMakeVisible (*overlay);
    overlay->setBounds (0, 72, kW, kH - 72 - 20);
    overlay->toFront (true);
}

void AtelierEditor::closeEditor()
{
    overlay.reset();
    ui.openEditorId = -1;
    inspector.rebuild();
    bridge.repaint();
}

void AtelierEditor::showExport()
{
    juce::Component::SafePointer<AtelierEditor> safe (this);
    exportDialog = Blueprint::createDialog (ui, [safe] { juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->exportDialog.reset(); }); });
    content.addAndMakeVisible (*exportDialog);
    exportDialog->setBounds (0, 0, kW, kH);
    exportDialog->toFront (true);
}
