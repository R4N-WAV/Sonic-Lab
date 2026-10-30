#pragma once

#include "Widgets.h"

// Left column: signal source - DAW input, built-in generator or audio file.
class SourcePanel : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    explicit SourcePanel (AtelierProcessor& p);
    ~SourcePanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Waveform : public juce::Component
    {
    public:
        explicit Waveform (AtelierProcessor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent& e) override { mouseDown (e); }
    private:
        AtelierProcessor& proc;
    };

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void updateVisibility();
    void chooseFile();

    AtelierProcessor& proc;
    juce::AudioProcessorValueTreeState& st;

    ParamCombo sourceCombo { st, "src.mode", "Source", "DAW Input passes the track through the chain. Generator and Audio File replace it." };
    juce::Label dawInfo;

    ParamCombo waveCombo { st, "gen.waveform", "Waveform", "Test waveform. Saw/square/triangle are band-limited." };
    ParamSlider freq { st, "gen.freq_hz", "Frequency", "Oscillator frequency. With a sine, harmonic analysis uses this exact fundamental." };
    ParamSlider level { st, "gen.level_db", "Level", "Peak level in dBFS (RMS for noise). -18 dBFS is a typical nominal level." };
    ParamSlider phase { st, "gen.phase_deg", "Phase", "Oscillator phase offset." };
    ParamSlider dc { st, "gen.dc", "DC Offset", "DC added to the signal (linear, full scale = 1)." };
    ParamCombo chans { st, "gen.channels", "Channels", "Channel routing of the generator." };
    ParamSlider sweepStart { st, "gen.sweep_start_hz", "Sweep Start" };
    ParamSlider sweepEnd { st, "gen.sweep_end_hz", "Sweep End" };
    ParamSlider sweepTime { st, "gen.sweep_s", "Sweep Time" };
    juce::TextButton runButton { "Generator: Off" };
    std::unique_ptr<ButtonAttachment> runAttachment;

    juce::TextButton loadButton { "Load audio file..." };
    juce::Label fileLabel;
    Waveform waveform { proc };
    juce::TextButton filePlay { "Play" }, fileStop { "Stop" };
    ParamSlider fileGain { st, "file.gain_db", "File Gain", "Gain applied to the file before the chain." };
    std::unique_ptr<juce::FileChooser> chooser;

    int lastSource = -1, lastWave = -1;
};
