#pragma once

#include <JuceHeader.h>

// ATELIER visual language: near-black graphite, charcoal panels, hairline
// metallic edges, one restrained violet accent for "processed / output",
// cool grey for "reference / input".
namespace theme
{
    const juce::Colour bg        { 0xff0f1012 };
    const juce::Colour panel     { 0xff17181b };
    const juce::Colour panelHi   { 0xff1e2024 };
    const juce::Colour edge      { 0xff2a2d32 };
    const juce::Colour metal     { 0xff3a3e45 };
    const juce::Colour grid      { 0xff22252a };
    const juce::Colour text      { 0xffdadce0 };
    const juce::Colour textDim   { 0xff8b9098 };
    const juce::Colour textFaint { 0xff5a5f67 };
    const juce::Colour accent    { 0xff9a86f2 }; // processed / output (violet)
    const juce::Colour accentDim { 0xff5d5196 };
    const juce::Colour reference { 0xff7f8996 }; // input / reference
    const juce::Colour good      { 0xff62b882 };
    const juce::Colour warn      { 0xffd9ae55 };
    const juce::Colour bad       { 0xffdd5a50 };

    inline juce::Font label (float h = 13.0f) { return juce::Font (juce::FontOptions (h)); }
    inline juce::Font bold (float h = 11.0f) { return juce::Font (juce::FontOptions (h, juce::Font::bold)); }
    inline juce::Font mono (float h = 12.0f)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain));
    }

    inline void drawPanel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& caption)
    {
        g.setColour (panel);
        g.fillRoundedRectangle (r.toFloat(), 3.0f);
        g.setColour (edge);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, 1.0f);
        // subtle top metallic hairline
        g.setColour (metal.withAlpha (0.35f));
        g.drawHorizontalLine (r.getY() + 1, (float) r.getX() + 4.0f, (float) r.getRight() - 4.0f);
        if (caption.isNotEmpty())
        {
            g.setColour (textDim);
            g.setFont (bold (10.5f));
            g.drawText (caption.toUpperCase(), r.reduced (12, 0).withHeight (28), juce::Justification::centredLeft);
        }
    }
}

class AtelierLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AtelierLookAndFeel()
    {
        using namespace theme;
        setColour (juce::ResizableWindow::backgroundColourId, bg);
        setColour (juce::Label::textColourId, text);
        setColour (juce::Slider::textBoxTextColourId, text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, accent.withAlpha (0.3f));
        setColour (juce::TextButton::buttonColourId, panelHi);
        setColour (juce::TextButton::buttonOnColourId, accentDim);
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, text);
        setColour (juce::ComboBox::backgroundColourId, panelHi);
        setColour (juce::ComboBox::outlineColourId, edge);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::arrowColourId, textDim);
        setColour (juce::PopupMenu::backgroundColourId, panelHi);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.22f));
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedTextColourId, text);
        setColour (juce::ToggleButton::textColourId, text);
        setColour (juce::ToggleButton::tickColourId, accent);
        setColour (juce::ToggleButton::tickDisabledColourId, textDim);
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff23252a));
        setColour (juce::TooltipWindow::textColourId, text);
        setColour (juce::TooltipWindow::outlineColourId, edge);
        setColour (juce::TextEditor::backgroundColourId, panelHi);
        setColour (juce::TextEditor::outlineColourId, edge);
        setColour (juce::TextEditor::textColourId, text);
        setColour (juce::AlertWindow::backgroundColourId, panel);
        setColour (juce::AlertWindow::textColourId, text);
        setColour (juce::ScrollBar::thumbColourId, edge);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
                           float minPos, float maxPos, juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style != juce::Slider::LinearHorizontal)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, sliderPos, minPos, maxPos, style, s);
            return;
        }
        const float cy = (float) y + (float) h * 0.5f;
        const float x0 = (float) x + 2.0f, x1 = (float) (x + w) - 2.0f;

        g.setColour (theme::edge);
        g.fillRoundedRectangle (x0, cy - 1.5f, x1 - x0, 3.0f, 1.5f);

        // Fill from zero for bipolar ranges, from the minimum otherwise.
        const double zeroVal = (s.getMinimum() < 0.0 && s.getMaximum() > 0.0) ? 0.0 : s.getMinimum();
        const float zx = (float) s.getPositionOfValue (zeroVal) + (float) x;
        g.setColour (s.isEnabled() ? theme::accent.withAlpha (0.85f) : theme::textFaint);
        g.fillRoundedRectangle (juce::jmin (zx, sliderPos), cy - 1.5f, std::abs (sliderPos - zx), 3.0f, 1.5f);

        // Machined thumb
        juce::ColourGradient grad (juce::Colour (0xffe6e7ea), sliderPos, cy - 6.0f, juce::Colour (0xff9da1a8), sliderPos, cy + 6.0f, false);
        g.setGradientFill (grad);
        g.fillEllipse (sliderPos - 6.0f, cy - 6.0f, 12.0f, 12.0f);
        g.setColour (theme::bg);
        g.drawEllipse (sliderPos - 6.0f, cy - 6.0f, 12.0f, 12.0f, 1.0f);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bgColour,
                               bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto c = bgColour;
        if (down) c = c.brighter (0.15f);
        else if (highlighted) c = c.brighter (0.07f);
        g.setColour (c);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (b.getToggleState() ? theme::accent : theme::edge);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return theme::label (juce::jmin (13.0f, (float) h * 0.55f)); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return theme::label (13.0f); }
    juce::Font getPopupMenuFont() override { return theme::label (13.0f); }
    juce::Font getLabelFont (juce::Label&) override { return theme::label (13.0f); }
};
