#pragma once

#include <JuceHeader.h>

#if __has_include(<BinaryData.h>)
 #include <BinaryData.h>
 #define ATELIER_HAS_FONTS 1
#else
 #define ATELIER_HAS_FONTS 0
#endif

// Number text with a fixed number of decimals (0 = rounded integer; juce::String (x, 0)
// would print full precision instead).
inline juce::String numText (double v, int decimals)
{
    return decimals <= 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, decimals);
}

// Atelier visual language, v0.3 ("lights on"):
// deep slate ground (lifted from near-black), soft rounded panels with depth,
// a glowing light-blue signal, warm amber for interaction and selection, a beige
// italic serif wordmark, and one colour per block category. Colour lives in lines,
// edges, arcs and small fills - never in large bright areas. Off-White details stay:
// "QUOTED" labels and FIG. captions.
namespace theme
{
    // ground and surfaces
    const juce::Colour bg        { 0xff0f131b };
    const juce::Colour panel     { 0xff161c27 };
    const juce::Colour card      { 0xff1c2432 };   // raised surface (cards, controls)
    const juce::Colour edge      { 0xff2a3446 };
    const juce::Colour edgeHi    { 0xff3a4862 };
    const juce::Colour grid      { 0xff1e2633 };
    const juce::Colour gridMajor { 0xff2b3547 };
    const juce::Colour dashed    { 0xff2a3446 };

    // text
    const juce::Colour text      { 0xffd3dbe7 };
    const juce::Colour textDim   { 0xff8e9aae };
    const juce::Colour textFaint { 0xff5e6a80 };
    const juce::Colour light     { 0xffe8edf5 };

    // signal + interaction
    const juce::Colour signalBlue { 0xff86b6ff };  // output / processed curve
    const juce::Colour signalDeep { 0xff4f7bd9 };
    const juce::Colour reference  { 0xff6c7a92 };  // input curve
    const juce::Colour accent     { 0xfff2a766 };  // interaction, selection, EQ curve
    const juce::Colour logo       { 0xffd9bf8f };  // wordmark, Input/Output labels
    const juce::Colour parallel   { 0xffa68cff };  // parallel wires

    // status
    const juce::Colour good      { 0xff6fd49a };
    const juce::Colour goodEdge  { 0xff4fa878 };
    const juce::Colour warn      { 0xffe8b65a };
    const juce::Colour bad       { 0xffff7a7a };
    const juce::Colour danger    { 0xffff8a7a };

    // block categories
    const juce::Colour catCharacter  { 0xfff2a766 };
    const juce::Colour catDynamics   { 0xffff8a7a };
    const juce::Colour catTone       { 0xff5ed1c0 };
    const juce::Colour catSpace      { 0xffa68cff };
    const juce::Colour catModulation { 0xfff07fc0 };
    const juce::Colour catPitch      { 0xff9fd86b };
    const juce::Colour catUtility    { 0xff8c9bb0 };

    inline juce::Colour categoryColour (const std::string& category)
    {
        if (category == "Character")  return catCharacter;
        if (category == "Dynamics")   return catDynamics;
        if (category == "Tone")       return catTone;
        if (category == "Space")      return catSpace;
        if (category == "Modulation") return catModulation;
        if (category == "Pitch")      return catPitch;
        return catUtility;
    }

    // legacy names (earlier panels) mapped onto the v0.3 palette
    const juce::Colour selFill   { 0xff22324a };
    const juce::Colour selEdge   = signalBlue;
    const juce::Colour beige     = accent;
    const juce::Colour beigeText { 0xffd99a5e };
    const juce::Colour beigeFill { 0xff2a2420 };
    const juce::Colour beigeTag  { 0xff33281f };
    const juce::Colour beigeEdge { 0xff9a7048 };
    const juce::Colour shadow    { 0x99000000 };
    const juce::Colour rowSel    { 0xff2a2420 };

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

    inline juce::Font serif (float h) { return make (faces().serif, h, "Georgia", juce::Font::italic | juce::Font::bold); }
    inline juce::Font mono (float h) { return make (faces().mono, h, "Consolas", juce::Font::plain); }
    inline juce::Font monoMedium (float h) { return make (faces().monoMedium, h, "Consolas", juce::Font::bold); }
    inline juce::Font sans (float h) { return make (faces().sans, h, "Segoe UI", juce::Font::plain); }
    inline juce::Font sansMedium (float h) { return make (faces().sansMedium, h, "Segoe UI Semibold", juce::Font::plain); }
    inline juce::Font sansSemi (float h) { return make (faces().sansSemi, h, "Segoe UI", juce::Font::bold); }

    // "LABEL" in quotes (Off-White detail)
    inline juce::String q (const juce::String& s) { return "\"" + s.toUpperCase() + "\""; }

    constexpr float kPanelRadius = 12.0f;
    constexpr float kCardRadius = 10.0f;

    // Soft drop shadow under a rounded rectangle (cheap: a few expanding translucent rounds).
    inline void softShadow (juce::Graphics& g, juce::Rectangle<float> r, float radius, float spread = 10.0f, float alpha = 0.35f)
    {
        for (int i = 3; i >= 1; --i)
        {
            const float e = spread * (float) i / 3.0f;
            g.setColour (juce::Colours::black.withAlpha (alpha / (float) (i + 1)));
            g.fillRoundedRectangle (r.translated (0.0f, e * 0.6f).expanded (e * 0.5f), radius + e * 0.5f);
        }
    }

    // Panel: rounded, lifted surface with a lighter header band, hairline edge and a
    // quoted caption + figure note.
    inline void drawPanel (juce::Graphics& g, juce::Rectangle<int> ri, const juce::String& caption = {},
                           const juce::String& figure = {}, int headerH = 42)
    {
        const auto r = ri.toFloat().reduced (0.5f);
        juce::ColourGradient grad (card, 0.0f, r.getY(), panel, 0.0f, r.getY() + 70.0f, false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r, kPanelRadius);
        g.setColour (juce::Colours::white.withAlpha (0.045f));
        g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + kPanelRadius, r.getRight() - kPanelRadius);
        g.setColour (edge);
        g.drawRoundedRectangle (r, kPanelRadius, 1.0f);
        if (caption.isEmpty())
            return;
        auto h = ri.withHeight (headerH).reduced (16, 0);
        g.setColour (text);
        g.setFont (mono (11.0f));
        const juce::String cap = q (caption);
        g.drawText (cap, h, juce::Justification::centredLeft);
        if (figure.isNotEmpty())
        {
            g.setColour (textFaint);
            g.setFont (mono (10.0f));
            const int w = juce::GlyphArrangement::getStringWidthInt (mono (11.0f), cap);
            g.drawText (figure.toUpperCase(), h.withTrimmedLeft (w + 16), juce::Justification::centredLeft);
        }
        g.setColour (edge);
        g.drawHorizontalLine (ri.getY() + headerH - 1, (float) ri.getX() + 1.0f, (float) ri.getRight() - 1.0f);
    }

    // Card (bridge block, Input/Output): raised, rounded, optional category stripe on the left,
    // glow ring when selected, dashed edge when not connected.
    inline void drawCard (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour edgeC, bool selected,
                          juce::Colour stripe = juce::Colours::transparentBlack, bool dashedEdge = false)
    {
        softShadow (g, r, kCardRadius, 12.0f, 0.45f);
        if (selected)
        {
            g.setColour (edgeC.withAlpha (0.16f));
            g.fillRoundedRectangle (r.expanded (4.0f), kCardRadius + 4.0f);
        }
        juce::ColourGradient grad (card.brighter (0.04f), 0.0f, r.getY(), panel, 0.0f, r.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r, kCardRadius);
        if (! stripe.isTransparent())
        {
            juce::Path clip;
            clip.addRoundedRectangle (r, kCardRadius);
            g.saveState();
            g.reduceClipRegion (clip);
            g.setColour (stripe);
            g.fillRect (r.withWidth (4.0f));
            g.restoreState();
        }
        if (dashedEdge)
        {
            juce::Path outline, d;
            outline.addRoundedRectangle (r, kCardRadius);
            const float dash[] = { 5.0f, 4.0f };
            juce::PathStrokeType (1.2f).createDashedStroke (d, outline, dash, 2);
            g.setColour (edgeC);
            g.fillPath (d);
        }
        else
        {
            g.setColour (edgeC);
            g.drawRoundedRectangle (r, kCardRadius, selected ? 1.6f : 1.0f);
        }
    }

    inline void drawTag (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& t, juce::Colour fg, juce::Colour edgeC, juce::Colour fill)
    {
        g.setColour (fill);
        g.fillRoundedRectangle (r.toFloat(), 5.0f);
        g.setColour (edgeC);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 5.0f, 1.0f);
        g.setColour (fg);
        g.setFont (mono (9.5f));
        g.drawText (t, r, juce::Justification::centred);
    }

    inline void cropMarks (juce::Graphics& g, juce::Rectangle<int> r)
    {
        g.setColour (textFaint.withAlpha (0.8f));
        const float x0 = (float) r.getX(), y0 = (float) r.getY(), x1 = (float) r.getRight(), y1 = (float) r.getBottom();
        g.drawLine (x0 - 9, y0 - 1, x0 - 1, y0 - 1, 1.0f);
        g.drawLine (x0 - 1, y0 - 9, x0 - 1, y0 - 1, 1.0f);
        g.drawLine (x1 + 1, y1 + 1, x1 + 9, y1 + 1, 1.0f);
        g.drawLine (x1 + 1, y1 + 1, x1 + 1, y1 + 9, 1.0f);
    }

    // Glowing curve: wide translucent strokes under a crisp one.
    inline void glowStroke (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width)
    {
        g.setColour (c.withAlpha (0.12f));
        g.strokePath (p, juce::PathStrokeType (width * 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c.withAlpha (0.25f));
        g.strokePath (p, juce::PathStrokeType (width * 2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Vertical gradient fill under a curve (strong near the top, fading to nothing).
    inline void fadeFill (juce::Graphics& g, const juce::Path& area, juce::Colour c, float top, float bottom, float strength = 0.38f)
    {
        juce::ColourGradient grad (c.withAlpha (strength), 0.0f, top, c.withAlpha (0.0f), 0.0f, bottom, false);
        g.setGradientFill (grad);
        g.fillPath (area);
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
        setColour (juce::Slider::textBoxHighlightColourId, accent.withAlpha (0.35f));
        setColour (juce::TextButton::buttonColourId, card);
        setColour (juce::TextButton::buttonOnColourId, selFill);
        setColour (juce::TextButton::textColourOffId, textDim);
        setColour (juce::TextButton::textColourOnId, signalBlue);
        setColour (juce::ComboBox::backgroundColourId, card);
        setColour (juce::ComboBox::outlineColourId, edge);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::arrowColourId, textDim);
        setColour (juce::PopupMenu::backgroundColourId, card);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, signalBlue.withAlpha (0.16f));
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedTextColourId, light);
        setColour (juce::PopupMenu::headerTextColourId, textFaint);
        setColour (juce::ToggleButton::textColourId, text);
        setColour (juce::ToggleButton::tickColourId, accent);
        setColour (juce::ToggleButton::tickDisabledColourId, textFaint);
        setColour (juce::TooltipWindow::backgroundColourId, card);
        setColour (juce::TooltipWindow::textColourId, text);
        setColour (juce::TooltipWindow::outlineColourId, edgeHi);
        setColour (juce::TextEditor::backgroundColourId, bg);
        setColour (juce::TextEditor::outlineColourId, edge);
        setColour (juce::TextEditor::focusedOutlineColourId, accent);
        setColour (juce::TextEditor::textColourId, text);
        setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.3f));
        setColour (juce::CaretComponent::caretColourId, accent);
        setColour (juce::AlertWindow::backgroundColourId, panel);
        setColour (juce::AlertWindow::textColourId, text);
        setColour (juce::AlertWindow::outlineColourId, edgeHi);
        setColour (juce::ScrollBar::thumbColourId, edgeHi);
        setColour (juce::ListBox::backgroundColourId, panel);
        setColour (juce::ProgressBar::backgroundColourId, card);
        setColour (juce::ProgressBar::foregroundColourId, signalBlue);
        setDefaultSansSerifTypeface (theme::faces().sans);
    }

    juce::Font getTextButtonFont (juce::TextButton& b, int h) override
    {
        if ((bool) b.getProperties().getWithDefault ("chip", false)) return theme::sans (12.0f);
        return theme::mono (juce::jmin (11.0f, (float) h * 0.45f));
    }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return theme::sans (13.0f); }
    juce::Font getPopupMenuFont() override { return theme::sans (13.0f); }
    juce::Font getLabelFont (juce::Label&) override { return theme::sans (13.0f); }
    juce::Font getAlertWindowMessageFont() override { return theme::sans (14.0f); }
    juce::Font getAlertWindowTitleFont() override { return theme::sansSemi (16.0f); }

    // Buttons: rounded; toggled = tinted with their colour ("colour" property, default signal blue);
    // "beige" = the warm call-to-action style (Export); "chip" = palette chip with a category dot.
    static juce::Colour buttonColour (juce::Button& b)
    {
        if (b.getProperties().contains ("colour")) return juce::Colour ((juce::uint32) (int) b.getProperties()["colour"]);
        return theme::signalBlue;
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hi, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const float rad = juce::jmin (7.0f, r.getHeight() * 0.3f);
        const bool on = b.getToggleState();
        const bool warm = (bool) b.getProperties().getWithDefault ("beige", false);
        const auto c = warm ? theme::accent : buttonColour (b);
        if (warm)
        {
            juce::ColourGradient grad (c.withAlpha (down ? 0.34f : (hi ? 0.28f : 0.22f)), 0.0f, r.getY(), c.withAlpha (0.07f), 0.0f, r.getBottom(), false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (r, rad);
            g.setColour (c);
            g.drawRoundedRectangle (r, rad, 1.0f);
            return;
        }
        juce::Colour fill = on ? c.withAlpha (0.14f) : theme::card;
        if (down) fill = fill.brighter (0.08f);
        else if (hi) fill = on ? c.withAlpha (0.2f) : theme::card.brighter (0.06f);
        g.setColour (fill);
        g.fillRoundedRectangle (r, rad);
        g.setColour (on ? c.withAlpha (0.9f) : (hi ? theme::edgeHi : theme::edge));
        g.drawRoundedRectangle (r, rad, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool hi, bool) override
    {
        const bool warm = (bool) b.getProperties().getWithDefault ("beige", false);
        const bool chip = (bool) b.getProperties().getWithDefault ("chip", false);
        auto area = b.getLocalBounds().reduced (4, 0);
        if (chip && b.getProperties().contains ("dot"))
        {
            const juce::Colour dot ((juce::uint32) (int) b.getProperties()["dot"]);
            g.setColour (dot);
            g.fillEllipse ((float) area.getX() + 6.0f, (float) area.getCentreY() - 3.0f, 6.0f, 6.0f);
            area.removeFromLeft (14);
        }
        juce::Colour c = warm ? theme::accent : (b.getToggleState() ? buttonColour (b) : (chip || hi ? theme::text : theme::textDim));
        g.setColour (c.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.drawFittedText (b.getButtonText(), area, juce::Justification::centred, 1);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
        g.setColour (theme::card);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (box.isMouseOver (true) ? theme::edgeHi : theme::edge);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
        juce::Path p;
        const float cx = (float) w - 13.0f, cy = (float) h * 0.5f;
        p.startNewSubPath (cx - 4, cy - 2);
        p.lineTo (cx, cy + 2);
        p.lineTo (cx + 4, cy - 2);
        g.setColour (theme::textDim);
        g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Knobs: arc in the control's colour ("arcColour" property; "beige" = warm), soft glow,
    // round caps, a raised cap and a pointer.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        const float size = (float) juce::jmin (w, h);
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).withSizeKeepingCentre (size, size).reduced (5.0f);
        const float cx = r.getCentreX(), cy = r.getCentreY(), rad = r.getWidth() * 0.5f;
        const float stroke = juce::jmax (3.0f, rad * 0.13f);
        juce::Colour arcC = (bool) s.getProperties().getWithDefault ("beige", false) ? theme::accent : theme::signalBlue;
        if (s.getProperties().contains ("arcColour")) arcC = juce::Colour ((juce::uint32) (int) s.getProperties()["arcColour"]);
        const auto caps = juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        juce::Path track;
        track.addCentredArc (cx, cy, rad - stroke * 0.5f, rad - stroke * 0.5f, 0.0f, a0, a1, true);
        g.setColour (theme::edge);
        g.strokePath (track, caps);

        const double zero = (s.getMinimum() < 0.0 && s.getMaximum() > 0.0) ? 0.0 : s.getMinimum();
        const float az = a0 + (float) s.valueToProportionOfLength (zero) * (a1 - a0);
        const float av = a0 + pos * (a1 - a0);
        juce::Path val;
        val.addCentredArc (cx, cy, rad - stroke * 0.5f, rad - stroke * 0.5f, 0.0f, juce::jmin (az, av), juce::jmax (az, av), true);
        g.setColour (arcC.withAlpha (0.22f));
        g.strokePath (val, juce::PathStrokeType (stroke * 2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (s.isEnabled() ? arcC : theme::textFaint);
        g.strokePath (val, caps);

        const float inner = rad * 0.64f;
        juce::ColourGradient capGrad (theme::card.brighter (0.12f), cx, cy - inner, theme::panel, cx, cy + inner, false);
        g.setGradientFill (capGrad);
        g.fillEllipse (cx - inner, cy - inner, inner * 2, inner * 2);
        g.setColour (theme::edgeHi);
        g.drawEllipse (cx - inner, cy - inner, inner * 2, inner * 2, 1.0f);
        g.setColour (theme::light);
        const float px = cx + std::sin (av) * inner * 0.78f, py = cy - std::cos (av) * inner * 0.78f;
        g.drawLine (cx + std::sin (av) * inner * 0.25f, cy - std::cos (av) * inner * 0.25f, px, py, 2.0f);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style != juce::Slider::LinearHorizontal)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
            return;
        }
        juce::Colour c = theme::signalBlue;
        if (s.getProperties().contains ("arcColour")) c = juce::Colour ((juce::uint32) (int) s.getProperties()["arcColour"]);
        else if ((bool) s.getProperties().getWithDefault ("beige", false)) c = theme::accent;
        const float cy = (float) y + (float) h * 0.5f;
        g.setColour (theme::edge);
        g.fillRoundedRectangle ((float) x, cy - 2.0f, (float) w, 4.0f, 2.0f);
        const double zero = (s.getMinimum() < 0.0 && s.getMaximum() > 0.0) ? 0.0 : s.getMinimum();
        const float zx = (float) s.getPositionOfValue (zero);
        g.setColour (c);
        g.fillRoundedRectangle (juce::jmin (zx, pos), cy - 2.0f, std::abs (pos - zx), 4.0f, 2.0f);
        g.setColour (c.withAlpha (0.25f));
        g.fillEllipse (pos - 8.0f, cy - 8.0f, 16.0f, 16.0f);
        g.setColour (theme::light);
        g.fillEllipse (pos - 5.0f, cy - 5.0f, 10.0f, 10.0f);
    }

    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (theme::mono (10.5f));
        l->setColour (juce::Label::textColourId, theme::textDim);
        l->setJustificationType (juce::Justification::centred);
        return l;
    }

    int getSliderThumbRadius (juce::Slider&) override { return 7; }

    // ---- popup menus: same universe as the panels ----
    void drawPopupMenuBackgroundWithOptions (juce::Graphics& g, int w, int h, const juce::PopupMenu::Options&) override
    {
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h);
        g.setColour (theme::bg);
        g.fillRect (r);
        juce::ColourGradient grad (theme::card.brighter (0.05f), 0.0f, 0.0f, theme::panel, 0.0f, (float) h, false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r.reduced (0.5f), 9.0f);
        g.setColour (theme::edgeHi);
        g.drawRoundedRectangle (r.reduced (0.5f), 9.0f, 1.0f);
    }

    int getPopupMenuBorderSizeWithOptions (const juce::PopupMenu::Options&) override { return 6; }

    void getIdealPopupMenuItemSizeWithOptions (const juce::String& text, bool isSeparator, int, int& w, int& h,
                                               const juce::PopupMenu::Options&) override
    {
        if (isSeparator) { w = 50; h = 11; return; }
        h = 28;
        w = juce::jmax (200, juce::GlyphArrangement::getStringWidthInt (theme::sans (13.0f), text) + 90);
    }

    void drawPopupMenuItemWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area, bool highlighted, const juce::PopupMenu::Item& item,
                                       const juce::PopupMenu::Options&) override
    {
        if (item.isSeparator)
        {
            g.setColour (theme::edge);
            g.drawHorizontalLine (area.getCentreY(), (float) area.getX() + 8.0f, (float) area.getRight() - 8.0f);
            return;
        }
        auto r = area.reduced (4, 1).toFloat();
        if (highlighted && item.isEnabled)
        {
            g.setColour (theme::signalBlue.withAlpha (0.14f));
            g.fillRoundedRectangle (r, 6.0f);
        }
        auto t = r.toNearestInt().reduced (10, 0);
        juce::Colour c = item.colour.isTransparent() ? theme::text : item.colour;
        if (! item.isEnabled) c = theme::textFaint;
        if (item.isTicked)
        {
            g.setColour (theme::accent);
            g.fillEllipse ((float) t.getX(), (float) t.getCentreY() - 3.0f, 6.0f, 6.0f);
        }
        t.removeFromLeft (14);
        if (item.subMenu != nullptr)
        {
            juce::Path p;
            const float ax = (float) t.getRight() - 4.0f, ay = (float) t.getCentreY();
            p.startNewSubPath (ax - 3.0f, ay - 4.0f);
            p.lineTo (ax + 1.0f, ay);
            p.lineTo (ax - 3.0f, ay + 4.0f);
            g.setColour (theme::textDim);
            g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        if (item.shortcutKeyDescription.isNotEmpty())
        {
            g.setColour (theme::textFaint);
            g.setFont (theme::mono (9.5f));
            g.drawText (item.shortcutKeyDescription.toUpperCase(), t, juce::Justification::centredRight);
        }
        g.setColour (c);
        g.setFont (theme::sans (13.0f));
        g.drawText (item.text, t, juce::Justification::centredLeft, true);
    }

    void drawPopupMenuSectionHeaderWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name,
                                                const juce::PopupMenu::Options&) override
    {
        g.setColour (theme::accent);
        g.setFont (theme::mono (9.5f));
        g.drawText (name.toUpperCase(), area.reduced (14, 0).withTrimmedTop (4), juce::Justification::centredLeft);
    }

    void drawTooltip (juce::Graphics& g, const juce::String& t, int w, int h) override
    {
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h);
        g.setColour (theme::card);
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (theme::edgeHi);
        g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);
        juce::AttributedString s;
        s.setJustification (juce::Justification::centredLeft);
        s.append (t, theme::sans (12.5f), theme::text);
        juce::TextLayout tl;
        tl.createLayoutWithBalancedLineLengths (s, (float) w - 20.0f);
        tl.draw (g, r.reduced (10.0f, 6.0f));
    }

    juce::Rectangle<int> getTooltipBounds (const juce::String& t, juce::Point<int> pos, juce::Rectangle<int> parent) override
    {
        juce::AttributedString s;
        s.append (t, theme::sans (12.5f), theme::text);
        juce::TextLayout tl;
        tl.createLayoutWithBalancedLineLengths (s, 340.0f);
        const int w = (int) tl.getWidth() + 22, h = (int) tl.getHeight() + 14;
        return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 18,
                                     pos.y > parent.getCentreY() ? pos.y - (h + 8) : pos.y + 16, w, h).constrainedWithin (parent);
    }
};
