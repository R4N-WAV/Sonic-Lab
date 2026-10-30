#include "SourcePanel.h"

using Wf = sonic::SignalGenerator::Waveform;
using Src = AtelierProcessor::Source;

SourcePanel::SourcePanel (AtelierProcessor& p) : proc (p), st (p.apvts)
{
    addAndMakeVisible (sourceCombo);

    dawInfo.setText ("The track's audio passes through the chain.\n\nFor reference-plugin analysis, insert the reference "
                     "plugin before ATELIER and route the unprocessed signal to ATELIER's sidechain input.",
                     juce::dontSendNotification);
    dawInfo.setFont (theme::label (12.0f));
    dawInfo.setColour (juce::Label::textColourId, theme::textDim);
    dawInfo.setJustificationType (juce::Justification::topLeft);
    addChildComponent (dawInfo);

    for (juce::Component* c : { (juce::Component*) &waveCombo, (juce::Component*) &freq, (juce::Component*) &level,
                                (juce::Component*) &phase, (juce::Component*) &dc, (juce::Component*) &chans,
                                (juce::Component*) &sweepStart, (juce::Component*) &sweepEnd, (juce::Component*) &sweepTime,
                                (juce::Component*) &runButton, (juce::Component*) &loadButton, (juce::Component*) &fileLabel,
                                (juce::Component*) &waveform, (juce::Component*) &filePlay, (juce::Component*) &fileStop,
                                (juce::Component*) &fileGain })
        addChildComponent (c);

    runButton.setClickingTogglesState (true);
    runButton.setTooltip ("Start/stop the test generator. The generator replaces the DAW input while running.");
    runAttachment = std::make_unique<ButtonAttachment> (st, "gen.run", runButton);

    loadButton.onClick = [this] { chooseFile(); };
    fileLabel.setFont (theme::label (12.0f));
    fileLabel.setColour (juce::Label::textColourId, theme::textDim);
    fileLabel.setText ("No file loaded (WAV, AIFF, FLAC)", juce::dontSendNotification);
    filePlay.onClick = [this]
    {
        auto& t = proc.transport();
        if (proc.currentFile() == juce::File())
            return;
        if (t.isPlaying())
            t.stop();
        else
            t.start();
    };
    fileStop.onClick = [this]
    {
        proc.transport().stop();
        proc.transport().setPosition (0.0);
    };

    proc.thumbnail().addChangeListener (this);
    if (proc.currentFile() != juce::File())
        fileLabel.setText (proc.currentFile().getFileName(), juce::dontSendNotification);

    startTimerHz (15);
    timerCallback();
}

SourcePanel::~SourcePanel()
{
    proc.thumbnail().removeChangeListener (this);
}

void SourcePanel::timerCallback()
{
    const int src = (int) proc.source();
    const int wave = (int) *st.getRawParameterValue ("gen.waveform");
    if (src != lastSource || wave != lastWave)
    {
        lastSource = src;
        lastWave = wave;
        updateVisibility();
    }
    runButton.setButtonText (runButton.getToggleState() ? "Generator: Running" : "Generator: Off");
    filePlay.setButtonText (proc.transport().isPlaying() ? "Pause" : "Play");
    if (waveform.isVisible())
        waveform.repaint();
}

void SourcePanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    waveform.repaint();
}

void SourcePanel::updateVisibility()
{
    const auto src = (Src) lastSource;
    const bool gen = src == Src::generator, file = src == Src::file;
    const auto wf = (Wf) lastWave;
    const bool sweep = wf == Wf::logSweep;
    const bool periodic = sonic::SignalGenerator::isPeriodic (wf);

    dawInfo.setVisible (src == Src::dawInput);
    waveCombo.setVisible (gen);
    freq.setVisible (gen && periodic);
    level.setVisible (gen);
    phase.setVisible (gen && periodic && wf != Wf::impulse);
    dc.setVisible (gen);
    chans.setVisible (gen);
    sweepStart.setVisible (gen && sweep);
    sweepEnd.setVisible (gen && sweep);
    sweepTime.setVisible (gen && sweep);
    runButton.setVisible (gen);

    for (juce::Component* c : { (juce::Component*) &loadButton, (juce::Component*) &fileLabel, (juce::Component*) &waveform,
                                (juce::Component*) &filePlay, (juce::Component*) &fileStop, (juce::Component*) &fileGain })
        c->setVisible (file);

    resized();
    repaint();
}

void SourcePanel::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Load reference audio",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg");
    juce::Component::SafePointer<SourcePanel> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr)
                                  return;
                              const auto f = fc.getResult();
                              if (f == juce::File())
                                  return;
                              juce::String err;
                              if (safe->proc.loadFile (f, err))
                              {
                                  const double len = safe->proc.transport().getLengthInSeconds();
                                  safe->fileLabel.setText (f.getFileName() + "  (" + juce::String (len, 1) + " s)", juce::dontSendNotification);
                                  safe->fileLabel.setColour (juce::Label::textColourId, theme::text);
                              }
                              else
                              {
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not load file", err);
                              }
                              safe->waveform.repaint();
                          });
}

void SourcePanel::paint (juce::Graphics& g)
{
    theme::drawPanel (g, getLocalBounds(), "Source");
}

void SourcePanel::resized()
{
    auto r = getLocalBounds().reduced (12);
    r.removeFromTop (22);
    const int row = 28;
    sourceCombo.setBounds (r.removeFromTop (row));
    r.removeFromTop (12);

    auto place = [&r, row] (juce::Component& c)
    {
        if (c.isVisible())
            c.setBounds (r.removeFromTop (row));
    };

    if (dawInfo.isVisible())
        dawInfo.setBounds (r.removeFromTop (120));

    if (runButton.isVisible())
    {
        place (waveCombo);
        place (freq);
        place (level);
        place (phase);
        place (dc);
        place (chans);
        place (sweepStart);
        place (sweepEnd);
        place (sweepTime);
        r.removeFromTop (12);
        runButton.setBounds (r.removeFromTop (34));
    }

    if (loadButton.isVisible())
    {
        loadButton.setBounds (r.removeFromTop (30));
        fileLabel.setBounds (r.removeFromTop (24));
        r.removeFromTop (4);
        waveform.setBounds (r.removeFromTop (90));
        r.removeFromTop (8);
        auto t = r.removeFromTop (30);
        filePlay.setBounds (t.removeFromLeft (90));
        t.removeFromLeft (6);
        fileStop.setBounds (t.removeFromLeft (80));
        r.removeFromTop (8);
        place (fileGain);
    }
}

void SourcePanel::Waveform::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (theme::bg);
    g.fillRect (r);
    auto& thumb = proc.thumbnail();
    const double len = thumb.getTotalLength();
    if (len <= 0.0)
    {
        g.setColour (theme::textFaint);
        g.setFont (theme::label (12.0f));
        g.drawText ("No waveform", r, juce::Justification::centred);
        return;
    }
    g.setColour (theme::reference);
    thumb.drawChannels (g, r.reduced (0, 4), 0.0, len, 1.0f);
    const double pos = proc.transport().getCurrentPosition();
    const float x = (float) (pos / juce::jmax (0.001, proc.transport().getLengthInSeconds())) * (float) r.getWidth();
    g.setColour (theme::accent);
    g.fillRect (x, 0.0f, 1.5f, (float) r.getHeight());
}

void SourcePanel::Waveform::mouseDown (const juce::MouseEvent& e)
{
    const double len = proc.transport().getLengthInSeconds();
    if (len > 0.0)
        proc.transport().setPosition (juce::jlimit (0.0, len, len * e.position.x / (double) getWidth()));
}
