#pragma once

#include <JuceHeader.h>

#if __has_include(<BinaryData.h>)
 #include <BinaryData.h>
 #define ATELIER_HAS_FONTS 1
#else
 #define ATELIER_HAS_FONTS 0
#endif

// Atelier visual language (approved concept, dark):
// near-black blue ground, charcoal panels, hairline edges, muted grey-blue text,
// dreamy light blue for signalBlue/curves, beige for identity and selection.
// Nothing bright in large areas.
namespace theme
{
    const juce::Colour bg        { 0xff07090c };
    const juce::Colour panel     { 0xff0c1117 };
    const juce::Colour card      { 0xff111821 };
    const juce::Colour edge      { 0xff1e2834 };
    const juce::Colour dashed    { 0xff18212c };
    const juce::Colour grid      { 0xff131b24 };
    const juce::Colour text      { 0xffafbbc9 };
    const juce::Colour textDim   { 0xff738398 };
    const juce::Colour light     { 0xffa9b9cc };
    const juce::Colour selFill   { 0xff1c2838 };
    const juce::Colour selEdge   { 0xff4e6a90 };
    const juce::Colour signalBlue    { 0xff7894bc }; // output / processed curve
    const juce::Colour reference { 0xff4a596b }; // input curve
    const juce::Colour parallel  { 0xff62809f };
    const juce::Colour beige     { 0xffb9a27c };
    const juce::Colour beigeText { 0xffa08b69 };
    const juce::Colour beigeFill { 0xff1e1910 };
    const juce::Colour beigeTag  { 0xff2a2316 };
    const juce::Colour beigeEdge { 0xff8a7550 };
    const juce::Colour good      { 0xff76a585 };
    const juce::Colour goodEdge  { 0xff5f8a6c };
    const juce::Colour warn      { 0xffb59a5e };
    const juce::Colour bad       { 0xffb0645c };
    const juce::Colour shadow    { 0xff16233a };
    const juce::Colour rowSel    { 0xff1e1a10 };
    const juce::Colour accent    { 0xffb9a27c };

    struct Faces
    {
        juce::Typeface::Ptr serif, mono, monoMedium, sans, sansMedium, sansSemi;
        Faces()
        {
           #if ATELIER_HAS_FONTS
            auto load = [] (const void* d, int n) { return juce::Typeface::createSystemTypefaceFor (d, (size_t) n); };
            serif = load (BinaryData::CormorantGaramondSemiBoldItalic_ttf, BinaryData::CormorantGaramondSemiBoldItalic_ttfSize);
            mono = load (BinaryData::IBMPlexMonoRegular_ttf, BinaryData::IBMPlexMonoRegular_ttfSize);
            monoMedium = load (BinaryData::IBMPlexMonoMedium_ttf, BinaryData::IBMPlexMonoMedium_ttfSize);
            sans = load (BinaryData::InterTightRegular_ttf, BinaryData::InterTightRegular_ttfSize);
            sansMedium = load (BinaryData::InterTightMedium_ttf, BinaryData::InterTightMedium_ttfSize);
            sansSemi = load (BinaryData::InterTightSemiBold_ttf, BinaryData::InterTightSemiBold_ttfSize);
           #endif
        }
    };
    inline const Faces& faces() { static Faces f; return f; }

    inline juce::Font make (juce::Typeface::Ptr tf, float h, const char* fallbackName, int style)
    {
        if (tf != nullptr)
            return juce::Font (juce::FontOptions (tf).withHeight (h));
        return juce::Font (juce::FontOptions (fallbackName, h, style));
    }

    // Wordmark / display serif (italic)
    inline juce::Font serif (float h) { return make (faces().serif, h, "Georgia", juce::Font::italic | juce::Font::bold); }
    // Technical labels: monospace, small caps style (callers upper-case text)
    inline juce::Font mono (float h) { return make (faces().mono, h, "Consolas", juce::Font::plain); }
    inline juce::Font monoMedium (float h) { return make (faces().monoMedium, h, "Consolas", juce::Font::bold); }
    // UI text
    inline juce::Font sans (float h) { return make (faces().sans, h, "Segoe UI", juce::Font::plain); }
    inline juce::Font sansMedium (float h) { return make (faces().sansMedium, h, "Segoe UI Semibold", juce::Font::plain); }
    inline juce::Font sansSemi (float h) { return make (faces().sansSemi, h, "Segoe UI", juce::Font::bold); }

    // "LABEL" in quotes, as in the concept
    inline juce::String q (const juce::String& s) { return "\"" + s.toUpperCase() + "\""; }

    // Panel with dashed header rule and optional caption + figure note.
    inline void drawPanel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& caption = {},
                           const juce::String& figure = {}, int headerH = 40)
    {
        g.setColour (panel);
        g.fillRect (r);
        g.setColour (edge);
        g.drawRect (r, 1);
        if (caption.isEmpty())
            return;
        auto h = r.withHeight (headerH).reduced (16, 0);
        g.setColour (text);
        g.setFont (mono (11.0f));
        const juce::String cap = q (caption);
        g.drawText (cap, h, juce::Justification::centredLeft);
        if (figure.isNotEmpty())
        {
            g.setColour (textDim);
            g.setFont (mono (10.0f));
            const int w = juce::GlyphArrangement::getStringWidthInt (mono (11.0f), cap);
            g.drawText (figure.toUpperCase(), h.withTrimmedLeft (w + 22), juce::Justification::centredLeft);
        }
        // dashed rule
        const float y = (float) r.getY() + (float) headerH - 0.5f;
        g.setColour (dashed);
        for (float x = (float) r.getX() + 1.0f; x < (float) r.getRight() - 1.0f; x += 6.0f)
            g.drawLine (x, y, juce::jmin (x + 3.0f, (float) r.getRight() - 1.0f), y, 1.0f);
    }

    inline void drawTag (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& t, juce::Colour fg, juce::Colour edgeC, juce::Colour fill)
    {
        g.setColour (fill);
        g.fillRect (r);
        g.setColour (edgeC);
        g.drawRect (r, 1);
        g.setColour (fg);
        g.setFont (mono (9.5f));
        g.drawText (t, r, juce::Justification::centred);
    }

    inline void cropMarks (juce::Graphics& g, juce::Rectangle<int> r)
    {
        g.setColour (textDim.withAlpha (0.7f));
        const float x0 = (float) r.getX(), y0 = (float) r.getY(), x1 = (float) r.getRight(), y1 = (float) r.getBottom();
        g.drawLine (x0 - 9, y0 - 1, x0 - 1, y0 - 1, 1.0f);
        g.drawLine (x0 - 1, y0 - 9, x0 - 1, y0 - 1, 1.0f);
        g.drawLine (x1 + 1, y1 + 1, x1 + 9, y1 + 1, 1.0f);
        g.drawLine (x1 + 1, y1 + 1, x1 + 1, y1 + 9, 1.0f);
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
        setColour (juce::Slider::textBoxHighlightColourId, selEdge.withAlpha (0.4f));
        setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        setColour (juce::TextButton::buttonOnColourId, selFill);
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, light);
        setColour (juce::ComboBox::backgroundColourId, card);
        setColour (juce::ComboBox::outlineColourId, edge);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::arrowColourId, textDim);
        setColour (juce::PopupMenu::backgroundColourId, card);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, selFill);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedTextColourId, light);
        setColour (juce::PopupMenu::headerTextColourId, textDim);
        setColour (juce::ToggleButton::textColourId, text);
        setColour (juce::ToggleButton::tickColourId, light);
        setColour (juce::ToggleButton::tickDisabledColourId, textDim);
        setColour (juce::TooltipWindow::backgroundColourId, card);
        setColour (juce::TooltipWindow::textColourId, text);
        setColour (juce::TooltipWindow::outlineColourId, edge);
        setColour (juce::TextEditor::backgroundColourId, card);
        setColour (juce::TextEditor::outlineColourId, edge);
        setColour (juce::TextEditor::focusedOutlineColourId, selEdge);
        setColour (juce::TextEditor::textColourId, text);
        setColour (juce::TextEditor::highlightColourId, selFill);
        setColour (juce::CaretComponent::caretColourId, light);
        setColour (juce::AlertWindow::backgroundColourId, panel);
        setColour (juce::AlertWindow::textColourId, text);
        setColour (juce::AlertWindow::outlineColourId, edge);
        setColour (juce::ScrollBar::thumbColourId, edge);
        setColour (juce::ListBox::backgroundColourId, panel);
        setColour (juce::ProgressBar::backgroundColourId, card);
        setColour (juce::ProgressBar::foregroundColourId, signalBlue);
        setDefaultSansSerifTypeface (theme::faces().sans);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return theme::mono (juce::jmin (11.0f, (float) h * 0.45f)); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return theme::sans (13.0f); }
    juce::Font getPopupMenuFont() override { return theme::sans (13.5f); }
    juce::Font getLabelFont (juce::Label&) override { return theme::sans (13.0f); }
    juce::Font getAlertWindowMessageFont() override { return theme::sans (14.0f); }
    juce::Font getAlertWindowTitleFont() override { return theme::sansSemi (16.0f); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hi, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        const bool beigeStyle = (bool) b.getProperties().getWithDefault ("beige", false);
        juce::Colour fill = on ? theme::selFill : juce::Colours::transparentBlack;
        juce::Colour edgeC = on ? theme::selEdge : theme::edge;
        if (beigeStyle) { fill = theme::beigeFill; edgeC = theme::beigeEdge; }
        if (down) fill = fill.brighter (0.06f);
        else if (hi) fill = fill.isTransparent() ? theme::card : fill.brighter (0.03f);
        g.setColour (fill);
        g.fillRect (r);
        g.setColour (edgeC);
        g.drawRect (r, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        const bool beigeStyle = (bool) b.getProperties().getWithDefault ("beige", false);
        g.setColour (beigeStyle ? theme::beige : (b.getToggleState() ? theme::light : theme::text).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 0), juce::Justification::centred, 1);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) override
    {
        g.setColour (theme::card);
        g.fillRect (0, 0, w, h);
        g.setColour (theme::edge);
        g.drawRect (0, 0, w, h, 1);
        juce::Path p;
        const float cx = (float) w - 12.0f, cy = (float) h * 0.5f;
        p.addTriangle (cx - 4, cy - 2, cx + 4, cy - 2, cx, cy + 3);
        g.setColour (theme::textDim);
        g.fillPath (p);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        const float size = (float) juce::jmin (w, h);
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).withSizeKeepingCentre (size, size).reduced (3.0f);
        const float cx = r.getCentreX(), cy = r.getCentreY(), rad = r.getWidth() * 0.5f;
        const float stroke = juce::jmax (2.5f, rad * 0.12f);
        const bool beigeArc = (bool) s.getProperties().getWithDefault ("beige", false);

        juce::Path track;
        track.addCentredArc (cx, cy, rad - stroke * 0.5f, rad - stroke * 0.5f, 0.0f, a0, a1, true);
        g.setColour (theme::grid);
        g.strokePath (track, juce::PathStrokeType (stroke));

        const double zero = (s.getMinimum() < 0.0 && s.getMaximum() > 0.0) ? 0.0 : s.getMinimum();
        const float az = a0 + (float) s.valueToProportionOfLength (zero) * (a1 - a0);
        const float av = a0 + pos * (a1 - a0);
        juce::Path val;
        val.addCentredArc (cx, cy, rad - stroke * 0.5f, rad - stroke * 0.5f, 0.0f, juce::jmin (az, av), juce::jmax (az, av), true);
        g.setColour (beigeArc ? theme::beige : theme::text);
        g.strokePath (val, juce::PathStrokeType (stroke));

        const float inner = rad * 0.64f;
        g.setColour (theme::card);
        g.fillEllipse (cx - inner, cy - inner, inner * 2, inner * 2);
        g.setColour (theme::edge);
        g.drawEllipse (cx - inner, cy - inner, inner * 2, inner * 2, 1.0f);
        g.setColour (theme::text);
        const float px = cx + std::sin (av) * inner * 0.8f, py = cy - std::cos (av) * inner * 0.8f;
        g.drawLine (cx + std::sin (av) * inner * 0.2f, cy - std::cos (av) * inner * 0.2f, px, py, 2.0f);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style != juce::Slider::LinearHorizontal)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
            return;
        }
        const float cy = (float) y + (float) h * 0.5f;
        g.setColour (theme::edge);
        g.fillRect ((float) x, cy - 2.0f, (float) w, 4.0f);
        const double zero = (s.getMinimum() < 0.0 && s.getMaximum() > 0.0) ? 0.0 : s.getMinimum();
        const float zx = (float) s.getPositionOfValue (zero);
        g.setColour (theme::beige);
        g.fillRect (juce::jmin (zx, pos), cy - 2.0f, std::abs (pos - zx), 4.0f);
        g.setColour (theme::light);
        g.fillRect (pos - 1.5f, cy - 6.0f, 3.0f, 12.0f);
    }

    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (theme::mono (10.5f));
        l->setColour (juce::Label::textColourId, theme::textDim);
        l->setJustificationType (juce::Justification::centred);
        return l;
    }

    int getSliderThumbRadius (juce::Slider&) override { return 6; }
};
