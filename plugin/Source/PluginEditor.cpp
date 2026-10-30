#include "PluginEditor.h"

AtelierEditor::AtelierEditor (AtelierProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (sourcePanel);
    addAndMakeVisible (chainPanel);
    addAndMakeVisible (moduleEditor);
    addAndMakeVisible (analysisPanel);
    chainPanel.onSelect = [this] (int m) { moduleEditor.setModule (m); };

    setResizable (true, true);
    setResizeLimits (1180, 780, 2560, 1600);
    setSize (1440, 900);
    startTimerHz (4);
}

AtelierEditor::~AtelierEditor()
{
    setLookAndFeel (nullptr);
}

void AtelierEditor::timerCallback()
{
    repaint (header);
}

void AtelierEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);

    // ---- Header: wordmark + live status ----
    auto h = header.reduced (16, 0);
    g.setColour (theme::text);
    g.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::plain)).withExtraKerningFactor (0.35f));
    auto mark = h.removeFromLeft (150);
    g.drawText ("ATELIER", mark, juce::Justification::centredLeft);
    g.setColour (theme::accent);
    g.setFont (theme::bold (10.0f).withExtraKerningFactor (0.3f));
    g.drawText ("BY R4N", h.removeFromLeft (60), juce::Justification::centredLeft);
    g.setColour (theme::textFaint);
    g.setFont (theme::label (12.0f));
    g.drawText ("Understand sound. Shape character.", h.removeFromLeft (260), juce::Justification::centredLeft);

    // status (right side)
    juce::String scText;
    juce::Colour scColour;
    switch (proc.sidechainState())
    {
        case AtelierProcessor::SidechainState::active:       scText = "SIDECHAIN: ACTIVE";        scColour = theme::good; break;
        case AtelierProcessor::SidechainState::silent:       scText = "SIDECHAIN: SILENT";        scColour = theme::warn; break;
        case AtelierProcessor::SidechainState::notConnected:
        default:                                              scText = "SIDECHAIN: NOT CONNECTED"; scColour = theme::textFaint; break;
    }
    auto pill = h.removeFromRight (190).withSizeKeepingCentre (180, 22);
    g.setColour (scColour.withAlpha (0.12f));
    g.fillRoundedRectangle (pill.toFloat(), 3.0f);
    g.setColour (scColour);
    g.drawRoundedRectangle (pill.toFloat().reduced (0.5f), 3.0f, 1.0f);
    g.setFont (theme::bold (10.0f));
    g.drawText (scText, pill, juce::Justification::centred);

    g.setColour (theme::textDim);
    g.setFont (theme::mono (11.5f));
    const int overs = proc.clipCount.load();
    const juce::String status = juce::String (proc.currentSampleRate() / 1000.0, 1) + " kHz   "
                              + juce::String (proc.currentBlockSize()) + " smp   latency "
                              + juce::String (proc.currentLatency()) + "   "
                              + (overs > 0 ? "overs " + juce::String (overs) + "   " : juce::String())
                              + "v" JucePlugin_VersionString;
    g.drawText (status, h.removeFromRight (420), juce::Justification::centredRight);

    g.setColour (theme::edge);
    g.drawHorizontalLine (header.getBottom() - 1, 0.0f, (float) getWidth());
}

void AtelierEditor::resized()
{
    auto r = getLocalBounds();
    header = r.removeFromTop (52);
    r.reduce (10, 10);

    sourcePanel.setBounds (r.removeFromLeft (320));
    r.removeFromLeft (10);
    chainPanel.setBounds (r.removeFromLeft (270));
    r.removeFromLeft (10);
    moduleEditor.setBounds (r.removeFromTop (juce::jmax (420, r.getHeight() * 45 / 100)));
    r.removeFromTop (10);
    analysisPanel.setBounds (r);
}
