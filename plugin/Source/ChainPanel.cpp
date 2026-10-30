#include "ChainPanel.h"

ChainPanel::ChainPanel (AtelierProcessor& p) : proc (p)
{
    abButton.setClickingTogglesState (true);
    abButton.setTooltip ("A/B: compare the processed chain (A) with the unprocessed signal (B). "
                         "B is delayed by the chain latency so both stay time-aligned.");
    abAttachment = std::make_unique<ButtonAttachment> (proc.apvts, "chain.bypass", abButton);
    abButton.onStateChange = [this] { abButton.setButtonText (abButton.getToggleState() ? "B  Bypassed" : "A  Processed"); };
    addAndMakeVisible (abButton);

    addAndMakeVisible (inTrim);
    addAndMakeVisible (outTrim);
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);
    addAndMakeVisible (scMeter);

    latencyLabel.setFont (theme::mono (11.0f));
    latencyLabel.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (latencyLabel);

    rebuildRows();
    startTimerHz (10);
}

void ChainPanel::rebuildRows()
{
    rows.clear();
    shownOrder = proc.chainOrder();
    for (int s = 0; s < (int) shownOrder.size(); ++s)
        addAndMakeVisible (rows.add (new Row (*this, shownOrder[(size_t) s], s, (int) shownOrder.size())));
    resized();
    repaint();
}

void ChainPanel::timerCallback()
{
    if (proc.chainOrder() != shownOrder)
        rebuildRows(); // e.g. after the host restored a session
    abButton.setButtonText (abButton.getToggleState() ? "B  Bypassed" : "A  Processed");
    latencyLabel.setText ("Chain latency: " + juce::String (proc.currentLatency()) + " smp", juce::dontSendNotification);
}

void ChainPanel::paint (juce::Graphics& g)
{
    theme::drawPanel (g, getLocalBounds(), "Chain");
}

void ChainPanel::resized()
{
    auto r = getLocalBounds().reduced (12);
    r.removeFromTop (22);

    // signal flow label
    for (auto* row : rows)
    {
        row->setBounds (r.removeFromTop (44));
        r.removeFromTop (6);
    }
    r.removeFromTop (6);
    abButton.setBounds (r.removeFromTop (34));
    r.removeFromTop (8);
    latencyLabel.setBounds (r.removeFromTop (18));
    r.removeFromTop (8);
    inTrim.setBounds (r.removeFromTop (28));
    outTrim.setBounds (r.removeFromTop (28));
    r.removeFromTop (12);

    auto meters = r.removeFromTop (juce::jmin (220, r.getHeight()));
    const int w = (meters.getWidth() - 16) / 3;
    inMeter.setBounds (meters.removeFromLeft (w));
    meters.removeFromLeft (8);
    outMeter.setBounds (meters.removeFromLeft (w));
    meters.removeFromLeft (8);
    scMeter.setBounds (meters.removeFromLeft (w));
}

// ---------------------------------------------------------------------------

ChainPanel::Row::Row (ChainPanel& o, int m, int s, int numSlots) : moduleIndex (m), slot (s), owner (o)
{
    enable.setTooltip ("Enable/disable this stage.");
    enableAttachment = std::make_unique<ButtonAttachment> (owner.proc.apvts, AtelierProcessor::enabledId (m), enable);
    addAndMakeVisible (enable);

    up.setTooltip ("Move earlier in the chain");
    down.setTooltip ("Move later in the chain");
    up.setEnabled (s > 0);
    down.setEnabled (s < numSlots - 1);
    up.onClick = [this] { owner.proc.moveChainSlot (slot, slot - 1); }; // rows rebuild from the timer (never delete a Row inside its own callback)
    down.onClick = [this] { owner.proc.moveChainSlot (slot, slot + 1); }; // rows rebuild from the timer (never delete a Row inside its own callback)
    addAndMakeVisible (up);
    addAndMakeVisible (down);
}

void ChainPanel::Row::paint (juce::Graphics& g)
{
    const bool sel = owner.selected == moduleIndex;
    auto r = getLocalBounds().toFloat();
    g.setColour (sel ? theme::panelHi.brighter (0.04f) : theme::panelHi);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (sel ? theme::accent : theme::edge);
    g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);

    auto& mod = owner.proc.chain().module (moduleIndex);
    auto t = getLocalBounds().reduced (34, 4).withTrimmedRight (60);
    g.setColour (owner.proc.chain().isEnabled (moduleIndex) ? theme::text : theme::textFaint);
    g.setFont (theme::label (13.5f));
    g.drawText (juce::String (slot + 1) + "  " + mod.displayName(), t.removeFromTop (20), juce::Justification::centredLeft);
    g.setColour (theme::textFaint);
    g.setFont (theme::mono (10.0f));
    g.drawText (juce::String (mod.typeId()) + "  |  " + juce::String (mod.latencySamples()) + " smp",
                t, juce::Justification::centredLeft);
}

void ChainPanel::Row::resized()
{
    auto r = getLocalBounds().reduced (4);
    enable.setBounds (r.removeFromLeft (26));
    auto b = r.removeFromRight (56);
    up.setBounds (b.removeFromTop (b.getHeight() / 2).reduced (1));
    down.setBounds (b.reduced (1));
}

void ChainPanel::Row::mouseDown (const juce::MouseEvent&)
{
    owner.selected = moduleIndex;
    for (auto* row : owner.rows)
        row->repaint();
    if (owner.onSelect)
        owner.onSelect (moduleIndex);
}
