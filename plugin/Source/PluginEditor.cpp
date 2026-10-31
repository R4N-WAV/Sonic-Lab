#include "PluginEditor.h"
#include "Blueprint.h"

void AtelierEditor::Content::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
}

AtelierEditor::AtelierEditor (AtelierProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    // every pop-up (menus, combo lists, dialogs) uses Atelier's look, even without a parent
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
    addAndMakeVisible (content);
    for (juce::Component* c : { (juce::Component*) &header, (juce::Component*) &spectrum, (juce::Component*) &readout,
                                (juce::Component*) &bridge })
        content.addAndMakeVisible (c);
    content.addChildComponent (blockView);

    // layout: the selected block takes the centre (where the spectrum is) - eye level,
    // not a corner. With nothing selected the spectrum is back.
    header.setBounds (0, 0, kW, 72);
    spectrum.setBounds (24, 88, 1240, 432);
    blockView.setBounds (24, 88, 1240, 432);
    readout.setBounds (1280, 88, 296, 432);
    bridge.setBounds (24, 536, 1552, 448);

    ui.onSelectionChanged = [this] { selectionChanged(); };
    ui.onBridgeChanged = [this]
    {
        bridge.repaint();
        if (ui.selected >= 2 && proc.graph().node (ui.selected) == nullptr)
            ui.select (-1);
        else if (blockView.isVisible() && (blockView.shownId() != ui.selected || blockView.isStale()))
            blockView.showFor (ui.selected);
    };
    ui.closeDialogs = [this] { exportDialog.reset(); };
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
    exportDialog.reset();
    if (&juce::LookAndFeel::getDefaultLookAndFeel() == &lnf)
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void AtelierEditor::selectionChanged()
{
    const bool showBlock = ui.selected >= 0;
    if (showBlock && blockView.shownId() != ui.selected)
        blockView.showFor (ui.selected);
    blockView.setVisible (showBlock);
    spectrum.setVisible (! showBlock);
    if (! showBlock)
        blockView.showFor (-2);   // releases the block's controls
    bridge.repaint();
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
    if (blockView.isVisible())
        blockView.analysisUpdated();
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
    if (k == juce::KeyPress::escapeKey)
    {
        if (exportDialog != nullptr) { exportDialog.reset(); return true; }
        if (ui.selected >= 0) { ui.select (-1); return true; }
    }
    return false;
}

void AtelierEditor::showExport()
{
    juce::Component::SafePointer<AtelierEditor> safe (this);
    exportDialog = Blueprint::createDialog (ui, [safe] { juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->exportDialog.reset(); }); });
    content.addAndMakeVisible (*exportDialog);
    exportDialog->setBounds (0, 0, kW, kH);
    exportDialog->toFront (true);
}
