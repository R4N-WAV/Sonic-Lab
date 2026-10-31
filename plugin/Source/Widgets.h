#pragma once

#include "PluginProcessor.h"
#include "Theme.h"

#include <sonic/Module.h>

// ------------------------------------------------------------------ formatting

inline juce::String formatValue (double v, const juce::String& unit)
{
    if (unit == "Hz")
        return v >= 1000.0 ? juce::String (v / 1000.0, v >= 10000.0 ? 1 : 2) + " kHz"
                           : juce::String (v, v < 100.0 ? 1 : 0) + " Hz";
    if (unit == "dB" || unit == "dBc")
        return (v > 0.0 && unit == "dB" ? "+" : "") + juce::String (v, 1) + " " + unit;
    if (unit == "%")
        return juce::String (v, 0) + " %";
    if (unit == "ms")
        return juce::String (v, v < 10.0 ? 2 : (v < 100.0 ? 1 : 0)) + " ms";
    if (unit == "deg")
        return juce::String (v, 0) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0"));
    if (unit == ":1")
        return juce::String (v, 1) + ":1";
    if (unit == "s" || unit == "x" || unit == "oct")
        return juce::String (v, 2) + " " + unit;
    return juce::String (v, 2) + (unit.isEmpty() ? "" : " " + unit);
}

inline juce::String formatParam (const sonic::ParamSpec& s, float v)
{
    if (s.isChoice())
        return juce::String (s.choices[(size_t) juce::jlimit (0, (int) s.choices.size() - 1, (int) v)]);
    return formatValue (v, s.unit);
}

inline juce::NormalisableRange<double> rangeFor (const sonic::ParamSpec& s)
{
    juce::NormalisableRange<double> r (s.minValue, s.maxValue);
    if (s.skewCentre > s.minValue && s.skewCentre < s.maxValue)
        r.setSkewForCentre (s.skewCentre);
    else if (s.skewCentre < 0.0f && s.unit == "dBc")
        r.setSkewForCentre (-30.0);
    return r;
}

// ------------------------------------------------------------------ module parameter controls

// Slider bound directly to a block parameter (thread-safe atomics in the module).
class ModuleSlider : public juce::Slider
{
public:
    ModuleSlider (AtelierProcessor& p, sonic::Module& m, int index, bool rotary)
        : proc (p), mod (m), idx (index)
    {
        const auto& s = mod.params()[(size_t) idx];
        setSliderStyle (rotary ? juce::Slider::RotaryVerticalDrag : juce::Slider::LinearHorizontal);
        setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        setNormalisableRange (rangeFor (s));
        setValue (mod.getParam (idx), juce::dontSendNotification);
        setDoubleClickReturnValue (true, s.defaultValue);
        setTooltip (juce::String (s.name) + ": " + juce::String (s.description));
        setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        onValueChange = [this] { mod.setParam (idx, (float) getValue()); };
        onDragEnd = [this] { proc.checkpoint(); };
    }

    void sync()
    {
        if (! isMouseButtonDown() && std::abs (getValue() - mod.getParam (idx)) > 1e-6)
            setValue (mod.getParam (idx), juce::dontSendNotification);
    }

    juce::String getTextFromValue (double v) override { return formatParam (mod.params()[(size_t) idx], (float) v); }
    const sonic::ParamSpec& spec() const { return mod.params()[(size_t) idx]; }

private:
    AtelierProcessor& proc;
    sonic::Module& mod;
    int idx;
};

// Segmented buttons (or a combo box for long lists) bound to a choice parameter.
class ModuleChoice : public juce::Component
{
public:
    ModuleChoice (AtelierProcessor& p, sonic::Module& m, int index) : proc (p), mod (m), idx (index)
    {
        const auto& s = mod.params()[(size_t) idx];
        useCombo = s.choices.size() > 5;
        if (useCombo)
        {
            for (size_t i = 0; i < s.choices.size(); ++i)
                combo.addItem (juce::String (s.choices[i]), (int) i + 1);
            combo.onChange = [this] { mod.setParam (idx, (float) (combo.getSelectedItemIndex())); proc.checkpoint(); };
            addAndMakeVisible (combo);
        }
        else
        {
            for (size_t i = 0; i < s.choices.size(); ++i)
            {
                auto* b = buttons.add (new juce::TextButton (juce::String (s.choices[i]).toUpperCase()));
                b->setClickingTogglesState (false);
                b->onClick = [this, i] { mod.setParam (idx, (float) i); proc.checkpoint(); sync(); };
                addAndMakeVisible (b);
            }
        }
        setTooltip (juce::String (s.name) + ": " + juce::String (s.description));
        sync();
    }

    void sync()
    {
        const int v = (int) mod.getParam (idx);
        if (useCombo)
        {
            if (combo.getSelectedItemIndex() != v)
                combo.setSelectedItemIndex (v, juce::dontSendNotification);
        }
        else
            for (int i = 0; i < buttons.size(); ++i)
                buttons[i]->setToggleState (i == v, juce::dontSendNotification);
    }

    void resized() override
    {
        if (useCombo)
        {
            combo.setBounds (getLocalBounds());
            return;
        }
        auto r = getLocalBounds();
        const int n = juce::jmax (1, buttons.size());
        const int w = (r.getWidth() - (n - 1) * 4) / n;
        for (auto* b : buttons)
        {
            b->setBounds (r.removeFromLeft (w));
            r.removeFromLeft (4);
        }
    }

    void setTooltip (const juce::String& t) { for (auto* b : buttons) b->setTooltip (t); combo.setTooltip (t); }

private:
    AtelierProcessor& proc;
    sonic::Module& mod;
    int idx;
    bool useCombo = false;
    juce::ComboBox combo;
    juce::OwnedArray<juce::TextButton> buttons;
};

// Knob with name and value underneath (SIMPLE view).
class ModuleKnob : public juce::Component
{
public:
    ModuleKnob (AtelierProcessor& p, sonic::Module& m, int index, bool beige = false)
        : slider (p, m, index, true)
    {
        slider.getProperties().set ("beige", beige);
        slider.onValueChange = [this, &m, index] { m.setParam (index, (float) slider.getValue()); repaint(); };
        addAndMakeVisible (slider);
    }
    void sync() { slider.sync(); repaint(); }
    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromBottom (34);
        slider.setBounds (r);
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().removeFromBottom (32);
        g.setColour (theme::text);
        g.setFont (theme::sansMedium (12.5f));
        g.drawText (juce::String (slider.spec().name), r.removeFromTop (16), juce::Justification::centred);
        g.setColour (theme::textDim);
        g.setFont (theme::mono (10.0f));
        g.drawText (slider.getTextFromValue (slider.getValue()), r, juce::Justification::centred);
    }
    ModuleSlider slider;
};

// A row in the ADVANCED list: name | control | value.
class ModuleParamRow : public juce::Component
{
public:
    ModuleParamRow (AtelierProcessor& p, sonic::Module& m, int index) : mod (m), idx (index)
    {
        const auto& s = m.params()[(size_t) index];
        if (s.isChoice())
            choice = std::make_unique<ModuleChoice> (p, m, index);
        else
        {
            slider = std::make_unique<ModuleSlider> (p, m, index, false);
            slider->onValueChange = [this] { mod.setParam (idx, (float) slider->getValue()); repaint(); };
        }
        if (choice) addAndMakeVisible (*choice);
        if (slider) addAndMakeVisible (*slider);
    }
    void sync()
    {
        if (choice) choice->sync();
        if (slider) slider->sync();
        repaint();
    }
    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromLeft (labelW);
        if (choice) choice->setBounds (r.reduced (0, 3));
        if (slider) slider->setBounds (r.withTrimmedRight (valueW));
    }
    void paint (juce::Graphics& g) override
    {
        const auto& s = mod.params()[(size_t) idx];
        g.setColour (theme::textDim);
        g.setFont (theme::sans (12.5f));
        g.drawText (juce::String (s.name), getLocalBounds().withWidth (labelW - 6), juce::Justification::centredLeft, true);
        if (slider)
        {
            g.setColour (theme::text);
            g.setFont (theme::mono (10.5f));
            g.drawText (formatParam (s, mod.getParam (idx)), getLocalBounds().removeFromRight (valueW), juce::Justification::centredRight);
        }
    }
    int labelW = 112, valueW = 70;

private:
    sonic::Module& mod;
    int idx;
    std::unique_ptr<ModuleSlider> slider;
    std::unique_ptr<ModuleChoice> choice;
};

// ------------------------------------------------------------------ processor parameter controls

using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

class ApvtsRow : public juce::Component
{
public:
    ApvtsRow (juce::AudioProcessorValueTreeState& st, const juce::String& id, const juce::String& caption_)
        : caption (caption_)
    {
        param = st.getParameter (id);
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (param))
        {
            combo.addItemList (c->choices, 1);
            addAndMakeVisible (combo);
            comboAtt = std::make_unique<ComboAttachment> (st, id, combo);
        }
        else
        {
            slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            addAndMakeVisible (slider);
            sliderAtt = std::make_unique<SliderAttachment> (st, id, slider);
            slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
            slider.onValueChange = [this] { repaint(); };
        }
    }
    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromLeft (100);
        if (combo.isVisible()) combo.setBounds (r.reduced (0, 3));
        else slider.setBounds (r.withTrimmedRight (74));
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::textDim);
        g.setFont (theme::sans (12.5f));
        g.drawText (caption, getLocalBounds().withWidth (96), juce::Justification::centredLeft);
        if (! combo.isVisible())
        {
            g.setColour (theme::text);
            g.setFont (theme::mono (10.5f));
            g.drawText (param->getCurrentValueAsText(), getLocalBounds().removeFromRight (74), juce::Justification::centredRight);
        }
    }

private:
    juce::String caption;
    juce::RangedAudioParameter* param = nullptr;
    juce::Slider slider;
    juce::ComboBox combo;
    std::unique_ptr<SliderAttachment> sliderAtt;
    std::unique_ptr<ComboAttachment> comboAtt;
};

// Small level meter (vertical stereo bars), used on Input/Output nodes.
inline void drawMiniMeter (juce::Graphics& g, juce::Rectangle<float> r, float level)
{
    g.setColour (theme::grid);
    g.fillRect (r);
    const float db = juce::jlimit (-60.0f, 0.0f, juce::Decibels::gainToDecibels (level, -60.0f));
    g.setColour (level >= 1.0f ? theme::bad : theme::signalBlue);
    g.fillRect (r.withWidth (r.getWidth() * (db + 60.0f) / 60.0f));
}

// Log-frequency helpers
namespace plot
{
    inline float freqToX (double f, juce::Rectangle<float> r, double fMin = 20.0, double fMax = 20000.0)
    {
        return r.getX() + (float) (std::log (f / fMin) / std::log (fMax / fMin)) * r.getWidth();
    }
    inline double xToFreq (float x, juce::Rectangle<float> r, double fMin = 20.0, double fMax = 20000.0)
    {
        return fMin * std::pow (fMax / fMin, (double) ((x - r.getX()) / r.getWidth()));
    }
    inline float dbToY (double db, juce::Rectangle<float> r, double dbMin, double dbMax)
    {
        return r.getBottom() - (float) ((db - dbMin) / (dbMax - dbMin)) * r.getHeight();
    }
    inline double yToDb (float y, juce::Rectangle<float> r, double dbMin, double dbMax)
    {
        return dbMin + (double) ((r.getBottom() - y) / r.getHeight()) * (dbMax - dbMin);
    }
    inline juce::String noteName (double f)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int midi = (int) std::lround (69.0 + 12.0 * std::log2 (f / 440.0));
        if (midi < 0 || midi > 135) return {};
        return juce::String (names[midi % 12]) + juce::String (midi / 12 - 1);
    }
    inline void drawFreqGrid (juce::Graphics& g, juce::Rectangle<float> r, bool labels = true)
    {
        g.setFont (theme::mono (9.5f));
        for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            const float x = freqToX (f, r);
            g.setColour (theme::grid);
            g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        }
        if (labels)
            for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
            {
                const float x = freqToX (f, r);
                g.setColour (theme::textDim);
                g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f),
                            juce::Rectangle<float> (x - 18.0f, r.getBottom() + 4.0f, 36.0f, 12.0f), juce::Justification::centred);
            }
    }
}
