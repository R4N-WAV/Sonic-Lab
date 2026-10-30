#pragma once

#include "PluginProcessor.h"
#include "Theme.h"

using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

// Horizontal slider bound to a plugin parameter. Range, skew and text
// formatting come from the parameter itself (single source of truth).
class ParamSlider : public juce::Component
{
public:
    ParamSlider (juce::AudioProcessorValueTreeState& state, const juce::String& paramID,
                 const juce::String& caption_, const juce::String& tooltip = {})
    {
        caption.setText (caption_, juce::dontSendNotification);
        caption.setFont (theme::label (12.5f));
        caption.setColour (juce::Label::textColourId, theme::textDim);
        caption.setTooltip (tooltip);
        addAndMakeVisible (caption);

        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 78, 20);
        slider.setTooltip (tooltip);
        addAndMakeVisible (slider);
        attachment = std::make_unique<SliderAttachment> (state, paramID, slider);

        if (auto* p = state.getParameter (paramID))
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    }

    void resized() override
    {
        auto r = getLocalBounds();
        caption.setBounds (r.removeFromLeft (juce::jmin (100, r.getWidth() / 3)));
        slider.setBounds (r);
    }

    juce::Slider slider;

private:
    juce::Label caption;
    std::unique_ptr<SliderAttachment> attachment;
};

// Combo box bound to a choice parameter (items are taken from the parameter).
class ParamCombo : public juce::Component
{
public:
    ParamCombo (juce::AudioProcessorValueTreeState& state, const juce::String& paramID,
                const juce::String& caption_, const juce::String& tooltip = {})
    {
        caption.setText (caption_, juce::dontSendNotification);
        caption.setFont (theme::label (12.5f));
        caption.setColour (juce::Label::textColourId, theme::textDim);
        caption.setTooltip (tooltip);
        addAndMakeVisible (caption);

        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramID)))
            combo.addItemList (p->choices, 1);
        combo.setTooltip (tooltip);
        addAndMakeVisible (combo);
        attachment = std::make_unique<ComboAttachment> (state, paramID, combo);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        caption.setBounds (r.removeFromLeft (juce::jmin (100, r.getWidth() / 3)));
        combo.setBounds (r.reduced (0, 2));
    }

    juce::ComboBox combo;

private:
    juce::Label caption;
    std::unique_ptr<ComboAttachment> attachment;
};

// Stereo peak + RMS bar meter, -60..0 dBFS, with numeric peak readout.
class LevelMeter : public juce::Component, private juce::Timer
{
public:
    LevelMeter (AtelierProcessor::Meter& m, const juce::String& name) : meter (m), caption (name) { startTimerHz (30); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (theme::textDim);
        g.setFont (theme::bold (9.5f));
        g.drawText (caption, r.removeFromTop (14), juce::Justification::centred);

        auto readout = r.removeFromBottom (14);
        const int bw = (r.getWidth() - 4) / 2;
        for (int c = 0; c < 2; ++c)
        {
            auto b = juce::Rectangle<int> (r.getX() + c * (bw + 4), r.getY(), bw, r.getHeight()).toFloat();
            g.setColour (theme::bg);
            g.fillRect (b);
            auto toY = [&] (float lin)
            {
                const float db = juce::jlimit (-60.0f, 0.0f, juce::Decibels::gainToDecibels (lin, -60.0f));
                return b.getBottom() - (db + 60.0f) / 60.0f * b.getHeight();
            };
            g.setColour (theme::reference.withAlpha (0.5f));
            g.fillRect (b.withTop (toY (rms[c])));
            g.setColour (peaks[c] >= 1.0f ? theme::bad : theme::accent);
            g.fillRect (b.getX(), toY (peaks[c]), b.getWidth(), 2.0f);
            g.setColour (theme::textFaint);
            g.drawHorizontalLine ((int) toY (juce::Decibels::decibelsToGain (-18.0f)), b.getX(), b.getRight());
        }

        g.setColour (theme::text);
        g.setFont (theme::mono (10.5f));
        const float pk = juce::jmax (peaks[0], peaks[1]);
        g.drawText (pk > 0.0f ? juce::String (juce::Decibels::gainToDecibels (pk, -120.0f), 1) : "-inf",
                    readout, juce::Justification::centred);
    }

private:
    void timerCallback() override
    {
        for (int c = 0; c < 2; ++c)
        {
            const float p = meter.peak[c].exchange (0.0f);
            peaks[c] = juce::jmax (p, peaks[c] * 0.88f);
            rms[c] = meter.rms[c].load();
        }
        repaint();
    }

    AtelierProcessor::Meter& meter;
    juce::String caption;
    float peaks[2] {}, rms[2] {};
};

// Log-frequency / dB plot helpers shared by graphs.
namespace plot
{
    inline float freqToX (double f, juce::Rectangle<float> r, double fMin = 20.0, double fMax = 20000.0)
    {
        return r.getX() + (float) (std::log (f / fMin) / std::log (fMax / fMin)) * r.getWidth();
    }
    inline float dbToY (double db, juce::Rectangle<float> r, double dbMin, double dbMax)
    {
        return r.getBottom() - (float) ((db - dbMin) / (dbMax - dbMin)) * r.getHeight();
    }

    inline void drawFreqGrid (juce::Graphics& g, juce::Rectangle<float> r, double dbMin, double dbMax, double dbStep,
                              const juce::String& unit = "dB")
    {
        g.setFont (theme::mono (9.5f));
        for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        {
            const float x = freqToX (f, r);
            g.setColour (theme::grid);
            g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
            g.setColour (theme::textFaint);
            g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f),
                        juce::Rectangle<float> (x - 20.0f, r.getBottom() + 2.0f, 40.0f, 12.0f), juce::Justification::centred);
        }
        for (double db = dbMin; db <= dbMax + 1e-6; db += dbStep)
        {
            const float y = dbToY (db, r, dbMin, dbMax);
            g.setColour (std::abs (db) < 1e-6 ? theme::metal : theme::grid);
            g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
            g.setColour (theme::textFaint);
            g.drawText (juce::String ((int) db) + (db == dbMax ? " " + unit : ""),
                        juce::Rectangle<float> (r.getX() - 46.0f, y - 6.0f, 42.0f, 12.0f), juce::Justification::centredRight);
        }
    }
}
