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

// Atelier visual language: "Maison" - drawn from the logo.
// A Paris atelier's pattern-cutting table: warm linen paper, black ink, the logo's camel,
// and oxblood only for gain reduction / dynamic EQ / warnings. Headings and names are set in
// the logo's italic serif; technical labels in a typewriter mono; axes are rulers; the Bridge
// is pattern paper with garment-tag blocks and stitched parallel paths. Square corners,
// hairline rules, hatching instead of gradients, no glows.
namespace theme
{
    // paper and ink
    const juce::Colour bg        { 0xffece6da };   // linen paper (ground)
    const juce::Colour sheet     { 0xfff4f0e8 };   // a lighter sheet laid on the table (plots)
    const juce::Colour card      { 0xfffbf9f4 };   // garment tag / menus / fields
    const juce::Colour panel     = bg;
    const juce::Colour ink       { 0xff1c1916 };
    const juce::Colour edge      { 0xffcdc4b4 };   // hairline rule
    const juce::Colour edgeHi    { 0xff8f8676 };
    const juce::Colour grid      { 0xffded7ca };
    const juce::Colour gridMajor { 0xffcdc4b4 };
    const juce::Colour dashed    = edge;

    const juce::Colour text      = ink;
    const juce::Colour textDim   { 0xff5f574c };
    const juce::Colour textFaint { 0xff9a9081 };
    const juce::Colour light     = ink;

    // the logo's camel, deeper for marks that must read on paper
    const juce::Colour logo      { 0xffb39a6e };
    const juce::Colour accent    { 0xff94784a };
    const juce::Colour oxblood   { 0xff8c3b2e };

    // roles (names kept from earlier versions)
    const juce::Colour signalBlue = ink;            // output curve, active controls
    const juce::Colour signalDeep = textDim;
    const juce::Colour reference  = textFaint;      // input curve (dotted)
    const juce::Colour parallel   = accent;         // stitched parallel wires
    const juce::Colour good       { 0xff4f6b4a };
    const juce::Colour goodEdge   = good;
    const juce::Colour warn       { 0xff9a6b2a };
    const juce::Colour bad        = oxblood;
    const juce::Colour danger     = oxblood;

    inline juce::Colour categoryColour (const std::string&) { return textDim; }

    const juce::Colour catCharacter = textDim, catDynamics = textDim, catTone = textDim, catSpace = textDim,
                       catModulation = textDim, catPitch = textDim, catUtility = textDim;

    const juce::Colour selFill   = ink;
    const juce::Colour selEdge   = ink;
    const juce::Colour beige     = accent;
    const juce::Colour beigeText = accent;
    const juce::Colour beigeFill = card;
    const juce::Colour beigeTag  = card;
    const juce::Colour beigeEdge = accent;
    const juce::Colour shadow    = edge;
    const juce::Colour rowSel    = sheet;

    struct Faces
    {
        juce::Typeface::Ptr serif, heading, numeral, mono, monoMedium, sans, sansMedium, sansSemi;
        Faces()
        {
           #if ATELIER_HAS_FONTS
            auto load = [] (const void* d, int n) { return juce::Typeface::createSystemTypefaceFor (d, (size_t) n); };
            serif = load (BinaryData::CormorantGaramondSemiBoldItalic_ttf, BinaryData::CormorantGaramondSemiBoldItalic_ttfSize);
            heading = load (BinaryData::CormorantGaramondMediumItalic_ttf, BinaryData::CormorantGaramondMediumItalic_ttfSize);
            numeral = load (BinaryData::CormorantGaramondMedium_ttf, BinaryData::CormorantGaramondMedium_ttfSize);
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

    // the wordmark (logo weight)
    inline juce::Font serif (float h) { return make (faces().serif, h, "Georgia", juce::Font::italic | juce::Font::bold); }
    // section titles, block names, button words: the logo's italic at a lighter weight
    inline juce::Font heading (float h) { return make (faces().heading, h, "Georgia", juce::Font::italic); }
    // big readout numbers: upright serif
    inline juce::Font numeral (float h) { return make (faces().numeral, h, "Georgia", juce::Font::plain); }
    inline juce::Font mono (float h) { return make (faces().mono, h, "Consolas", juce::Font::plain); }
    inline juce::Font monoMedium (float h) { return make (faces().monoMedium, h, "Consolas", juce::Font::bold); }
    inline juce::Font sans (float h) { return make (faces().sans, h, "Segoe UI", juce::Font::plain); }
    inline juce::Font sansMedium (float h) { return make (faces().sansMedium, h, "Segoe UI Semibold", juce::Font::plain); }
    inline juce::Font sansSemi (float h) { return make (faces().sansSemi, h, "Segoe UI", juce::Font::bold); }

    // small technical label: upper-case (the mono font does the rest)
    inline juce::String q (const juce::String& s) { return s.toUpperCase(); }

    constexpr float kPanelRadius = 0.0f;
    constexpr float kCardRadius = 0.0f;

    inline void softShadow (juce::Graphics& g, juce::Rectangle<float> r, float = 0.0f, float = 0.0f, float = 0.0f)
    {
        g.setColour (edge);
        g.fillRect (r.translated (3.0f, 3.0f));
    }

    // Section: a black rule across the top, an italic serif title and a mono figure note.
    inline void drawPanel (juce::Graphics& g, juce::Rectangle<int> ri, const juce::String& caption = {},
                           const juce::String& figure = {}, int headerH = 42)
    {
        g.setColour (bg);
        g.fillRect (ri);
        g.setColour (ink);
        g.drawHorizontalLine (ri.getY(), (float) ri.getX(), (float) ri.getRight());
        if (caption.isEmpty())
            return;
        auto h = ri.withHeight (headerH).reduced (16, 0).withTrimmedTop (4);
        g.setColour (ink);
        const auto f = heading (26.0f);
        g.setFont (f);
        g.drawText (caption, h, juce::Justification::centredLeft);
        if (figure.isNotEmpty())
        {
            const int w = juce::GlyphArrangement::getStringWidthInt (f, caption);
            g.setColour (textFaint);
            g.setFont (mono (10.0f));
            g.drawText (figure.toUpperCase(), h.withTrimmedLeft (w + 14).withTrimmedTop (4), juce::Justification::centredLeft);
        }
    }

    // A vertical hairline separating a side column (readout, inspector).
    inline void columnRule (juce::Graphics& g, juce::Rectangle<int> r)
    {
        g.setColour (ink);
        g.drawVerticalLine (r.getX(), (float) r.getY(), (float) r.getBottom());
    }

    // Garment tag: paper card, ink hairline (dashed when loose), a flat offset shadow;
    // selection = camel edge with tailor's corner marks.
    inline void drawCard (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour edgeC, bool selected,
                          juce::Colour = juce::Colours::transparentBlack, bool dashedEdge = false)
    {
        g.setColour (edge);
        g.fillRect (r.translated (2.0f, 2.0f));
        g.setColour (card);
        g.fillRect (r);
        if (dashedEdge)
        {
            juce::Path outline, d;
            outline.addRectangle (r);
            const float dash[] = { 4.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (d, outline, dash, 2);
            g.setColour (textDim);
            g.fillPath (d);
        }
        else
        {
            g.setColour (selected ? accent : (edgeC == ink || edgeC == edge ? ink : edgeC));
            g.drawRect (r, selected ? 1.5f : 1.0f);
        }
        if (selected)
        {
            g.setColour (accent);
            const float o = 7.0f, l = 8.0f;
            auto corner = [&] (float x, float y, float dx, float dy)
            {
                g.drawLine (x, y, x + dx * l, y, 1.0f);
                g.drawLine (x, y, x, y + dy * l, 1.0f);
            };
            corner (r.getX() - o, r.getY() - o, 1, 1);
            corner (r.getRight() + o, r.getY() - o, -1, 1);
            corner (r.getX() - o, r.getBottom() + o, 1, -1);
            corner (r.getRight() + o, r.getBottom() + o, -1, -1);
        }
    }

    inline void drawTag (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& t, juce::Colour fg, juce::Colour, juce::Colour)
    {
        g.setColour (fg);
        g.setFont (mono (9.5f));
        g.drawText (t, r, juce::Justification::centredRight);
    }

    inline void cropMarks (juce::Graphics&, juce::Rectangle<int>) {}

    // A plain ink line (the old "glow" call sites draw clean strokes now).
    inline void glowStroke (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width)
    {
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Engraving-style diagonal hatching inside a path.
    inline void hatch (juce::Graphics& g, const juce::Path& area, juce::Colour c, float alpha = 0.22f, float spacing = 5.0f, bool rising = true)
    {
        const auto b = area.getBounds();
        if (b.isEmpty()) return;
        g.saveState();
        g.reduceClipRegion (area);
        g.setColour (c.withAlpha (alpha));
        const float h = b.getHeight();
        for (float x = b.getX() - h; x < b.getRight() + h; x += spacing)
            if (rising) g.drawLine (x, b.getBottom(), x + h, b.getY(), 0.7f);
            else g.drawLine (x, b.getY(), x + h, b.getBottom(), 0.8f);
        g.restoreState();
    }

    // Former gradient fill under curves: hatching now.
    inline void fadeFill (juce::Graphics& g, const juce::Path& area, juce::Colour c, float, float, float strength = 0.38f)
    {
        hatch (g, area, c, juce::jlimit (0.12f, 0.3f, strength * 0.7f));
    }

    // A tailor's ruler along the bottom of a log-frequency plot: fine ticks, long ticks at labels.
    inline void frequencyRuler (juce::Graphics& g, juce::Rectangle<float> plot, std::initializer_list<double> labelled, bool dense)
    {
        auto xOf = [&] (double f) { return plot.getX() + (float) (std::log (f / 20.0) / std::log (1000.0)) * plot.getWidth(); };
        g.setColour (textFaint);
        const int n = dense ? 300 : 200;
        for (int i = 0; i <= n; ++i)
        {
            const float x = plot.getX() + plot.getWidth() * (float) i / (float) n;
            g.drawLine (x, plot.getBottom(), x, plot.getBottom() + 3.0f, 0.6f);
        }
        g.setFont (mono (9.5f));
        for (double f : labelled)
        {
            const float x = xOf (f);
            g.setColour (ink);
            g.drawLine (x, plot.getBottom(), x, plot.getBottom() + 8.0f, 0.9f);
            g.setColour (textDim);
            const auto lab = f >= 1000.0 ? (std::fmod (f, 1000.0) == 0.0 ? juce::String ((int) (f / 1000.0)) : juce::String (f / 1000.0, 1)) + "k"
                                         : juce::String ((int) f);
            g.drawText (lab, juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 9.0f, 40.0f, 12.0f), juce::Justification::centred);
        }
    }

    // A round marker with an italic serif number (EQ bands).
    inline void numberedHandle (juce::Graphics& g, juce::Point<float> p, int number, bool selected, juce::Colour ring, float r = 10.0f)
    {
        g.setColour (selected ? ink : card);
        g.fillEllipse (p.x - r, p.y - r, r * 2, r * 2);
        g.setColour (selected ? ink : ring);
        g.drawEllipse (p.x - r, p.y - r, r * 2, r * 2, 1.3f);
        g.setColour (selected ? card : ink);
        g.setFont (heading (r * 1.45f));
        g.drawText (juce::String (number), juce::Rectangle<float> (p.x - r, p.y - r - 1.0f, r * 2, r * 2), juce::Justification::centred);
    }
}

class AtelierLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AtelierLookAndFeel()
    {
        using namespace theme;
        setColour (juce::ResizableWindow::backgroundColourId, bg);
        setColour (juce::DocumentWindow::backgroundColourId, bg);
        setColour (juce::Label::textColourId, ink);
        setColour (juce::Slider::textBoxTextColourId, ink);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, logo.withAlpha (0.35f));
        setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        setColour (juce::TextButton::buttonOnColourId, ink);
        setColour (juce::TextButton::textColourOffId, textDim);
        setColour (juce::TextButton::textColourOnId, card);
        setColour (juce::ComboBox::backgroundColourId, card);
        setColour (juce::ComboBox::outlineColourId, edge);
        setColour (juce::ComboBox::textColourId, ink);
        setColour (juce::ComboBox::arrowColourId, textDim);
        setColour (juce::PopupMenu::backgroundColourId, card);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, sheet);
        setColour (juce::PopupMenu::textColourId, ink);
        setColour (juce::PopupMenu::highlightedTextColourId, ink);
        setColour (juce::PopupMenu::headerTextColourId, ink);
        setColour (juce::ToggleButton::textColourId, ink);
        setColour (juce::ToggleButton::tickColourId, ink);
        setColour (juce::ToggleButton::tickDisabledColourId, textFaint);
        setColour (juce::TooltipWindow::backgroundColourId, card);
        setColour (juce::TooltipWindow::textColourId, ink);
        setColour (juce::TooltipWindow::outlineColourId, ink);
        setColour (juce::TextEditor::backgroundColourId, card);
        setColour (juce::TextEditor::outlineColourId, edge);
        setColour (juce::TextEditor::focusedOutlineColourId, accent);
        setColour (juce::TextEditor::textColourId, ink);
        setColour (juce::TextEditor::highlightColourId, logo.withAlpha (0.35f));
        setColour (juce::TextEditor::highlightedTextColourId, ink);
        setColour (juce::CaretComponent::caretColourId, ink);
        setColour (juce::AlertWindow::backgroundColourId, card);
        setColour (juce::AlertWindow::textColourId, ink);
        setColour (juce::AlertWindow::outlineColourId, ink);
        setColour (juce::ScrollBar::thumbColourId, edgeHi);
        setColour (juce::ListBox::backgroundColourId, card);
        setColour (juce::ProgressBar::backgroundColourId, grid);
        setColour (juce::ProgressBar::foregroundColourId, ink);
        setDefaultSansSerifTypeface (theme::faces().sans);
    }

    // Button kinds (component properties):
    //   default  - mono caps in a hairline box; toggled = solid ink with paper text
    //   "beige"  - the call to action (Export, Apply): solid ink, italic serif word
    //   "chip"   - palette chip: hairline box, italic serif word
    //   "link"   - navigation: italic serif word, underlined when toggled
    juce::Font getTextButtonFont (juce::TextButton& b, int h) override
    {
        if ((bool) b.getProperties().getWithDefault ("beige", false)) return theme::heading (juce::jmin (20.0f, (float) h * 0.55f));
        if ((bool) b.getProperties().getWithDefault ("chip", false)) return theme::heading (15.5f);
        if ((bool) b.getProperties().getWithDefault ("link", false)) return theme::heading (20.0f);
        return theme::mono (juce::jmin (10.5f, (float) h * 0.42f));
    }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return theme::sans (13.0f); }
    juce::Font getPopupMenuFont() override { return theme::sans (13.0f); }
    juce::Font getLabelFont (juce::Label&) override { return theme::sans (13.0f); }
    juce::Font getAlertWindowMessageFont() override { return theme::sans (14.0f); }
    juce::Font getAlertWindowTitleFont() override { return theme::heading (24.0f); }
    juce::Font getAlertWindowFont() override { return theme::sans (13.0f); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hi, bool down) override
    {
        auto r = b.getLocalBounds().toFloat();
        const bool on = b.getToggleState();
        if ((bool) b.getProperties().getWithDefault ("link", false))
            return;
        if ((bool) b.getProperties().getWithDefault ("beige", false))
        {
            g.setColour (down ? theme::textDim : (hi ? juce::Colour (0xff2e2a25) : theme::ink));
            g.fillRect (r);
            return;
        }
        if (on)
        {
            g.setColour (theme::ink);
            g.fillRect (r);
            return;
        }
        if (hi || down)
        {
            g.setColour (theme::card);
            g.fillRect (r);
        }
        g.setColour (hi ? theme::ink : theme::edge);
        g.drawRect (r, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool hi, bool) override
    {
        const bool warm = (bool) b.getProperties().getWithDefault ("beige", false);
        const bool link = (bool) b.getProperties().getWithDefault ("link", false);
        const bool on = b.getToggleState();
        juce::Colour c = warm ? theme::card : (on && ! link ? theme::card : (hi || link || (bool) b.getProperties().getWithDefault ("chip", false) ? theme::ink : theme::textDim));
        if (link && b.getProperties().contains ("colour")) c = juce::Colour ((juce::uint32) (int) b.getProperties()["colour"]);
        g.setColour (c.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
        const auto f = getTextButtonFont (b, b.getHeight());
        g.setFont (f);
        auto area = b.getLocalBounds().reduced (link ? 0 : 4, 0);
        g.drawFittedText (b.getButtonText(), area, link ? juce::Justification::centredLeft : juce::Justification::centred, 1);
        if (link && (on || hi))
        {
            const int w = juce::jmin (area.getWidth(), juce::GlyphArrangement::getStringWidthInt (f, b.getButtonText()));
            g.drawHorizontalLine (area.getCentreY() + (int) (f.getHeight() * 0.42f), (float) area.getX(), (float) (area.getX() + w));
        }
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h);
        g.setColour (theme::card);
        g.fillRect (r);
        g.setColour (box.isMouseOver (true) ? theme::ink : theme::edge);
        g.drawRect (r, 1.0f);
        juce::Path p;
        const float cx = (float) w - 13.0f, cy = (float) h * 0.5f;
        p.startNewSubPath (cx - 4, cy - 2);
        p.lineTo (cx, cy + 2);
        p.lineTo (cx + 4, cy - 2);
        g.setColour (theme::ink);
        g.strokePath (p, juce::PathStrokeType (1.1f));
    }

    // Knob = a watch dial: paper face with an ink rim, 11 ticks around it, the value as a
    // camel arc just outside the rim and an ink hand.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        const float size = (float) juce::jmin (w, h);
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).withSizeKeepingCentre (size, size).reduced (size * 0.16f);
        const float cx = r.getCentreX(), cy = r.getCentreY(), rad = r.getWidth() * 0.5f;
        const bool inkArc = s.getProperties().contains ("inkArc");
        // ticks
        for (int k = 0; k <= 10; ++k)
        {
            const float a = a0 + (a1 - a0) * (float) k / 10.0f;
            const float r1 = rad + 4.0f, r2 = rad + ((k % 5 == 0) ? 9.0f : 6.5f);
            g.setColour (theme::textDim);
            g.drawLine (cx + std::sin (a) * r1, cy - std::cos (a) * r1, cx + std::sin (a) * r2, cy - std::cos (a) * r2, 0.8f);
        }
        // value arc
        const double zero = (s.getMinimum() < 0.0 && s.getMaximum() > 0.0) ? 0.0 : s.getMinimum();
        const float az = a0 + (float) s.valueToProportionOfLength (zero) * (a1 - a0);
        const float av = a0 + pos * (a1 - a0);
        juce::Path val;
        val.addCentredArc (cx, cy, rad + 4.0f, rad + 4.0f, 0.0f, juce::jmin (az, av), juce::jmax (az, av), true);
        g.setColour (s.isEnabled() ? (inkArc ? theme::ink : theme::accent) : theme::textFaint);
        g.strokePath (val, juce::PathStrokeType (2.0f));
        // face
        g.setColour (theme::card);
        g.fillEllipse (cx - rad, cy - rad, rad * 2, rad * 2);
        g.setColour (theme::ink);
        g.drawEllipse (cx - rad, cy - rad, rad * 2, rad * 2, 1.0f);
        // hand
        g.drawLine (cx + std::sin (av) * rad * 0.22f, cy - std::cos (av) * rad * 0.22f,
                    cx + std::sin (av) * (rad - 3.5f), cy - std::cos (av) * (rad - 3.5f), 1.6f);
        g.fillEllipse (cx - 1.8f, cy - 1.8f, 3.6f, 3.6f);
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
        g.drawLine ((float) x, cy, (float) (x + w), cy, 1.0f);
        // ruler ticks
        for (int k = 0; k <= 10; ++k)
        {
            const float tx = (float) x + (float) w * (float) k / 10.0f;
            g.drawLine (tx, cy + 3.0f, tx, cy + (k % 5 == 0 ? 7.0f : 5.0f), 0.7f);
        }
        const double zero = (s.getMinimum() < 0.0 && s.getMaximum() > 0.0) ? 0.0 : s.getMinimum();
        const float zx = (float) s.getPositionOfValue (zero);
        g.setColour (theme::ink);
        g.drawLine (juce::jmin (zx, pos), cy, juce::jmax (zx, pos), cy, 2.0f);
        juce::Path d;
        d.addRectangle (-4.0f, -4.0f, 8.0f, 8.0f);
        d.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::pi * 0.25f).translated (pos, cy));
        g.setColour (theme::card);
        g.fillPath (d);
        g.setColour (theme::ink);
        g.strokePath (d, juce::PathStrokeType (1.2f));
    }

    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (theme::mono (10.5f));
        l->setColour (juce::Label::textColourId, theme::ink);
        l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        l->setColour (juce::Label::outlineColourId, theme::edge);
        l->setJustificationType (juce::Justification::centred);
        return l;
    }

    int getSliderThumbRadius (juce::Slider&) override { return 6; }

    // ---- popup menus: a tag of paper with an ink edge and an italic title ----
    void drawPopupMenuBackgroundWithOptions (juce::Graphics& g, int w, int h, const juce::PopupMenu::Options&) override
    {
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h);
        g.setColour (theme::card);
        g.fillRect (r);
        g.setColour (theme::ink);
        g.drawRect (r, 1.0f);
    }

    int getPopupMenuBorderSizeWithOptions (const juce::PopupMenu::Options&) override { return 6; }

    void getIdealPopupMenuItemSizeWithOptions (const juce::String& text, bool isSeparator, int, int& w, int& h,
                                               const juce::PopupMenu::Options&) override
    {
        if (isSeparator) { w = 50; h = 11; return; }
        h = 27;
        w = juce::jmax (210, juce::GlyphArrangement::getStringWidthInt (theme::sans (13.0f), text) + 96);
    }

    void drawPopupMenuItemWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area, bool highlighted, const juce::PopupMenu::Item& item,
                                       const juce::PopupMenu::Options&) override
    {
        if (item.isSeparator)
        {
            g.setColour (theme::edge);
            g.drawHorizontalLine (area.getCentreY(), (float) area.getX() + 10.0f, (float) area.getRight() - 10.0f);
            return;
        }
        if (highlighted && item.isEnabled)
        {
            g.setColour (theme::sheet);
            g.fillRect (area);
            g.setColour (theme::ink);
            g.fillRect (area.getX(), area.getY() + 5, 2, area.getHeight() - 10);
        }
        auto t = area.reduced (12, 0);
        juce::Colour c = item.colour.isTransparent() || item.colour == theme::textDim ? theme::ink : item.colour;
        if (! item.isEnabled) c = theme::textFaint;
        if (item.isTicked)
        {
            g.setColour (theme::ink);
            g.fillEllipse ((float) t.getX(), (float) t.getCentreY() - 3.0f, 6.0f, 6.0f);
        }
        t.removeFromLeft (12);
        if (item.subMenu != nullptr)
        {
            juce::Path p;
            const float ax = (float) t.getRight() - 4.0f, ay = (float) t.getCentreY();
            p.startNewSubPath (ax - 3.0f, ay - 4.0f);
            p.lineTo (ax + 1.0f, ay);
            p.lineTo (ax - 3.0f, ay + 4.0f);
            g.setColour (theme::ink);
            g.strokePath (p, juce::PathStrokeType (1.1f));
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
        auto r = area.reduced (12, 0);
        g.setColour (theme::ink);
        g.setFont (theme::heading (18.0f));
        // section headers arrive upper-cased from older call sites: show them as titles
        auto title = name.trim();
        if (title == title.toUpperCase()) title = title.substring (0, 1) + title.substring (1).toLowerCase();
        g.drawText (title.removeCharacters ("\""), r, juce::Justification::centredLeft);
        g.setColour (theme::edge);
        g.drawHorizontalLine (area.getBottom() - 1, (float) area.getX() + 10.0f, (float) area.getRight() - 10.0f);
    }

    void drawTooltip (juce::Graphics& g, const juce::String& t, int w, int h) override
    {
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h);
        g.setColour (theme::card);
        g.fillRect (r);
        g.setColour (theme::ink);
        g.drawRect (r, 1.0f);
        juce::AttributedString s;
        s.setJustification (juce::Justification::centredLeft);
        s.append (t, theme::sans (12.5f), theme::ink);
        juce::TextLayout tl;
        tl.createLayoutWithBalancedLineLengths (s, (float) w - 20.0f);
        tl.draw (g, r.reduced (10.0f, 6.0f));
    }

    juce::Rectangle<int> getTooltipBounds (const juce::String& t, juce::Point<int> pos, juce::Rectangle<int> parent) override
    {
        juce::AttributedString s;
        s.append (t, theme::sans (12.5f), theme::ink);
        juce::TextLayout tl;
        tl.createLayoutWithBalancedLineLengths (s, 340.0f);
        const int w = (int) tl.getWidth() + 22, h = (int) tl.getHeight() + 14;
        return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 18,
                                     pos.y > parent.getCentreY() ? pos.y - (h + 8) : pos.y + 16, w, h).constrainedWithin (parent);
    }

    void drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& e) override
    {
        g.setColour (e.hasKeyboardFocus (true) ? theme::accent : theme::edge);
        g.drawRect (0, 0, w, h, e.hasKeyboardFocus (true) ? 1 : 1);
    }

    void fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor& e) override
    {
        g.setColour (e.findColour (juce::TextEditor::backgroundColourId));
        g.fillRect (0, 0, w, h);
    }
};

// For top-level dialogs (they are not children of the editor): lives as long as the plugin binary.
inline AtelierLookAndFeel& dialogLookAndFeel() { static AtelierLookAndFeel l; return l; }
