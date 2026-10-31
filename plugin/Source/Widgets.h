#pragma once

#include "PluginProcessor.h"
#include "Theme.h"
#include "ParamHelp.h"

#include <sonic/Module.h>
#include <sonic/ModuleRegistry.h>

// Category colour of a block type (Character amber, Dynamics coral, Tone teal ...).
inline juce::Colour blockColour (const std::string& type)
{
    for (auto& m : sonic::availableModules())
        if (m.typeId == type)
            return theme::categoryColour (m.category);
    return theme::textDim;
}

// ------------------------------------------------------------------ formatting

inline juce::String formatValue (double v, const juce::String& unit)
{
    if (unit == "Hz")
        return v >= 1000.0 ? juce::String (v / 1000.0, v >= 10000.0 ? 1 : 2) + " kHz"
                           : numText (v, v < 100.0 ? 1 : 0) + " Hz";
    if (unit == "dB" || unit == "dBc")
        return (v > 0.0 && unit == "dB" ? "+" : "") + juce::String (v, 1) + " " + unit;
    if (unit == "%")
        return juce::String (juce::roundToInt (v)) + " %";
    if (unit == "ms")
        return numText (v, v < 10.0 ? 2 : (v < 100.0 ? 1 : 0)) + " ms";
    if (unit == "deg")
        return juce::String (juce::roundToInt (v)) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0"));
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

// ------------------------------------------------------------------ typing values

// Parses what a person types into a number in the parameter's unit:
// "2k" / "2.5 kHz" -> 2000 / 2500 (Hz), "-3" / "-3 dB", "50 %", "1:4" / "4:1" ratios.
inline double parseTypedValue (juce::String text, const juce::String& unit, double fallback)
{
    text = text.trim().toLowerCase().replace (",", ".");
    if (text.isEmpty()) return fallback;
    if (unit == ":1" && text.contains (":"))
    {
        const double a = text.upToFirstOccurrenceOf (":", false, false).getDoubleValue();
        const double b = text.fromFirstOccurrenceOf (":", false, false).getDoubleValue();
        return b == 1.0 || b == 0.0 ? a : (a == 1.0 ? b : a / b);
    }
    double mult = 1.0;
    if (text.containsChar ('k') && (unit == "Hz" || unit.isEmpty())) mult = 1000.0;
    if (unit == "ms" && text.endsWith ("s") && ! text.endsWith ("ms")) mult = 1000.0; // "0.5 s"
    const auto start = text.indexOfAnyOf ("+-.0123456789");
    if (start < 0) return fallback;
    return text.substring (start).getDoubleValue() * mult;
}

// Small text field that appears over a value. Enter applies; Esc, or a click anywhere
// else, cancels.
class InlineValueEditor : public juce::TextEditor
{
public:
    static void show (juce::Component& host, juce::Rectangle<int> areaInHost, const juce::String& current,
                      std::function<void (const juce::String&)> commit)
    {
        auto* e = new InlineValueEditor (std::move (commit));
        host.addAndMakeVisible (e);
        e->setBounds (areaInHost.withSizeKeepingCentre (juce::jmax (areaInHost.getWidth(), 76), juce::jmax (areaInHost.getHeight(), 22)));
        e->setText (current, false);
        e->selectAll();
        e->toFront (true);
        e->grabKeyboardFocus();
    }
    ~InlineValueEditor() override { juce::Desktop::getInstance().removeGlobalMouseListener (&outside); }

private:
    explicit InlineValueEditor (std::function<void (const juce::String&)> c) : commitFn (std::move (c))
    {
        setJustification (juce::Justification::centred);
        setFont (theme::mono (11.5f));
        setColour (juce::TextEditor::backgroundColourId, theme::card);
        setColour (juce::TextEditor::textColourId, theme::ink);
        setColour (juce::TextEditor::outlineColourId, theme::ink);
        setColour (juce::TextEditor::focusedOutlineColourId, theme::ink);
        setColour (juce::TextEditor::highlightColourId, theme::logo.withAlpha (0.45f));
        setColour (juce::CaretComponent::caretColourId, theme::ink);
        setSelectAllWhenFocused (true);
        onReturnKey = [this] { finish (true); };
        onEscapeKey = [this] { finish (false); };
        onFocusLost = [this] { finish (false); };
        juce::Desktop::getInstance().addGlobalMouseListener (&outside);
    }
    // a click anywhere outside the field cancels typing
    struct Outside : public juce::MouseListener
    {
        InlineValueEditor& owner;
        explicit Outside (InlineValueEditor& o) : owner (o) {}
        void mouseDown (const juce::MouseEvent& e) override
        {
            auto* c = e.eventComponent;
            if (c != &owner && ! owner.isParentOf (c))
                owner.finish (false);
        }
    } outside { *this };
    void finish (bool apply)
    {
        if (done) return;
        done = true;
        juce::Desktop::getInstance().removeGlobalMouseListener (&outside);
        if (apply && commitFn) commitFn (getText());
        juce::Component::SafePointer<juce::Component> self (this);
        juce::MessageManager::callAsync ([self] { delete self.getComponent(); });
    }
    std::function<void (const juce::String&)> commitFn;
    bool done = false;
};

// Slider used everywhere in Atelier:
//   drag / fine mouse-wheel (Shift = finer, Ctrl = coarser) to move,
//   double-click to reset to the default,
//   right-click (or click the number next to it) to type an exact value.
class AtelierSlider : public juce::Slider, private juce::Timer
{
public:
    AtelierSlider() { setSliderSnapsToMousePosition (false); setPopupMenuEnabled (false); }

    // Where the text field should appear, in this slider's coordinates (default: the slider itself).
    std::function<juce::Rectangle<int>()> valueArea;
    std::function<void()> onTypedValue;   // e.g. push an undo checkpoint (default: onDragEnd)

    void setDefault (double v) { defaultValue = v; hasDefault = true; setDoubleClickReturnValue (true, v, juce::ModifierKeys::altModifier); }
    double getDefault() const { return defaultValue; }

    void resetToDefault()
    {
        if (! hasDefault || ! isEnabled()) return;
        setValue (defaultValue, juce::sendNotificationSync);
        notifyEdit();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override { resetToDefault(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { beginTextEntry(); return; }
        juce::Slider::mouseDown (e);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (! isEnabled() || getMaximum() <= getMinimum())
        {
            juce::Component::mouseWheelMove (e, w);
            return;
        }
        float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? -w.deltaX : w.deltaY;
        if (w.isReversed) d = -d;
        if (d == 0.0f) return;
        // ~0.8 % of the travel per wheel notch (JUCE's default is ~3.5 %): fine enough to tune by ear
        double step = d * 0.034;
        if (e.mods.isShiftDown()) step *= 0.25;
        else if (e.mods.isCommandDown()) step *= 4.0;
        const double old = getValue();
        double v = proportionOfLengthToValue (juce::jlimit (0.0, 1.0, valueToProportionOfLength (old) + step));
        if (getInterval() > 0.0 && std::abs (snapValue (v, juce::Slider::notDragging) - old) < getInterval() * 0.5)
            v = old + (step > 0 ? getInterval() : -getInterval());
        setValue (juce::jlimit (getMinimum(), getMaximum(), v), juce::sendNotificationSync);
        startTimer (450);   // one undo step once the wheel rests
    }

    void beginTextEntry()
    {
        auto* host = getParentComponent();
        if (host == nullptr || ! isEnabled()) return;
        auto area = (valueArea ? valueArea() : getLocalBounds()) + getPosition();
        juce::Component::SafePointer<AtelierSlider> self (this);
        InlineValueEditor::show (*host, area, getTextFromValue (getValue()), [self] (const juce::String& t)
        {
            if (self == nullptr) return;
            const double v = juce::jlimit (self->getMinimum(), self->getMaximum(), self->getValueFromText (t));
            self->setValue (v, juce::sendNotificationSync);
            self->notifyEdit();
        });
    }

private:
    void notifyEdit()
    {
        if (onTypedValue) onTypedValue();
        else if (onDragEnd) onDragEnd();
    }
    void timerCallback() override { stopTimer(); notifyEdit(); }
    double defaultValue = 0.0;
    bool hasDefault = false;
};

// ------------------------------------------------------------------ module parameter controls

// Slider bound directly to a block parameter (thread-safe atomics in the module).
class ModuleSlider : public AtelierSlider
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
        setDefault (s.defaultValue);
        setTooltip (help::tooltip (mod, idx));
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
    double getValueFromText (const juce::String& t) override { return parseTypedValue (t, mod.params()[(size_t) idx].unit, getValue()); }
    const sonic::ParamSpec& spec() const { return mod.params()[(size_t) idx]; }
    sonic::Module& module() { return mod; }
    int index() const { return idx; }

private:
    AtelierProcessor& proc;
    sonic::Module& mod;
    int idx;
};

// Segmented buttons bound to a choice parameter; switches to a drop-down list when the
// options do not fit side by side.
class ModuleChoice : public juce::Component
{
public:
    ModuleChoice (AtelierProcessor& p, sonic::Module& m, int index) : proc (p), mod (m), idx (index)
    {
        const auto& s = mod.params()[(size_t) idx];
        for (size_t i = 0; i < s.choices.size(); ++i)
            combo.addItem (juce::String (s.choices[i]), (int) i + 1);
        combo.onChange = [this] { mod.setParam (idx, (float) (combo.getSelectedItemIndex())); proc.checkpoint(); };
        addChildComponent (combo);
        for (size_t i = 0; i < s.choices.size(); ++i)
        {
            auto* b = buttons.add (new juce::TextButton (juce::String (s.choices[i]).toUpperCase()));
            b->setClickingTogglesState (false);
            b->onClick = [this, i] { mod.setParam (idx, (float) i); proc.checkpoint(); sync(); };
            addChildComponent (b);
        }
        setTooltip (help::tooltip (mod, idx));
        sync();
    }

    void sync()
    {
        const int v = (int) mod.getParam (idx);
        if (combo.getSelectedItemIndex() != v)
            combo.setSelectedItemIndex (v, juce::dontSendNotification);
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setToggleState (i == v, juce::dontSendNotification);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const int n = juce::jmax (1, buttons.size());
        const int w = (r.getWidth() - (n - 1) * 4) / n;
        int widest = 0;
        for (auto* b : buttons)
            widest = juce::jmax (widest, juce::GlyphArrangement::getStringWidthInt (theme::mono (10.0f), b->getButtonText()) + 12);
        const bool useCombo = n > 5 || widest > w;
        combo.setVisible (useCombo);
        combo.setBounds (r);
        for (auto* b : buttons)
        {
            b->setVisible (! useCombo);
            b->setBounds (r.removeFromLeft (w));
            r.removeFromLeft (4);
        }
    }

    void setTooltip (const juce::String& t) { for (auto* b : buttons) b->setTooltip (t); combo.setTooltip (t); }
    void setAccent (juce::Colour c) { for (auto* b : buttons) b->getProperties().set ("colour", (int) c.getARGB()); repaint(); }

private:
    AtelierProcessor& proc;
    sonic::Module& mod;
    int idx;
    juce::ComboBox combo;
    juce::OwnedArray<juce::TextButton> buttons;
};

// Knob with name and value underneath.
class ModuleKnob : public juce::Component, public juce::SettableTooltipClient
{
public:
    ModuleKnob (AtelierProcessor& p, sonic::Module& m, int index, bool beige = false)
        : slider (p, m, index, true)
    {
        slider.getProperties().set ("beige", beige);
        slider.onValueChange = [this, &m, index] { m.setParam (index, (float) slider.getValue()); repaint(); };
        slider.valueArea = [this] { return getLocalBounds().removeFromBottom (18).reduced (6, 0) - slider.getPosition(); };
        setTooltip (slider.getTooltip());
        addAndMakeVisible (slider);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() || getLocalBounds().removeFromBottom (18).contains (e.getPosition()))
            slider.beginTextEntry();
    }
    void mouseDoubleClick (const juce::MouseEvent&) override { slider.resetToDefault(); }
    void sync()
    {
        slider.sync();
        const bool rel = slider.module().isParamRelevant (slider.index());
        if (rel != isEnabled()) { setEnabled (rel); setAlpha (rel ? 1.0f : 0.38f); }
        repaint();
    }
    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromBottom (34);
        slider.setBounds (r);
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().removeFromBottom (32);
        g.setColour (theme::ink);
        g.setFont (theme::heading (17.0f));
        g.drawText (juce::String (slider.spec().name), r.removeFromTop (17), juce::Justification::centred);
        g.setColour (theme::textDim);
        g.setFont (theme::mono (9.5f));
        g.drawText (slider.getTextFromValue (slider.getValue()), r, juce::Justification::centred);
    }
    ModuleSlider slider;
};

// A row: name | control | value. Greyed out when the block ignores it with the current settings.
class ModuleParamRow : public juce::Component, public juce::SettableTooltipClient
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
            slider->valueArea = [this] { return getLocalBounds().removeFromRight (valueW) - slider->getPosition(); };
        }
        if (choice) addAndMakeVisible (*choice);
        if (slider) addAndMakeVisible (*slider);
        setTooltip (help::tooltip (mod, idx));
        relevant = mod.isParamRelevant (idx);
        applyRelevance();
    }
    void sync()
    {
        if (choice) choice->sync();
        if (slider) slider->sync();
        const bool rel = mod.isParamRelevant (idx);
        if (rel != relevant)
        {
            relevant = rel;
            applyRelevance();
        }
        repaint();
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (slider && (e.mods.isPopupMenu() || getLocalBounds().removeFromRight (valueW).contains (e.getPosition())))
            slider->beginTextEntry();
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (slider && ! getLocalBounds().removeFromRight (valueW).contains (e.getPosition()))
            slider->resetToDefault();
    }
    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromLeft (labelW);
        if (choice) choice->setBounds (r.reduced (0, 3));
        if (slider) slider->setBounds (r.withTrimmedRight (valueW + 4));
    }
    void paint (juce::Graphics& g) override
    {
        const auto& s = mod.params()[(size_t) idx];
        g.setColour (theme::textDim);
        g.setFont (theme::sans (12.5f));
        juce::String name (s.name);
        if (name.startsWith ("Band ") && name.substring (5).containsChar (' '))   // "Band 3 freq" -> "Freq" (the band is shown above)
            name = name.substring (5).fromFirstOccurrenceOf (" ", false, false);
        name = name.substring (0, 1).toUpperCase() + name.substring (1);
        g.drawText (name, getLocalBounds().withWidth (labelW - 6), juce::Justification::centredLeft, true);
        if (slider)
        {
            auto v = getLocalBounds().removeFromRight (valueW);
            g.setColour (theme::text);
            g.setFont (theme::mono (10.5f));
            g.drawText (formatParam (s, mod.getParam (idx)), v, juce::Justification::centredRight);
        }
    }
    int labelW = 112, valueW = 70;
    int index() const { return idx; }

private:
    void applyRelevance()
    {
        setEnabled (relevant);
        setAlpha (relevant ? 1.0f : 0.38f);
        const auto tip = help::tooltip (mod, idx);
        setTooltip (tip);
        if (slider) slider->setTooltip (tip);
        if (choice) choice->setTooltip (tip);
    }
    sonic::Module& mod;
    int idx;
    bool relevant = true;
    std::unique_ptr<ModuleSlider> slider;
    std::unique_ptr<ModuleChoice> choice;
};

// ------------------------------------------------------------------ processor parameter controls

using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

class ApvtsRow : public juce::Component, public juce::SettableTooltipClient
{
public:
    ApvtsRow (juce::AudioProcessorValueTreeState& st, const juce::String& id, const juce::String& caption_, const juce::String& tip = {})
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
            slider.setDefault (param->convertFrom0to1 (param->getDefaultValue()));
            slider.valueArea = [this] { return getLocalBounds().removeFromRight (74) - slider.getPosition(); };
            slider.onValueChange = [this] { repaint(); };
        }
        if (tip.isNotEmpty()) { setTooltip (tip); slider.setTooltip (tip); combo.setTooltip (tip); }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! combo.isVisible() && (e.mods.isPopupMenu() || getLocalBounds().removeFromRight (74).contains (e.getPosition())))
            slider.beginTextEntry();
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (! combo.isVisible() && ! getLocalBounds().removeFromRight (74).contains (e.getPosition())) slider.resetToDefault();
    }
    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromLeft (100);
        if (combo.isVisible()) combo.setBounds (r.reduced (0, 3));
        else slider.setBounds (r.withTrimmedRight (78));
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
    AtelierSlider slider;
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
    // Level (dB) of a linear-bin spectrum at the column [f0, f1], smoothed over 1/12 octave
    // (power average) - so curves read like an analyser, not like noise. Where the band is
    // narrower than a bin (the bass), it interpolates smoothly between neighbouring bins.
    inline float sampleSpectrum (const std::vector<float>& s, double binHz, double f0, double f1)
    {
        if (s.size() < 4 || binHz <= 0.0) return -200.0f;
        const int last = (int) s.size() - 1;
        const double fc = std::sqrt (f0 * f1);
        const double lo = juce::jmin (f0, fc / 1.0293), hi = juce::jmax (f1, fc * 1.0293);   // >= 1/12 octave
        const double k0 = lo / binHz, k1 = hi / binHz;
        const int a = juce::jmax (1, (int) std::ceil (k0)), b = juce::jmin (last, (int) std::floor (k1));
        if (b - a >= 1)
        {
            double p = 0.0;
            for (int k = a; k <= b; ++k) p += std::pow (10.0, s[(size_t) k] * 0.1);
            return (float) (10.0 * std::log10 (juce::jmax (1e-20, p / (b - a + 1))));
        }
        const double kc = juce::jlimit (1.0, (double) last - 1.0, fc / binHz);
        const int i = (int) kc;
        const double t = kc - i;
        const double w = 0.5 - 0.5 * std::cos (t * juce::MathConstants<double>::pi);   // cosine: round peaks
        return (float) ((1.0 - w) * s[(size_t) i] + w * s[(size_t) i + 1]);
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
